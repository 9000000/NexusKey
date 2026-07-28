# Delayed Focus Snapshot Safety

## Problem

Focus classification is intentionally asynchronous. A WinEvent producer
captures the foreground request, `MainThreadWorker` performs the expensive
window/app probes, and the hook thread consumes the immutable result through
`HookCommandMailbox`.

Physical keys can overtake that round trip. Before this change, the delayed
result called `ResetComposition()` unconditionally. The application UI kept
the already-rendered word, but the engine lost its matching composition. The
next tone key or in-word Backspace then operated on an empty/different buffer,
which matches the intermittent report that deleting the whole word and typing
after a space recovers the IME.

## Decision

Each classification request carries two pieces of ordering evidence:

- `requestSerial`: monotonically identifies the newest foreground request.
- `inputEpochAtRequest`: snapshots the physical-key epoch when the request is
  published.

The hook applies one pure policy:

| Snapshot | Input since request | Live typing context | Result |
|---|---:|---:|---|
| Older than latest request | any | any | Drop |
| Latest | yes | yes | Defer to boundary |
| Latest | no | any | Apply now |
| Latest | yes | no | Apply now |

A live typing context means either `engine_->Count() > 0` or a non-empty raw
macro buffer. A deferred snapshot is revalidated on every natural hook drain.
A newer focus request invalidates it immediately; otherwise it applies once the
typing context is empty.

## Data Flow and Ownership

`OnFocusChanged` remains produce-only. It allocates one immutable
`FocusClassifyRequest`, publishes it through
`atomic<shared_ptr<const FocusClassifyRequest>>`, and signals the worker. This
follows the worker-thread doctrine for payloads larger than one pointer and
keeps the HWND, request serial and input epoch consistent.

The worker pre-drops a request that is already superseded, performs
classification, copies the ordering evidence into `FocusClassification`, and
posts the result. The hook is the final authority: it compares both generations
immediately before any typing-state mutation.

`deferredFocusApply_` is hook-thread-owned. It needs no mutex or atomic because
only `DrainHookCommands` reads or writes it.

## Transaction Boundary

The full typing-context apply is deferred as one transaction. It includes
composition reset, per-word flags, injector selection, TSF/excluded/forced
mode, Smart Switch, encoding and input-method overrides. Splitting those fields
mid-word would create a hybrid state that is harder to reason about than the
original race.

Anti-hook-hijack protection is intentionally separate and idempotent. Chromium
and known-hijacker detector/burst state applies promptly even when the typing
transaction waits, because delaying that protection would expose the current
word to a competing hook. It does not mutate composition or dispatch semantics.

## Performance and Failure Behavior

The hook hot path adds one relaxed 64-bit atomic increment per real physical
key-down. There is no new allocation, mutex, I/O or explicit wake syscall on
that path. Focus requests allocate only in the WinEvent cold path. Deferred
apply uses the existing natural drain: typically the worker tick, or at latest
the next key before a new word is dispatched.

The policy cannot consume a key: command drain still completes before
`ProcessKeyDown`, and the route only selects drop/defer/apply. A delayed result
with a live word takes the defer branch and returns control to the unchanged key
dispatch. If evidence is stale or incomplete, the safe behavior is to preserve
the current word and wait for a newer classification.

## Known Ceilings

Deferral buys word integrity by delaying the context switch, which leaves two
bounded windows. Both are preferable to the race they replace, but they are the
new failure modes and should be recognised rather than re-derived.

1. **Uncommitted word carried across an app switch.** If the user leaves a word
   open, switches app, and types into the new app before the classification
   lands, those keys extend the old buffer under the old app's rules until the
   next word boundary. Bounded by that boundary; a click or commit ends it. The
   alternative is wiping a visible word, which is the bug this design fixes.

2. **A transient focus event can strand a deferred transaction.** Any newer
   request serial drops the retained snapshot. When that newer request
   classifies to a helper window, `ApplyFocusOnHookThread` early-returns and the
   real app's typing context never applies from this path. Recovery is
   `OnTickPoll`'s stale-PID fallback (≤ one tick), which works only because the
   helper path deliberately leaves `lastForegroundPid_` unchanged. That coupling
   is load-bearing: changing where the PID is updated re-opens this window.

## Verification

- Pure exhaustive matrix for serial, input epoch and live-context combinations.
- Interleaving regression for a delayed result arriving over a visible word.
- Backspace-at-arrival test proving the pending focus does not consume or reset
  the key's composition.
- Deferred apply at boundary, newer-request invalidation, deferred replacement
  by the latest result, and out-of-order worker completion.
- Existing mailbox coalescing/stress tests.
- Windows and Linux full unit suites, hook-thread audit and sanitizers.
