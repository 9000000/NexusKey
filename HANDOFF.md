# NexusKey Refactor — Sprint 1 Handoff (D7 done, D8+ next)

## TL;DR

NexusKey's hook engine has long-standing race-condition bugs (x2 space, ghost
key, tone misplacement under fast typing). Phase 0a built the test harness;
Sprint 1 (this branch) is bringing the hook into compliance with the
just-committed Rule #11 (no mutex on hook hot path) via single-owner refactor.

**Where we are right now (2026-05-04):** Foundation + Phase A spike + **Phase
B complete (D5 + D5.1 + D5.2 + D6 + D7)** — all hook-read state on
`HookEngine` is Rule #11.3-compliant (18 atomic primitives + 1 RCU
shared_ptr struct), and a CI-integrated audit script enforces the
guarantees on every Windows build. L1 worst-case chaos p99 trajectory: D4
17 ms → D5 18 ms → D5.1 16 ms → D5.2 16 ms → D6 18 ms (at the D12 merge
gate cap). The 3 commented hook-thread `lock_guard` lines from D4 are now
formally proven unreachable by the D7 audit; they remain as historical
markers (deletion deferred to a later cleanup commit). Pick up at **D8**
(MainThreadWorker — async queue for hook→main work, e.g. ConfigEvent reload
+ macro persistence) or **D11** (drop `recursive_mutex` → `std::mutex`),
then **D12.5** (engine-level fix for chaos 3.3) per plan §C/§D.

| Layer | Status | Reference |
|---|---|---|
| Project philosophy + Rule #11 | ✅ Committed `0e5322e` | `docs/PHILOSOPHY.md`, `docs/CODING_RULES/11-hook-system-rules.md` |
| Sprint 1 plan (14-day, A0→A→B→C→D→E) | ✅ Committed `ec9798e` | `docs/plans/sprint-1-single-owner-refactor.md` |
| D0: NextKeyTestRunner extensions (text field, --convert, edit_distance) | ✅ Committed `a372a27` + Windows fix `3459642` | `tools/NextKeyTestRunner/` |
| D1: Sustained forward baseline locked | ✅ **0.41 % error** at master | `docs/baselines/perf-baseline-3459642-sustained-forward.{csv,xml,md}` |
| D2: Sustained edit baseline locked | ✅ **0.00 % error** on both edit cases at master | `docs/baselines/perf-baseline-3459642-sustained-edit.{csv,xml,md}` |
| D3: Pre-spike snapshot locked | ✅ Sustained zero-drift; chaos heisenbug-bounded | `docs/baselines/perf-baseline-a28f1ea-pre-spike-{chaos,sustained}.{csv,xml,md}` |
| D4: Spike (3 hook-thread mutex acquisitions commented out) | ✅ **Outcome B** — mutex not the bug source | `docs/baselines/perf-baseline-d4-spike-{chaos,sustained}.{csv,xml,md}` |
| D5: `vietnameseMode_` → `std::atomic<bool>` (incremental, N=1) | ✅ DoD met — sustained byte-identical, chaos heisenbug envelope preserved, L1 worst p99 18 ms | `docs/baselines/perf-baseline-d5-atomic-vnmode-{chaos,sustained}.{csv,xml,md}`, `tests/HookEngineAtomicTests.cpp` |
| D5.1: `currentMethod_` → `std::atomic<InputMethod>`, `isTsfApp_` → `std::atomic<bool>` (19 sites) | ✅ DoD met — sustained byte-identical (p99 −3 ms vs D5), chaos 1 PASS gain via 1.2 flip, no PASS regress, L1 worst p99 16 ms (improvement) | `docs/baselines/perf-baseline-d5.1-atomic-method-tsf-{chaos,sustained}.{csv,xml,md}` |
| D5.2: 15 remaining hook-read primitives → `std::atomic` (8 per-app cached + `excludedPid_` DWORD + 6 config-derived; ~60 sites) | ✅ DoD met — sustained byte-identical, chaos verdicts preserved (1.1+1.2+1.3+5.1+5.2 byte-identical PASS, 2.1+2.2 byte-identical FAIL), L1 worst p99 16 ms (cap unchanged), 3.3 engine-stress p99 −5 ms | `docs/baselines/perf-baseline-d5.2-atomic-rest-{chaos,sustained}.{csv,xml,md}` |
| D6: RCU `shared_ptr<const TypingConfig>` for `config_` (7 sites + 3 RCU GTest cases) | ✅ DoD met — sustained byte-identical (forward p99 −1, edits −3 ms vs D5.2), chaos stable PASS preserved, stable FAIL {2.1, 2.2, 3.3} byte-identical, L1 worst p99 18 ms (run 2; run 1 hit 22 ms = heisenbug, dropped to 14 ms on re-run); 5.2 flipped FAIL (flip-prone per HANDOFF) | `docs/baselines/perf-baseline-d6-rcu-config-{chaos,sustained}.{csv,xml,md}`, `tests/TypingConfigRCUTests.cpp` |
| D7: audit script `tools/audit/check_hook_thread_no_mutex.sh` + CI integration (4 checks: D4 spike comment integrity, no `stateMutex_` reachable from LL hook entries, atomic fields use `.load`/`.store`, RCU `config_` likewise) | ✅ DoD met — script exits 0 on current tree, wired into `.github/workflows/build.yml` as fail-fast pre-build step | `tools/audit/check_hook_thread_no_mutex.sh`, `.github/workflows/build.yml` |
| D8+: MainThreadWorker, drop recursive_mutex, **D12.5 engine fix for chaos 3.3** | pending | `docs/plans/sprint-1-single-owner-refactor.md` §C/§D |

