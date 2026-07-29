# Issue #234: TSF Composition Correctness

## Goal

Fix the Windows 10 Notepad TSF regressions reported in issue #234 without
removing VKey's ability to reopen and edit a committed Vietnamese word:

- sentence auto-caps must not arm from an unreadable or stale text range;
- navigation must commit the live composition and then move the host caret once;
- pre-edit text must occupy a real document range instead of visually covering
  following text;
- Space must be committed exactly once;
- reopening and retyping `Không` must preserve `Không`, not `KHông`.

The change applies to the shared TSF path rather than an executable-specific
Notepad compatibility branch.

## Pre-code Gate

### Q1 — Layer

Composition ownership, document ranges, caret updates, and key-event pairing
belong in `src/tsf`. Pure decisions that need Linux regression coverage may
live in a small `src/core` header, but no hook or UI code participates.

### Q2 — Performance

The design keeps the existing one synchronous edit session per handled key.
New bookkeeping is O(1): a few enum/boolean comparisons, estimated below
1 microsecond of CPU time per key beyond the host-controlled TSF edit session.
There is no additional document scan beyond the existing 64-character
auto-cap inspection.

### Q3 — Native alternative

Use the native TSF range returned by `ITfInsertAtSelection` and the selection
owned by `ITfContext`. A hook-side injector, window-message fallback, or
Notepad-specific text channel would add a second document writer and is
rejected because it would recreate ordering and duplicate-character races.

### Q4 — No-lock / no-exception

No mutex, file I/O, wait, thread, or new exception boundary is introduced.
COM interfaces remain scoped with `CComPtr` or explicit ownership at the TSF
boundary. All document mutations stay inside read/write edit sessions.

### Q5 — Trade-off

The benefit is one standards-aligned lifecycle shared by Notepad, Chromium,
and other TSF hosts. The cost is touching the central composition path, so
the change requires both automated decision/engine regressions and a Windows
manual matrix. Chromium's self-sufficient `OnKeyDown` fallbacks and the
single-press behavior of action keys remain explicit invariants.

## Architecture and Data Flow

Starting text and updating text have different semantics. For a new word, the
start edit session inserts the initial rendered text at the current selection,
receives the inserted range, and starts the composition over that non-empty
range. Subsequent updates replace the authoritative composition range with
normal `SetText` flags. `TF_ST_CORRECTION` is not used: Microsoft defines it
for correcting existing content while preserving its properties, whereas
VKey is creating or replacing active pre-edit text.

The composition manager owns the current `ITfComposition` and context.
Updating a live composition collapses the selection to the composition end
only when typing changes the rendered pre-edit. Ending a composition does not
perform a second caret move. Therefore an arrow key follows this sequence:

```
OnTestKeyDown(action key)
  -> commit edit session: final text + EndComposition
  -> engine reset
  -> return pfEaten = FALSE
  -> Notepad performs one native navigation step
```

Space remains a printable key. It is claimed during the test phase and handled
in the key phase by one atomic `CommitWithChar(L' ')`. The paired key cache is
consumed once so a duplicated or stale callback cannot both commit a space and
pass the same physical event to the host. Chromium may omit the test phase, so
`OnKeyDown` must still make the same decision from live engine state.

## Auto-caps and Reopen-word Behavior

The preceding-text inspection becomes evidence-based. “No characters could be
shifted” is not automatically equivalent to “document start” unless the edit
session successfully establishes that the current selection is at the context
start. Failed range movement, failed text reads, non-empty selections, and
partial/unsupported host behavior all fail closed: no auto-cap. Sentence
punctuation and line-start classification continue to use
`ComputeShouldAutoCap`.

Reopen-word remains enabled. A word range is accepted only when the requested
backward shift covers exactly the detected word. Revive sessions start the
composition on that existing word range and preserve the casing represented by
the word or a matching raw snapshot. Their text update must not grow beyond the
composition range into following document text. The regression sequence
`Không -> delete to K -> type hoong -> Space -> VKey` must yield exactly
`Không VKey`.

## Error Handling

Every TSF operation that establishes a required invariant returns success or
failure to its edit session. If initial insertion, composition start, exact
range shift, or text replacement fails, VKey clears its local composition
state and lets the physical key pass where possible. It must never report a
key as consumed after failing to place its text, and it must never interpret a
failed read as sentence-start evidence.

`OnCompositionTerminated` remains the recovery path for host-initiated
termination. Normal VKey commits end and release the composition once; local
engine reset occurs after the synchronous edit session completes.

## Implementation Plan

### Task 1 — Regression contracts

Acceptance:

- Auto-cap probe failure and ambiguous context start do not capitalize.
- Exact word-range coverage is required before revive.
- Space/action-key decisions cannot consume one key event twice.
- Simple Telex casing regression reproduces the `K`-then-retype sequence.

Verification:

- Run the focused new GTest filters and confirm the intended pre-fix failures.

### Task 2 — Composition lifecycle

Acceptance:

- Initial text is inserted before `StartComposition`.
- Updates use normal replacement flags and propagate failure.
- Normal commit does not perform an extra caret movement.

Verification:

- Windows Debug and Release `VKeyTSF` builds succeed with `/WX`.

### Task 3 — Auto-caps, revive, and key pairing

Acceptance:

- Ambiguous text probes fail closed.
- Revive uses an exact existing range and preserves casing.
- Space is emitted once; action keys retain single-press pass-through behavior.

Verification:

- Focused regressions pass, followed by the full Linux suite.

### Checkpoint — Windows host matrix

- Windows 10 Notepad: all four issue reproductions.
- Chromium host: punctuation, Space, Enter, Ctrl+A, and arrow non-regressions.
- Notepad++/Scintilla: natural Space path remains unchanged.

## Risks

- Host implementations differ in selection/range behavior. Mitigation: fail
  closed on ambiguous reads and retain Chromium/Scintilla branches.
- Starting composition over inserted text may expose a host that rejects the
  composition. Mitigation: insertion/start are one edit session with explicit
  failure cleanup and a pass-through result.
- COM termination callbacks may be synchronous. Mitigation: transfer/release
  ownership in a defined order and review the final diff for re-entrancy.
