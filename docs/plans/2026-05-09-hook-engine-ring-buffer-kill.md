# Hook→Engine Ring Buffer — Kill Decision

**Date:** 2026-05-09
**Type:** Architectural decision memo (not an implementation spec)
**Decision:** **KILL H6b** (Lock-free SPSC ring buffer between Hook and Engine).
**Status:** Approved by anh 2026-05-09. Pending doc updates only — no code changes.

---

## TL;DR

`HookEngine.cpp:724` runs the engine synchronously on the Windows LL keyboard
hook thread. `CODE_GOVERNANCE.md §3` proposed decoupling via an SPSC ring
buffer (Sprint 4 roadmap, tracked as H6b in `REFACTOR_STATUS.md`). After
re-examining the premise and the existing measurement baselines, the
investment does not clear the cost/benefit bar:

- **Hook callback p99 = 14–17ms** measured across 5 hosts (Notepad,
  Notepad++, Chrome, Discord, ChatGPT). 55/55 chaos PASS at 500µs–10ms
  inter-key. Windows `LowLevelHooksTimeout` = 300ms → **~280ms headroom**.
- The "x2 space race" the SPSC ring was meant to resolve was already
  fixed in Sprint 1 D12 — root cause was an `EM_REPLACESEL` sent/posted
  reorder in the output channel, not a TSF+Hook double-capture.
- Async model would introduce a baseline +1–2ms lag on **every**
  keystroke (including English / gaming / hotkeys), where the sync
  model has zero lag for non-Vietnamese keystrokes (>90% on dev/gaming
  machines).

Closing H6b with evidence; H6a (watchdog half) was already shipped via
PR #154.

## What was the gap

`docs/REFACTOR_STATUS.md` Section C lists:

> H6 — **Sprint 4 §3 SPSC ring + watchdog** — Rule #11 next-stage
> compliance. Source: CODE_GOVERNANCE.md §3. Effort: Sprint scale.
> Priority: LOW (roadmap).

`docs/CODE_GOVERNANCE.md §3` ("IPC + Concurrency") prescribes:

> SPSC lock-free ring buffer in shared memory. TSF and Hook NEVER
> capture keys in parallel — one is producer, the other observer.
> Resolves the historical "x2 space" race.
>
> Minimal hook + watchdog: `HookEngine.cpp` only does `SetWindowsHookEx`
> and pushes events into the SPSC ring. A separate watchdog handles
> Windows silently dropping the hook on `LowLevelHooksTimeout`, hook
> hijacking, and re-installation.

`docs/CODING_RULES/11-hook-system-rules.md §11.1` sets the architectural
ideal:

> The hook callback MUST return within 1 ms under all conditions.
> Engine processing: < 1 µs. SendInput dispatch: ~0.1 ms. Total
> budget: < 1 ms.

The gap is the unbuilt half: a lock-free SPSC ring that decouples
`LowLevelKeyboardProc` (producer) from a worker thread that drives the
engine and emits output (consumer).

## What was already shipped

| Half | Status | Evidence |
|---|---|---|
| **H6a — Watchdog / self-healing** | ✅ DONE via PR #154 (merge `d37d0e1`) | `HookSelfHealer` + `HeartbeatPublisher` + `NexusKeyWatchdog.exe` |
| **H6b — SPSC ring buffer** | ❌ Not built | This memo's subject |

PR #154 attribution to H6a was implicit; this memo makes it explicit.

## Why kill — evidence base

### Latency: not a problem in measured reality

Sprint 1 baseline series (`docs/baselines/perf-baseline-*.md`) measures
L1 = hook callback latency from `LowLevelKeyboardProc` entry to exit.
Worst-case p99 across the series:

| Phase | L1 worst p99 (ms) | Source |
|---|---|---|
| Pre-spike `a28f1ea` | 12 | D3 baseline |
| D4 spike | 17 | mutex commented out |
| D5 atomic vnMode | 18 | scheduler noise |
| D5.1 atomic method/tsf | 16 | improvement |
| D5.2 atomic rest | 16 | cap unchanged |
| D6 RCU config | 14–18 (run-dependent) | heisenbug envelope |
| D11 plain mutex | 14 | **lowest in series** |
| D12 EM_REPLACESEL fix | 14 (case 1.1) | chaos 11/11 PASS |
| ChannelTraits sweep PR #123 | within envelope | **5 hosts, 55/55 PASS** |

The ChannelTraits sweep covered the exact host classes that own the
synchronous Sleep / SendMessage sites in `HookEngine.cpp`:

- **Notepad Win11 RichEditD2DPT** — `Sleep(1ms)` × up to 30 retry in
  `HookEngine.cpp:3189` (caret-lag).
- **Discord (Electron)** — `Sleep(6ms)` in `SplitDispatchInjector.cpp:79`.
- **ChatGPT (Chromium)** — same SplitDispatch path.
- **Notepad++ (Win32)** — `SendMessageTimeoutW` × 4–5 per commit
  (`TryEditMessagePaste`, `HookEngine.cpp:2123`).
- **Chrome omnibox** — Win32 batched SendInput.

