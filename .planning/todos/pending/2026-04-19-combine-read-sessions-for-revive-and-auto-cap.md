---
created: 2026-04-19T12:52:53.761Z
title: Combine read sessions for revive and auto-cap
area: tsf
files:
  - src/tsf/CompositionEditSession.h
  - src/tsf/EngineController.cpp:327-347
---

## Problem

`HandleKey` A-Z path can fire up to 3 sync edit sessions per keystroke when
the engine is empty:

1. `ReadPrecedingWordEditSession` — Vietnamese-word scan inside `TryReviveOnType`
2. `ReadPrecedingCharsEditSession` — raw-chars scan inside `ShouldAutoCapitalize`
3. `StartCompositionEditSession` / `UpdateCompositionEditSession` — applies the
   PushChar result

Two of those (1 and 2) read overlapping text from the same document range.
Redundant sync edit sessions add latency on slow hosts and make a hot path
noisier than it needs to be.

Surfaced during review of commit `b41d51e` (M1 finding). Not a correctness
issue — current code works — but worth cleaning up if keystroke latency ever
becomes a complaint.

## Solution

Refactor into a single `InspectAndReviveEditSession` that:

- Reads preceding text once (existing `ReadPrecedingCharsEditSession` logic).
- Derives both the Vietnamese-word length (for revive) and the auto-cap
  decision (for capitalization) from the same buffer.
- If word + not English: runs the revive surgery inline (StartComposition +
  ShiftStart + Seed + PushChar) — one session does read + revive atomically.
- Otherwise returns `{revived=false, shouldAutoCap=bool}` to the caller.

Caller flow in `HandleKey` becomes:
```
auto* session = new InspectAndReviveEditSession(pContext, engine, &mgr, config, ch);
RequestEditSession(pContext, session);
if (session->Revived()) return true;
if (session->ShouldAutoCap()) ch = towupper(ch);
session->Release();
// Normal PushChar + StartComposition flow
```

Session count per A-Z keystroke at new-composition boundary:
- Before: 3 (two reads + one write) in non-revive path; 2 (read + write) in revive path
- After: 2 (one combined + one write) in non-revive path; 1 (combined) in revive path

Priority: performance polish, not correctness. Defer until real slowness
reported. Risk is moderate — session mixes read + write responsibilities so
error paths need care. Keep existing separate-session classes around until
the combined version proves stable.
