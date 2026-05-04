# NexusKey Refactor — Phase 0a Handoff

## TL;DR

NexusKey's hook engine has long-standing race-condition bugs (x2 space, ghost
key, tone misplacement under fast typing). We brainstormed a 5-phase refactor
(Single-Owner architecture, IOutputInjector factory, ...) but realized we
couldn't measure success without a deterministic test harness.

**Phase 0a built that harness.** It's complete, locked, and ready for PR.
Baseline: **5 PASS / 6 FAIL** on an 11-case stress corpus, **L1 hook p99 = 16 ms**
worst case. Phase 1+ refactor work measures itself against this frozen state.

## Branch state

- Branch: `refactor/phase-0a-test-runner` (15 commits ahead of master `43fb4c1`)
- Cross-platform tests: **250 / 250** pass on Linux + Windows MSVC
- Tool location: `tools/NextKeyTestRunner/`
- Frozen baseline: `docs/baselines/perf-baseline-43fb4c1.{md,csv,xml}`
- Branch only adds tooling. The single comment-only diff in
  `src/app/system/HookEngine.cpp` does not change runtime behavior.

## Read in this order

1. **`_bmad-output/brainstorming/brainstorming-session-2026-05-03-1201.md`** (986 lines)
   The full design dialog: Reverse Brainstorm → Constraint Mapping → Chaos Engineering
   → Subsystem Compatibility → Crash Resilience → (extension) Six Thinking Hats
   → First Principles → SCAMPER → Idea Organization with Sprint 0a-5 plan.
   Search "Phase 7" for the actionable sprint table.

2. **`docs/baselines/perf-baseline-43fb4c1.md`**
   Frozen reference state, per-case L1 timing, failure categorization for
   Phase 1+ planning, reproduction steps, and the **Phase 1 merge gate**
   (must not regress 5/11 PASS, must fix ≥1 FAIL).

3. **`tools/NextKeyTestRunner/README.md`**
   Build + run + CLI flag reference + source layout.

4. **`git log 43fb4c1..HEAD --oneline`** plus full bodies — every commit has
   10-30 lines of context (what + why + manual smoke test results).

## Sprint 1 (next, 2 weeks): Single-Owner Architecture

**Spec:** brainstorm session Phase 7.4 row 1 + Phase 6.3 SCAMPER design
(`WaitOnAddress` + 1-thread worker for config event + heartbeat).

**Files to touch:**
- `src/app/system/HookEngine.cpp` — single-owner refactor (drop `recursive_mutex`)
- `src/core/ipc/SharedStateManager.cpp` — atomic flag publication
- New `src/app/system/MainThreadWorker.{h,cpp}` for config + heartbeat

**Gate (must hold before merge):**
- ≥ 5 PASS on chaos corpus (no PASS regression)
- ≥ 1 new PASS (must fix at least one of the 6 known FAILs)
- p99 hook callback ≤ 16 ms (current baseline)

**Verification per commit:**
1. Build NexusKey debug + restart
2. `NextKeyTestRunner --corpus chaos.toml --hook-log ... --junit ... --perf-csv ...`
3. Diff `perf-baseline-<new-sha>.csv` vs `perf-baseline-43fb4c1.csv`
4. On merge, commit new baseline as `docs/baselines/perf-baseline-<sha>.{md,csv,xml}`

## Known limitations (do NOT re-discover)

| Limitation | Where documented | Impact |
|---|---|---|
| Sub-ms inter-key impossible from user-mode driver — floors at ~3 ms | baseline.md observation #1 | Phase 1 perf goals use 3 ms target, not 1 ms |
| L1 timing only via post-mortem log parse (NexusKey buffers + locks log while running) | commit `2efd180` body | Tool prompts user to stop NexusKey at end of run |
| **Heisenbug**: `_IONBF` log slowed hook enough to mask race-condition bugs entirely | commit `d58bb4e` revert + `2efd180` body | **DO NOT re-enable `_IONBF`** without verifying bugs still reproduce against baseline |
| Uppercase Vietnamese passthrough in `--raw` mode returns false from VkKeyScanW | commit `fd4a14f` body | Use lowercase Vietnamese in TOML `keys` field |

## Sprint 1-5 roadmap (from brainstorm Phase 7.4)

| # | Focus | Est | Source |
|---|---|---|---|
| **1** | T2: Single-Owner + `WaitOnAddress` + 1 worker thread | 2 weeks | Phase 6.3 SCAMPER |
| 2 | T3: `IOutputInjector` factory + matrix harness | 2 weeks | Phase 6.2 First Principles |
| 3 | T4: `noexcept` enforcement + SEH = WER fallthrough | 1 week | Phase 5 Crash Resilience |
| 4 | T5: Passive self-healing (heartbeat) | 1 week | Phase 2 architecture |
| 5 | Integration + full regression | 1 week | T1-5 wired together |

**V2 backlog** (deferred, with full context preserved in brainstorm Phase 6.3):
- Hook fast-path foreground detection (profile switch 50 ms → 1 ms)
- ETW tracing (replaces post-mortem log parse with real-time)
- External crash watcher process (alternative to WER fallthrough)

## Anti-abandon rules (from brainstorm Phase 6.1 Blue Hat)

Phase 0a was completed with zero abandoned days. Same discipline carries to Sprint 1+:
- Daily commit (10 LOC counts) — streak prevents abandonment.
- User-reported bugs unrelated to current sprint → issue tracker, **do not** fix inline.
- Crash / data-loss bugs are the exception — pause sprint, fix, return.
- Don't merge a half-baked sprint branch into master. Ship full sprints.

## Build commands (quick reference)

### Linux test build (recommended for CI / engine logic)
```bash
cmake -B build-linux -G "Unix Makefiles" -DCMAKE_BUILD_TYPE=Debug
cmake --build build-linux --target NextKeyTestRunnerTests
./build-linux/tests/NextKeyTestRunnerTests
```
Expected: 250 / 250 tests pass in < 5 ms.

### Windows full build (from WSL)
```bash
powershell.exe -Command 'cd "\\wsl.localhost\Ubuntu-24.04\home\phatmt\code\NexusKey\build"; cmake --build . --target NextKeyTestRunner --config Debug'
powershell.exe -Command 'cd "\\wsl.localhost\Ubuntu-24.04\home\phatmt\code\NexusKey\build"; cmake --build . --target NextKeyApp --config Debug'
```

### Reproduce baseline
See `docs/baselines/perf-baseline-43fb4c1.md` "Reproduction" section.

## Open questions / next decisions

When picking up Sprint 1, you'll need to decide:
1. Land `MainThreadWorker` in same PR as Single-Owner refactor, or split?
2. Keep current `SharedStateManager` seqlock, or migrate to `WaitOnAddress` event?
3. Drop `recursive_mutex` cold turkey or behind a feature flag?

The brainstorm doc Phase 6.3 SCAMPER has these tradeoffs analyzed. Read the
"Tổng hợp 4 đề xuất" table before committing to an approach.