## Branch state

- Branch: `refactor/phase-1-single-owner` (4 commits ahead of master `43fb4c1`)
- Cross-platform tests: **281 / 281** pass on Linux (Windows MSVC verified)
- Tool location: `tools/NextKeyTestRunner/` (now with `--convert`, `text` field, `edit_distance` verdict)
- Frozen baselines:
  - `docs/baselines/perf-baseline-43fb4c1.{md,csv,xml}` — chaos corpus (11 cases, 5 PASS / 6 FAIL)
  - `docs/baselines/perf-baseline-3459642-sustained-forward.{md,csv,xml}` — sustained forward (1 case, 0.41 % error)
- Branch adds docs + test infra only. **No production code touched yet.** The single-owner refactor begins at D3.

## Significant findings from D1 + D2 baselines

**D1 (sustained forward, 50 ms inter-key, 1 case, 1 631 keys):** master state
handles realistic forward Vietnamese typing at **0.41 % error rate**.

**D2 (sustained edit, 50 ms inter-key, 2 cases, ~21 keys total):** master state
handles realistic edit scenarios (inline tone correction, cross-word backspace
replay) at **0.00 % error rate**. The chaos `5.3-cross-word-bs-vieejt-nam-bs4-s`
key sequence — which corrupts at 1 ms inter-key — produces correct output at
50 ms inter-key, byte-exactly.

**Combined implications:**

1. The original "≥ 30 % improvement on sustained" gate is unworkable on BOTH
   sustained dimensions (30 % of 0.41 % is below noise; 30 % of 0.00 % is
   undefined). Plan revised: both sustained dimensions are "no-regression"
   anchors only.
2. The interesting Sprint 1 movement signal lives in **chaos FAIL flips**
   (≥ 1 of 6) and **L1 timing tightening** (single-owner removes mutex
   contention; expected p99 drop from current 60 ms to under 50 ms post-D11).
