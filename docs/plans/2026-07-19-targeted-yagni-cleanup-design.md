# Targeted YAGNI Cleanup Design

## Goal and constraints

Clean the four local commits and the tracked `src/` tree without changing
user-visible typing behavior. The cleanup follows the repository rules: keep
the hook path allocation-free and lock-free, mutate TSF text only through edit
sessions, prefer concrete code over hypothetical extension points, and retain
active recovery mechanisms. Existing user changes to release notes, engine
binaries, and engine documentation are outside this work.

HookHijackDetector remains active. Its polling, drift detection, key recovery,
reinstall behavior, and AdaptiveTick integration are not redesign targets. Only
the unused `observedKeyDowns_` field and its no-op assignments are removed.

## Architecture and data flow

TSF macro handling keeps action-key behavior intact because Enter, Tab, and
navigation keys may need test-phase handling to preserve single-press host
behavior. Printable English macro triggers are different: `OnTestKeyDown`
will claim the key without changing document text, and `OnKeyDown` will run the
existing macro expansion edit session. A small core decision helper classifies
text-producing triggers and is covered by platform-neutral tests.

Macro TOML reloads are removed from keystroke callbacks. Shared-memory scalar
configuration still applies immediately, but a changed macro generation marks
the in-process macro table stale. Initialization and foreground-focus handling
may reload the table from disk. Until the next focus transition, a stale table
is cleared so old macros cannot expand. This deliberately prefers a temporarily
unavailable macro after an external in-place config edit over blocking a host
application's key callback with filesystem I/O and a cache mutex.

The unused phonology plugin path is collapsed to the only production operation:
a concrete `FindTonePosition` function. TypingEngine calls it directly. The
unused syllable-validation/completability API, two virtual interfaces, default
adapter, factory, singleton plumbing, and tests that only prove that plumbing
are deleted. Existing tone-placement and full engine regression tests remain.

## Error handling

`ReplacePrecedingTextEditSession` reports success only if both replacement and
caret selection succeed. Output injectors retain the held-Shift workaround but
also record how many synthetic events Win32 accepted. If a partial batch
delivered the leading Shift-up, the injector sends a best-effort marked
Shift-down before returning failure. A zero-event failure does not inject an
unnecessary modifier event.

No new mutex, heap allocation, exception boundary, worker thread, or virtual
dispatch is added to the low-level hook path. The phonology cleanup removes a
virtual call from tone placement. TSF disk access remains outside key callbacks.

## Implementation tasks and verification

1. Add failing decision and partial-send recovery tests, then implement TSF
   printable-trigger deferral, focus-bound macro reload, selection error
   propagation, and Shift recovery.
2. Preserve tone-placement tests while replacing the unused phonology
   interfaces with the concrete function; remove stale source comments and
   build entries.
3. Delete the dead HookHijackDetector field, inactive settings-button branch,
   unused CSS selectors, and known stale output ownership comments.
4. Run formatting checks, committed-diff whitespace checks, the hook no-mutex
   audit, Linux tests with the exact committed Rust library, Windows `/W4 /WX`
   builds, Windows tests, and the Rust ABI tests against the intended binary.

Acceptance requires unchanged typing test results, passing injector recovery
tests, zero file reads or config-cache mutex acquisitions from TSF key callbacks,
and no modifications to the user's pre-existing files.
