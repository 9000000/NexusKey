# NexusKey Refactor — Sprint 1 Handoff (D1 done, D2 next)

## TL;DR

NexusKey's hook engine has long-standing race-condition bugs (x2 space, ghost
key, tone misplacement under fast typing). Phase 0a built the test harness;
Sprint 1 (this branch) is bringing the hook into compliance with the
just-committed Rule #11 (no mutex on hook hot path) via single-owner refactor.

**Where we are right now (2026-05-04):** Foundation docs + test infrastructure
done. Baselines locked. **The single-owner refactor itself has not started** —
that begins at D3 (diagnostic spike) per the plan. A teammate continuing here
should pick up at **D2** (sustained-edit corpus encoding) before D3 unlocks.

| Layer | Status | Reference |
|---|---|---|
| Project philosophy + Rule #11 | ✅ Committed `0e5322e` | `docs/PHILOSOPHY.md`, `docs/CODING_RULES/11-hook-system-rules.md` |
| Sprint 1 plan (14-day, A0→A→B→C→D→E) | ✅ Committed `ec9798e` | `docs/plans/sprint-1-single-owner-refactor.md` |
| D0: NextKeyTestRunner extensions (text field, --convert, edit_distance) | ✅ Committed `a372a27` + Windows fix `3459642` | `tools/NextKeyTestRunner/` |
| D1: Sustained forward baseline locked | ✅ **0.41 % error** at master | `docs/baselines/perf-baseline-3459642-sustained-forward.{csv,xml,md}` |
| D2: Sustained edit baseline (typo + cross-word) | 🔜 **NEXT** | corpus to extend in `tools/NextKeyTestRunner/corpus/sustained.toml` |
| D3+: Diagnostic spike, foundation refactor, MainThreadWorker, drop mutex | ⏳ Per plan | `docs/plans/sprint-1-single-owner-refactor.md` |

## Branch state

- Branch: `refactor/phase-1-single-owner` (4 commits ahead of master `43fb4c1`)
- Cross-platform tests: **281 / 281** pass on Linux (Windows MSVC verified)
- Tool location: `tools/NextKeyTestRunner/` (now with `--convert`, `text` field, `edit_distance` verdict)
- Frozen baselines:
  - `docs/baselines/perf-baseline-43fb4c1.{md,csv,xml}` — chaos corpus (11 cases, 5 PASS / 6 FAIL)
  - `docs/baselines/perf-baseline-3459642-sustained-forward.{md,csv,xml}` — sustained forward (1 case, 0.41 % error)
- Branch adds docs + test infra only. **No production code touched yet.** The single-owner refactor begins at D3.

## Significant finding from D1

Master state already handles realistic forward Vietnamese typing at **0.41 % error
rate** — chaos-style bugs are speed-bound and don't reproduce at 50 ms inter-key.
This means:

1. The original "≥ 30 % improvement on sustained forward" gate is unworkable
   (30 % of 0.41 % is below noise). Plan revised: forward sustained is now
   "no-regression" anchor only.
2. The interesting Sprint 1 movement signal will live in **chaos FAIL flips**
   (≥ 1 of 6) and **D2 sustained-edit baseline** (when it exists). Forward
   typing is solid territory; bugs cluster in edit scenarios (BS-and-retry,
   cross-word backspace) per chaos category 5.3 and brainstorm Phase 3.

See `docs/baselines/perf-baseline-3459642-sustained-forward.md` Observations
#1–3 for the full reasoning.

## Read in this order (for a teammate picking up D2)

1. **`docs/PHILOSOPHY.md`** (~10 min)
   Four pillars (Nhanh / Nhẹ / Mượt / Mở rộng-no-runtime-cost), test-first,
   three pre-code questions. Highest-level filter for all design decisions.

2. **`docs/CODING_RULES/11-hook-system-rules.md`** (~5 min)
   Operationalizes Pillar #1 — 1 ms hook budget, forbidden ops, contention
   law, atomic + RCU patterns, two-phase classification. Mandatory before
   touching anything reachable from `LowLevelKeyboardProc`.

3. **`docs/plans/sprint-1-single-owner-refactor.md`** (~10 min)
   The 14-day plan. D0–D1 are ✅. **Pick up at D2.** Each day has Q1/Q2/Q3
   pre-code answers, test-first artifact, DoD, and commit message draft.

4. **`docs/baselines/perf-baseline-43fb4c1.md`** (chaos baseline, 5 PASS / 6 FAIL)
   and **`docs/baselines/perf-baseline-3459642-sustained-forward.md`**
   (sustained forward, 0.41 % error). The "before" pictures Sprint 1 must
   not regress.

5. **`_bmad-output/brainstorming/brainstorming-session-2026-05-03-1201.md`**
   (986 lines) — full design history. Phase 6.3 SCAMPER + Phase 7.4 sprint
   table. Read only if Sprint 1 hits an unexpected blocker; otherwise the
   plan + rules are sufficient.

6. **`_bmad-output/brainstorming/brainstorming-session-2026-05-04-0734.md`**
   pre-mortem session that produced the philosophy + plan. Optional, but
   shows the reasoning behind the assumption verdicts.