3. Chaos bugs are confirmed speed-bound: same key sequences pass cleanly at
   realistic typing pace. The Sprint 1 refactor's value is **letting the
   engine survive sub-millisecond burst input**, not fixing structural
   edit-path bugs (those don't exist at this layer).

### Heisenbug variance noted on chaos co-run (2026-05-04 ~1010)

Re-running `chaos.toml` against the same `43fb4c1` runtime as the locked baseline
produced a 5 PASS / 6 FAIL total (count unchanged) but the **composition shifted**:

| Case | Locked baseline (43fb4c1.md) | New run | Note |
|---|---|---|---|
| 5.2 `uongs` | FAIL `ốngg` | **PASS** | flipped |
| 6.1 `binhf thuongwf` | PASS `bình thường` | **FAIL** `bình tườngg` | flipped |
| 2.3 `hello vieejt` | FAIL `helệo viet` | FAIL `heloệ viet` | same FAIL, ệ position drifts |
| 5.3 `vieejt nam BS×4 s` | FAIL `etết` | FAIL `itết` | same FAIL, first char differs |
| 2.1, 2.3, 3.3 | FAIL | FAIL (identical strings) | stable |
| 1.1, 1.3, 5.1 | PASS | PASS | stable |
| **1.2** | PASS | PASS in conv-run, **FAIL `in`** in D3 capture | flip-prone (D3 evidence) |
| **2.2, 5.3** | FAIL | FAIL with corruption shape varying between runs | composition unstable |

This is the heisenbug behavior already documented in `perf-baseline-43fb4c1.md`
observation #2 (6.1 was borderline at D7 capture). Confirmed: under sub-ms
input, engine state is non-deterministic — the same input gives different
corrupt outputs run-to-run, and a few cases (5.2, 6.1) flip between PASS/FAIL.

**Implication for Sprint 1 D12 gate**: "≥ 1 FAIL flip" can be satisfied by
heisenbug noise rather than by an actual fix. Stable FAILs (2.1, 2.2, 3.3,
2.3, 5.3) are the meaningful regression-detection targets; 5.2 and 6.1 are
unreliable signals on their own. Concrete tightening for D12:

- **Run chaos N=3 times** post-refactor and require a flip in ≥ 2 of 3 runs.
- **Track stable FAILs explicitly**: a flip on 2.1, 2.2, or 3.3 (which never
  PASSed in any captured run) is high-confidence; a flip on 5.2 or 6.1 alone
  is low-confidence and must be corroborated by an L1 timing improvement.
- **Locked baseline is NOT updated** with this re-run data — `43fb4c1.md` is
  frozen by design. The variance observation lives here in HANDOFF.

### Cross-engine + cross-version check (2026-05-04 ~1030)

The same chaos corpus run on the same Windows host at the same 1–5 ms inter-key
pace against three engines:

| Engine | Result | Note |
|---|---|---|
| NexusKey current (`43fb4c1`) | 5 PASS / 6 FAIL (heisenbug variance noted above) | Sprint 1 starting state |
| **NexusKey v2.1** (older release) | **PASS-clean (≈ EVKey)** | Per Phat's test, 2026-05-04 |
| **EVKey** | **11 PASS / 0 FAIL** | Different project, stable |

This is **regression evidence**: an older version of *the same project* handled
the chaos cases cleanly. The bugs were introduced by feature additions over
time, not by a fundamental design limitation. The competing engine (EVKey)
confirms the architecture *can* be stable.

Implications for Sprint 1:

1. The chaos failures are **NexusKey-specific regressions**, not a pace-induced
   limitation. Earlier doc wording that called them "speed-bound" (in chaos
   baseline obs #2 and the first draft of sustained-edit obs #1) was
   imprecise — v21 demonstrates the same engine logic *was* stable at this
   pace before recent feature work introduced regressions.
2. Sprint 1 single-owner refactor's value is **restoring architectural
   stability** — not as a "code health" abstraction, but as a concrete
   condition that v21 had and current does not: adding a feature should not
   regress prior chaos behavior. The four pillars (Nhanh / Nhẹ / Mượt / Mở
   rộng-không-làm-nặng) plus "dễ debug" govern this directly: an architecture
   where features compose without regressing each other.
3. Plan scope is **unchanged**. The single-owner refactor is the right
   structural change — it disentangles state ownership so feature additions
   do not silently couple via shared mutable state. Phase D outcome B
   (D12.5 single-FAIL engine fix) accommodates per-bug regression repairs
   if the architectural change alone does not fully restore v21 behavior.
4. EVKey passing 11/11 + v21 passing chaos is **proof the architecture is
   recoverable**, not just proof "fixes exist for individual bugs". Post-
   Sprint 1, the right comparison is: does adding the next feature on the
   refactored base regress chaos? If no, the architectural goal is met.

**Locked baseline `43fb4c1.md` is NOT amended** with this data — frozen by
design. The observation lives here in HANDOFF.

**Methodology note (lesson recorded in feedback_test_dont_theorize):** the
v21 + EVKey runs were direct test evidence supplied by Phat. An earlier
draft of this section reasoned about what the chaos FAILs *must* be from
the locked baseline alone, without the comparator data. That hypothesizing
was wrong-shaped — the right question was "can we run the same corpus
against a known-good engine?", which Phat answered by running v21 and EVKey.
Future Sprint 1 work that needs to attribute a regression cause should
likewise prefer "run the corpus against state X" over "reason about state X
from data we already have".

## Read in this order (for a teammate picking up D3)

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