55/55 PASS, p99 within the same 14–17ms envelope. The Sleep / SendMessage
sites consume budget but do not push callbacks anywhere near the OS limit.

The conclusion was already written in `perf-baseline-43fb4c1.md:86`
(2026-05-04):

> Hook-callback p99 jitter is small. No case exceeds 17 ms p99 even on
> the slowest configuration. The Windows `LowLevelHooksTimeout` is
> 300 ms, so even worst-case callbacks have ~280 ms of headroom. The
> original brainstorm hypothesis that mutex contention pushed callback
> time anywhere near the timeout limit is **not supported by this
> baseline** — bugs are functional (state machine / composition), not
> raw latency.

### Premise: "x2 space race" was a different bug

CODE_GOVERNANCE §3 cites the historical "x2 space" race as motivation
for the SPSC ring (decoupling TSF and Hook capture). Two facts disqualify
this premise for the current codebase:

1. **TSF disabled in production** (memory `project_hook_only.md`).
   `TSF_ACTIVE` flag (SharedFlags 0x0008) acts as a mutex — hook
   pass-through for TSF apps, DLL pass-through for non-TSF apps. Two
   capture paths never run on the same keystroke. The "parallel
   double-capture" the ring would resolve does not exist today.

2. **The chaos x2-space FAIL was a different mechanism.** Sprint 1 D12
   (`perf-baseline-d12-richedit-fix-chaos.md`) flipped `2.1 x2-space-
   vieejt-nam` from FAIL `ệiet nam` to PASS `việt nam` via commit
   `582dab2`. The fix was: route every output channel through
   `EM_REPLACESEL` for `useEditMsgPath_` hosts, with a 30ms caret-lag
   retry. Root cause was the WinUI 3 RichEditD2DPT async-render race
   where sent `EM_REPLACESEL` pre-empts posted `SendInput` BS. Not a
   TSF+Hook double-capture.

Memory `project_h6_premise_questioned.md` flagged this 2026-05-08; this
memo verifies and closes that flag with evidence.

### Async model would regress UX baseline

A decoupled ring requires the hook to EAT every keystroke (return 1)
because the callback cannot know yet whether the engine will transform
or pass-through. The worker thread runs the engine, then either:

- **Transform path:** SendInput the new char sequence (no different
  from today)
- **Pass-through path:** SendInput the original key to compensate for
  the hook eating it

This adds **+1–2ms thread schedule lag to 100% of keystrokes**,
including English typing, gaming, and hotkeys. Today's sync model has
zero added lag on the >90% of keystrokes that don't trigger Vietnamese
processing — the hook returns `CallNextHookEx` and the OS delivers the
key in the same callback frame.

This is why Unikey, EVKey, and NexusKey v2.1 chose the sync model
despite knowing the lock-free ring pattern. Memory
`project_v21_evkey_chaos_pass.md` (2026-05-04): both ship sync and pass
chaos.toml at 1ms inter-key cleanly. They accept the 5–6ms in-callback
Sleep to avoid the universal lag tax.

### Race conditions a build would create

Even ignoring the UX regression, six new race conditions appear at the
producer/consumer boundary:

| # | Race | Solvable? | Cost |
|---|---|---|---|
| 1 | EAT-then-PASS lag on every keystroke | ❌ FUNDAMENTAL | UX baseline regression |
| 2 | Modifier state desync (`GetKeyState` reads OS, not snapshot) | ✅ snapshot at hook, push with event | Low |
| 3 | Focus change mid-dispatch (key lands in wrong app) | ⚠️ partial — re-check `GetForegroundWindow` at dispatch; edge cases on rapid focus flips | Medium |
| 4 | Reset drain protocol (mouse/arrow leaves ghost output) | ✅ CMD_RESET in same queue, drop-after-reset | Medium |
| 5 | Queue full under chaos load | ⚠️ heuristic sizing, no clean answer for full+blocked | Medium |
| 6 | Synth-event tracking cross-thread | ✅ atomic + memory order, has Sprint 1 precedent | Low |

#1 is fundamental and not engineering-fixable. The other five are
solvable but together represent multi-week implementation + audit work.

## Cost-benefit summary

| Dimension | Sync (today) | Async ring (proposed H6b) |
|---|---|---|
| L1 p99 measured | 14–17ms | unmeasured; estimate +schedule jitter |
| Headroom vs `LowLevelHooksTimeout` | 280ms | likely similar |
| Lag on non-Vietnamese keystrokes | **0ms** | **+1–2ms always** |
| Implementation effort | already shipped | 1–2 sprint, HIGH RISK |
| Resolves a measured bug? | n/a | No — premise already fixed in D12 |
| Aligns with Rule #11.1 ideal (1ms) | violates ideal | aligns ideal but violates UX |

Rule #11.1's 1ms target is an architectural ideal that has drifted from
codebase reality (HookEngine.cpp:3173-3174 explicitly accepts the
`LowLevelHooksTimeout` budget). The honest move is to update the rule to
match measured reality, not rebuild the architecture to match an aspirational
number that has no user-visible benefit.

## Decision

