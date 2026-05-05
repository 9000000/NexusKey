# perf-baseline-d12-richedit-fix-chaos

Sprint 1 D12 corpus capture, after the `EM_REPLACESEL` all-output fix
(commit `582dab2`). Companion files in this directory:

- `perf-baseline-d12-richedit-fix-chaos.csv` — per-case verdict + L1 stats
- `perf-baseline-d12-richedit-fix-chaos.xml` — JUnit-style report
- this file — narrative summary

## Capture environment

- App: NexusKey Debug build (commit `582dab2`)
- Target window: Win11 New Notepad (`RichEditD2DPT`, `editMsg=1`)
- Driver: `NextKeyTestRunner.exe --corpus chaos.toml --junit ... --perf-csv ... --hook-log ...`
- Date: 2026-05-05

## Verdict — 11 / 11 PASS

| # | Case | Inter-key (µs) | Verdict | L1 mean (ms) | L1 p99 (ms) | L1 max (ms) | vs locked baseline `43fb4c1` |
|---|---|---|---|---|---|---|---|
| 1.1 | ghost-hoaf-bs-t | 10 000 | ✅ PASS | 13 | 14 | 14 | byte-identical (was PASS) |
| 1.2 | tone-ghost-toans-bs3-i | 5 000 | ✅ PASS | 9 | 11 | 11 | byte-identical (was PASS) |
| 1.3 | escape-bs-aa-b | 5 000 | ✅ PASS | 10 | 13 | 13 | byte-identical (was PASS) |
| 2.1 | x2-space-vieejt-nam | 1 000 | ✅ **PASS** | 4 | 7 | 7 | **flipped** (was FAIL `ệiet nam`) |
| 2.2 | word-boundary-xin-chao-ban | 1 000 | ✅ **PASS** | 4 | 8 | 8 | **flipped** (was FAIL `xàạnchao ban`) |
| 2.3 | en-vn-transition-hello-vieejt | 1 000 | ✅ **PASS** | 4 | 8 | 8 | **flipped** (was FAIL `helệo viet`) |
| 3.3 | engine-stress-truongf | 500 | ✅ **PASS** | 4 | 6 | 6 | **flipped** (was FAIL `tờương`) |
| 5.1 | case-tracking-Giar | 5 000 | ✅ PASS | 6 | 9 | 9 | byte-identical (was PASS) |
| 5.2 | vowel-start-uongs | 5 000 | ✅ **PASS** | 8 | 10 | 10 | **flipped** (was FAIL `ốngg`) |
| 5.3 | cross-word-bs-vieejt-nam-bs4-s | 1 000 | ✅ **PASS** | 6 | 9 | 9 | **flipped** (was FAIL `etết`) |
| 6.1 | autocap-binh-thuongf | 5 000 | ✅ PASS | 8 | 10 | 10 | byte-identical (was PASS) |

**6 of 6 stable FAILs flipped to PASS.** Stable PASSes preserved byte-identical. Worst-case L1 p99 = 14 ms (1.1) — equal to the lowest in the Sprint 1 series (D11 = 14 ms, D6r2 = 14 ms). The 3.3 engine-stress p99 dropped from 14 ms (D11) → 6 ms (D12) — the EM_REPLACESEL all-output route is faster than the SendInput-fallback path it replaces because there are no posted events to drain.

## L1 trajectory across Sprint 1 (worst-case p99 per phase)

| Phase | L1 worst p99 (ms) | Notes |
|---|---|---|
| Pre-spike (`a28f1ea`) | 12 | D3 baseline |
| D4 spike | 17 | mutex commented out — Outcome B confirmed mutex not the bug source |
| D5 atomic vnMode | 18 | within scheduler noise |
| D5.1 atomic method/tsf | 16 | improvement |
| D5.2 atomic rest | 16 | cap unchanged |
| D6 RCU config | 18 (run 2) / 14 (run 3) | heisenbug envelope |
| D11 plain mutex | 14 | lowest in series |
| **D12 EM_REPLACESEL fix** | **14 (1.1)** | **chaos verdict: 11/11 PASS** |

The series shows L1 timing was always within 12–18 ms regardless of the architectural change — confirming the chaos failures were not a latency / mutex contention problem. They were the RichEditD2DPT sent/posted reorder race documented in HANDOFF.

## Why this passes when prior phases couldn't

The D5–D11 architectural cleanup (atomic flags, RCU config, MainThreadWorker, mutex downgrade) all preserved the chaos verdict at 5 / 11 because none of them touched the actual bug source. The bug lived in `HookEngine`'s output channel mixing strategy, not its input/state ownership:

- `HandleAlphaKey` simple-append passthrough (physical key → app via `WM_KEYDOWN`, posted)
- `ReplaceComposition` EM_REPLACESEL path (sent message)
- `SendBackspaces` (SendInput VK_BACK, posted)
- Commit-trigger passthrough (physical → posted)
- Commit-undo BS passthrough (physical → posted)

Under chaos burst input (1 ms / 500 µs inter-key) on `RichEditD2DPT` (which renders WM_KEYDOWN async on the compositor thread), `EM_GETSEL` returns a stale (low) caret while queued physical events haven't run yet. Sent EM_REPLACESEL pre-empts the posted queue → applies replacement at stale caret → posted events drain afterwards and corrupt the result. The chaos shapes (`tờương`, `ờnương`, `ườngng`, `việtnam`, `việế`) are all exact re-renderings of that interleave at different drain points.

D12's fix routes every output channel through `EM_REPLACESEL` for `useEditMsgPath_` apps — no posted output anywhere. The all-or-nothing rule is what makes it safe; previous mixed strategies were inherently racy for async-render hosts.

## D12 merge gate (revised, 2026-05-05)

The Sprint 1 plan §D D12 gate has evolved through this branch:

- Original: ≥ 1 chaos FAIL flip + 0 regress, sustained error down (impossible per D1).
- D4 revision: no regression on stable PASS / FAIL byte shapes, sustained zero regression, L1 p99 ≤ 18 ms, plus D12.5 single-FAIL fix.
- D12.5 revision: D12.5 plus-clause dropped (engine layer cleared, fix scope moved to D8+).
- **D12 outcome (this capture):** **6 chaos FAILs flipped to PASS unconditionally.** The "≥ 1 flip" condition is exceeded by 6×; stable PASSes preserved; L1 p99 = 14 ms ≤ 18 ms; sustained baseline unchanged because no engine logic / data flow was modified by D12 (only output channel routing). Gate ✅ MET.

## Restoration / regression detection

- D7 audit script (`tools/audit/check_hook_thread_no_mutex.sh`) continues to exit 0 — Phase B compliance maintained.
- The `useEditMsgPath_` all-output rule is enforced by code paths in `HookEngine.cpp` (search for `useEditMsgPath_.load`). A future commit that re-introduces a posted output path (passthrough alpha, raw `SendBackspaces`, raw `InjectKey(VK_BACK)`) for an editMsg app will resurrect the chaos failures.
- Recommended Sprint 2 follow-up: codify the rule in the audit script — grep `HookEngine.cpp` for any `SendInput` / `InjectKey` call site reachable from the editMsg branch and fail the build.

## Reproduce

```
NextKeyTestRunner.exe ^
    --corpus tools\NextKeyTestRunner\corpus\chaos.toml ^
    --junit docs\baselines\perf-baseline-d12-richedit-fix-chaos.xml ^
    --perf-csv docs\baselines\perf-baseline-d12-richedit-fix-chaos.csv ^
    --hook-log build\Debug\NexusKey_hook.log
```

Requires NexusKey Debug build of commit `582dab2` running, foreground = Win11 New Notepad.