7. **`git log 43fb4c1..HEAD --oneline`** plus full bodies — every commit has
   pre-code questions answered + test diff. Read commit `a372a27` (D0
   infrastructure) and `3459642` (Windows fix) for the testing patterns.

8. **`tools/NextKeyTestRunner/README.md`** + the runner's `--help` output —
   for `--corpus`, `--convert`, `--hook-log` flag references.

## Sprint 1 — D2 next (sustained edit baseline)

**Goal:** Encode 2–3 cases for typo + cross-word edit scenarios into
`tools/NextKeyTestRunner/corpus/sustained.toml`, run them on the SAME
master `43fb4c1` runtime that the rest of D0–D1 measured, lock the
baseline.

**Approach (per plan §A0 D2):**

Each scenario built from the `text_with_edits`-style pattern:
- Pure text segments → run through `Telex.h::StrToTelex` (use the new `text` field; auto-converts).
- Backspace interjections → explicit `\b` characters or `keys` field.
- For each case: write expected as the *intended Vietnamese final text*,
  drive the actual sequence, observe what the engine produces, set
  `expected = <observed>` if it matches user expectation. **Do not predict
  the output from telex math — verify on the actual engine.**

Verify any hand-written telex via the new CLI:
```powershell
.\build\tools\Debug\NextKeyTestRunner.exe --convert "việt có dấu"
# -> vieejt cos daasu
```

Reference scenario types (see plan A0 §D2):
- `inline-typo-correction` — type a wrong letter mid-word, BS, retype.
- `cross-word-edit-fix` — type a word, commit, BS past committed text,
  retype to fix. (User's example: `việt có dấu` → BS×N → `viết có dấu`.)
- `mixed-edit-session` — 50 forward + 3 typos + 1 cross-word edit (optional).

**DoD for D2:** baseline files
`docs/baselines/perf-baseline-<sha>-sustained-edit.{csv,xml,md}` committed.
Each new case in `sustained.toml` has a comment citing kTable lines
(`Telex.h:25–44`) for hand-written telex segments.

**Anti-pattern reminder (from `feedback_never_hand_encode_telex` memory):**
Do NOT generate Vietnamese telex from memory. Always cite the kTable entry.
The `--convert` CLI is the source of truth.

## Sprint 1 — D3+ (refactor proper)

After D2 baseline is locked, the diagnostic spike begins (D3 lock pre-spike
snapshot, D4 minimal mutex drop). See plan for full day-by-day. The 3 Rule
#11 violations to remove:

| File:line | Function |
|---|---|
| `src/app/system/HookEngine.cpp:647` | `LowLevelKeyboardProc` |
| `src/app/system/HookEngine.cpp:677` | `WinEventProc` |
| `src/app/system/HookEngine.cpp:705` | `LowLevelMouseProc` |

**Gate (revised after D1):**
- Chaos: ≥ 5 PASS, ≥ 1 FAIL flip vs `perf-baseline-43fb4c1`.
- Hook callback p99: ≤ 16 ms (chaos burst).
- Sustained forward: no regression vs D1 baseline (≤ 0.41 % error).
- Sustained edit: no regression vs D2 baseline (≥ 30 % improvement target).

**Verification per commit:**
1. Build NexusKey debug + restart.
2. `NextKeyTestRunner --corpus chaos.toml --hook-log ... --junit ... --perf-csv ...`
3. `NextKeyTestRunner --corpus sustained.toml --hook-log ... --junit ... --perf-csv ...`
4. Diff against baselines.
5. On merge, commit new baselines as
   `docs/baselines/perf-baseline-<sha>-{chaos,sustained-forward,sustained-edit}.{csv,xml,md}`.

## Known limitations (do NOT re-discover)

| Limitation | Where documented | Impact |
|---|---|---|
| Sub-ms inter-key impossible from user-mode driver — floors at ~3 ms | baseline.md observation #1 | Phase 1 perf goals use 3 ms target, not 1 ms |
| L1 timing only via post-mortem log parse (NexusKey buffers + locks log while running) | commit `2efd180` body | Tool prompts user to stop NexusKey at end of run |
| **Heisenbug**: `_IONBF` log slowed hook enough to mask race-condition bugs entirely | commit `d58bb4e` revert + `2efd180` body | **DO NOT re-enable `_IONBF`** without verifying bugs still reproduce against baseline |
| Uppercase Vietnamese passthrough in `--raw` mode returns false from VkKeyScanW | commit `fd4a14f` body | Use lowercase Vietnamese in TOML `keys` field |
| Uppercase Đ (U+0110) in `text` field also fails — `StrToTelex` passes uppercase through and `VkKeyScanW(Đ) == -1` on US layout | D1 baseline `3459642`, commit body | Edit Vietnamese source paragraphs to avoid uppercase Đ; e.g. `Điều này` → `Việc này`. ASCII uppercase (H, K, M, V, ...) is fine. |
| `std::min({initializer-list})` breaks under MSVC after `Windows.h` (min macro) | D0 fix commit `3459642`, memory `feedback_windows_min_max_macros` | Use `(std::min)(a, (std::min)(b, c))` paren-trick form in headers that may be included after `Windows.h`. |

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
Expected: **281 / 281** tests pass in < 5 ms (was 250 before D0 added EditDistance + CliConvert + new TomlLoader cases).

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