**KILL H6b** — close the SPSC ring buffer line item with this memo as
evidence pointer. Document the architectural ideal vs measured reality
mismatch in Rule #11.1 so future readers do not re-derive the same
analysis.

## Reversal triggers

Reopen H6b as a fresh proposal (not a zombie roadmap item) if any of the
following surface with evidence:

1. `HookSelfHealer` logs ≥1 production hook-drop event in 30 days of
   normal use.
2. Chaos.toml regresses to FAIL with timeout signature on a host class
   not previously failing.
3. TSF Phase 2/3 revival lands and `TSF_ACTIVE` mutex is replaced by
   parallel TSF+Hook capture — at which point the original "x2 space"
   premise becomes live.
4. Production user report of fast-typing key loss that cannot be fixed
   by a localized injector / engine patch.

A reversal proposal must include the new evidence link and re-run the
cost-benefit comparison against this memo.

## Doc updates required

Four files need to reflect this decision. Listed for application as a
follow-up commit; not done in this memo's commit.

### 1. `docs/REFACTOR_STATUS.md` Section C

Replace the H6 row:

```
| H6 | Sprint 4 §3 SPSC ring + watchdog — Rule #11 next-stage compliance | ... | Sprint scale | LOW (roadmap) |
```

with two rows:

```
| ~~H6a~~ | ~~Watchdog / self-healing~~ — `HookSelfHealer` + `HeartbeatPublisher` + `NexusKeyWatchdog.exe` | DONE | ✅ PR #154 `d37d0e1` |
| ~~H6b~~ | ~~SPSC ring buffer Hook→Engine~~ — CLOSED 2026-05-09 per `docs/plans/2026-05-09-hook-engine-ring-buffer-kill.md`. Latency measured 14–17ms p99 across 5 hosts (280ms `LowLevelHooksTimeout` headroom); async model would regress UX baseline (+1–2ms on every non-Vietnamese keystroke). | N/A | ❌ Wontfix |
```

### 2. `docs/CODE_GOVERNANCE.md §3`

Rewrite the section header status from `[STATUS: roadmap, Sprint 4]` to
`[STATUS: watchdog shipped PR #154; SPSC ring closed 2026-05-09 — see plans/2026-05-09-hook-engine-ring-buffer-kill.md]`.

Replace the SPSC ring bullet with a closed-with-evidence note. Keep the
side-channel context bullet as a TSF Phase 2/3 prerequisite (not part of
this kill).

### 3. `docs/CODING_RULES/11-hook-system-rules.md §11.1`

Replace the single 1ms total budget with a two-tier statement:

- **Engine pure CPU (state-machine work):** < 1µs — still aspirational and
  measured by gtest engine reproducers.
- **Total hook callback (including SendInput / SendMessage / Sleep
  budgeted output dispatch):** target < 30ms p99 in chaos.toml across
  the 5 baseline hosts, hard ceiling at `LowLevelHooksTimeout / 3` ≈ 100ms.

Cite the Sprint 1 baseline series and ChannelTraits sweep as the measured
floor.

### 4. `docs/TODO.md` — "Architecture proposal alignment review" Module A

Change the Module A row from `~75%` / "Lock-free SPSC ring buffer
between Hook→Engine. Proposed in Sprint 4 §3 H6b but premise (x2 space
race) unverified" to `CLOSED 2026-05-09` with pointer to this memo.

## Out of scope

- Rule #11.1 wording rewrite — stays as a doc TODO; not part of this
  decision.
- Production HOOK_LOG instrumentation — not needed; baselines already
  cover the question.
- Re-running chaos sweep on additional hosts (Slack, VS Code, Teams) —
  defer until a host class shows the failure mode in user reports.
- Reopening or revising the watchdog (PR #154) scope — that half is done.

## References

- `docs/REFACTOR_STATUS.md` Section C, Section F sequencing rule
- `docs/CODE_GOVERNANCE.md §3` (current text — to be updated per above)
- `docs/CODING_RULES/11-hook-system-rules.md §11.1, §11.2`
- `docs/baselines/perf-baseline-43fb4c1.md` (line 86 conclusion)
- `docs/baselines/perf-baseline-d11-plain-mutex-chaos.md`
- `docs/baselines/perf-baseline-d12-richedit-fix-chaos.md`
- `docs/baselines/perf-baseline-d12-chrome-cross-app.md`
- `docs/baselines/perf-baseline-channeltraits-chaos.md` (5-host sweep)
- `docs/TODO.md` "Architecture proposal alignment review" (2026-05-08)
- `src/app/system/HookEngine.cpp:724` (sync `ProcessKeyDown` call site)
- `src/app/system/HookEngine.cpp:3173-3174` (budget acceptance comment)
- `src/app/system/HookEngine.cpp:3189` (caret-lag Sleep retry)
- `src/app/output/SplitDispatchInjector.cpp:79` (Electron/Console Sleep)
- PR #154 `d37d0e1` (HookSelfHealer + HeartbeatPublisher + Watchdog.exe)
- Memory: `project_h6_premise_questioned.md`, `project_hook_only.md`,
  `project_v21_evkey_chaos_pass.md`, `feedback_design_philosophy.md`,
  `feedback_test_dont_theorize.md`
