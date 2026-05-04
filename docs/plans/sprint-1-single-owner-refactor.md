# Sprint 1 Plan — Single-Owner Hook Engine Refactor

> **Branch:** `refactor/phase-1-single-owner`
> **Foundation:** [`docs/PHILOSOPHY.md`](../PHILOSOPHY.md), [`docs/CODING_RULES/11-hook-system-rules.md`](../CODING_RULES/11-hook-system-rules.md)
> **Gate (revised after D1 baseline 2026-05-04):**
> - Chaos corpus: ≥ 5 PASS, ≥ 1 FAIL flip vs `perf-baseline-43fb4c1`
> - Hook callback p99: ≤ 16 ms (chaos)
> - **Sustained 200wpm forward test:** no regression vs D1 baseline (`0.41 %` error). The original "≥ 30 % improvement" wording is removed — D1 captured master at 0.41 %, so 30 % of that is below measurement noise. See `docs/baselines/perf-baseline-3459642-sustained-forward.md` Observation #3.
> - **Sustained edit (typo + cross-word) test:** no regression vs D2 baseline; ≥ 30 % improvement is target (D2 baseline TBD).
> **Frozen test corpora:**
> - `tools/NextKeyTestRunner/corpus/chaos.toml` (11 cases, current: 5 PASS / 6 FAIL)
> - `tools/NextKeyTestRunner/corpus/sustained.toml` (1 case `forward-200wpm-sustained`, locked in D1; D2 will append edit cases)
> **Foundation commit (already landed on this branch):** `0e5322e` (PHILOSOPHY + Rule #11 + PROJECT_MAP read-first)

---

## Why this plan exists

The pre-mortem session 2026-05-04 surfaced two classes of problem:

**(1) Spec assumptions never verified.** Phase 6.3 SCAMPER (brainstorm 2026-05-03) assumed:
1. `recursive_mutex` is the root cause of the 6 chaos FAILs.
2. Atomic flag publication is "obvious" and need not be designed in detail.
3. Main thread does not touch engine state.

All three were unverified. Rule #11 (committed in `0e5322e`) resolved (2) and (3) by prescribing concrete patterns (atomic acquire/release, RCU `shared_ptr`, two-phase classify-then-apply). Assumption (1) requires a **diagnostic spike** before the full refactor commits — Phase A.

**(2) Test corpus is binary, not continuous.** The 11 chaos cases give pass/fail per case but cannot answer "does typing FEEL smoother?" — the user-perceptible smoothness goal of Pillar #3 (Mượt). A 200wpm sustained test with edit-distance verdict was specified in brainstorm Phase 3 line 234 but Phase 0a did not ship it. Phase A0 closes this gap before any refactor work.

Every phase below is governed by the three pre-code questions (PHILOSOPHY §3): right place? impact? better way? — answered in the design moment, then test-first, then implement.

---

## Code reconnaissance (current state on this branch)

**`recursive_mutex stateMutex_`** declared at `src/app/system/HookEngine.h:305`.

**Hook-thread acquisitions (Rule #11.3 violations — Sprint 1 must remove):**

| File:line | Function | Why it's on hook thread |
|---|---|---|
| `HookEngine.cpp:647` | `LowLevelKeyboardProc` | LL keyboard hook callback — every keystroke |
| `HookEngine.cpp:677` | `WinEventProc` | Foreground-window event — runs on hook thread per existing comment |
| `HookEngine.cpp:705` | `LowLevelMouseProc` | LL mouse hook callback — every left-click |

**Main-thread acquisitions (OK in principle — writers; will migrate `recursive_mutex` → `std::mutex` at the end):**

| File:line | Function |
|---|---|
| `HookEngine.cpp:80` | `CommitPending` |
| `HookEngine.cpp:87` | `ApplyConfig` |
| `HookEngine.cpp:330` | (focus / app-detect path) |
| `HookEngine.cpp:382` | (focus / app-detect path) |
| `HookEngine.cpp:411` | (config event path) |
| `HookEngine.cpp:476` | `CheckConfigEvent` |
| `HookEngine.cpp:2313` | (deep state mutator) |

**Existing precedent:** `src/app/system/HotkeyManager.h:62` already uses `std::mutex slotsMutex_` with documented "hook thread reads, main thread writes" pattern. Sprint 1 generalizes this pattern.

**Existing telex tooling:** `tools/NextKeyTestRunner/src/Telex.h` provides `StrToTelex(input) -> u16string` (header-only, 67-entry verified table at lines 25-44, golden-tested in `tests/TelexTest.cpp`). Phase A0 reuses this — never hand-encode.

---

## Phase A0 — Sustained test infrastructure + baselines (D0–D2)

**Goal:** Build the measurement infrastructure that captures user-perceptible smoothness, then lock baselines on master state. Without this, the Sprint cannot prove "smoother typing" — only "FAIL count went down."

### D0 — Test runner extensions

- **Q1 (right place):** All changes inside `tools/NextKeyTestRunner/src/` — runner is the right home for test infra. No production code touched.
- **Q2 (impact):** Adds new corpus fields and verdict mode; existing 11 chaos cases continue to use `keys` + `verdict_mode = "exact"` (default), so they remain identical. No SharedState change. No DLL impact.
- **Q3 (better way):** Considered a separate Python harness (out-of-process) for sustained tests; rejected — splits the test surface, doubles the dependency footprint, and the existing C++ runner already drives SendInput correctly. Single tool, multiple modes, is the cheaper extension.
- **Test-first:** unit tests for the three additions:
  - `tests/CorpusTextFieldTests.cpp` — `text` field auto-converts via `StrToTelex`, matches manual `keys` for known cases.
  - `tests/EditDistanceTests.cpp` — Levenshtein on representative pairs.
  - `tests/CliConvertTests.cpp` — `./NextKeyTestRunner --convert "việt"` outputs `vieejt`.
- **Implementation (~80 LOC total):**
  - **Corpus parser:** accept `text` field (alternative to `keys`). When `text` is present, runner calls `Telex::StrToTelex(text)` to derive raw key sequence.
  - **CLI mode:** add `--convert <quoted-text>` flag. Reads stdin or argument, runs `StrToTelex`, prints raw telex to stdout. Exits 0.
  - **Verdict mode:** add `verdict_mode = "edit_distance"` corpus field with `threshold_pct = N.N`. Runner computes Levenshtein(actual, expected), reports `error_chars / expected_chars * 100 %`, PASS if `(100 - err%) >= threshold_pct`.
  - **Reporter:** CSV columns add `mode`, `error_chars`, `error_pct`. JUnit XML adds `<failure>` tag with edit-distance details.
- **DoD:** all 3 unit-test files pass on Linux. Manual: `./NextKeyTestRunner --convert "có dấu việt"` outputs `cos daasu vieejt`.
- **Commit:** `Sprint 1 D0: NextKeyTestRunner — text field, --convert CLI, edit_distance verdict`

### D1 — Encode + run forward-only sustained baseline ✅ DONE

**Status:** Completed 2026-05-04. Baseline locked at `0.41 %` error.

- **Q1:** New corpus file `tools/NextKeyTestRunner/corpus/sustained.toml`. Source paragraph reused from `tools/NextKeyTestRunner/tests/TelexGolden.h:218` (already golden-tested vs vn-str). Two `Điều này` sentence-openers rewritten to `Việc này` because uppercase Đ (U+0110) is not typeable via `VkKeyScanW` on a US layout — pattern documented in baseline.md for D2.
- **Q2:** Adds one new corpus file. No code change.
- **Q3:** Considered multiple smaller cases vs one long case; chose one long case to capture sustained engine state under realistic typing pressure. A 200-word run took 86.7 s at 50 ms inter-key — fast enough to iterate.
- **Test-first:** the corpus case itself is the test artifact. Pre-condition: D0 must pass (and did).
- **Outcome (locked baseline):**
  - Verdict: PASS at threshold 99 %.
  - Error rate: **0.41 %** (5 chars wrong out of ~1500 expected).
  - L1 hook timing: mean 52 ms, p99 60 ms, max 65 ms (over 1 631 keydowns).
  - Wall-clock: 86.743 s.
  - Files: `docs/baselines/perf-baseline-3459642-sustained-forward.{csv,xml,md}`.
- **Significant finding for the plan:** Forward typing at realistic pace is already near-perfect at master state. The "≥ 30 % improvement" gate was removed for this dimension (30 % of 0.41 % = 0.12 %, below measurement noise). Forward sustained is now a **no-regression** anchor; the interesting improvement signal will live in chaos FAIL flips and (after D2) the sustained-edit baseline.
- **Commit:** `Sprint 1 D1: lock forward-200wpm sustained baseline (0.41 % error, 5 chars)`

### D2 — Encode + verify cross-word edit cases ✅ DONE

**Status:** Completed 2026-05-04. Baseline locked at **0.00 % error** on both edit cases. Files: `docs/baselines/perf-baseline-3459642-sustained-edit.{csv,xml,md}`. Cases: `edit-1-inline-tone-fix-vieet-bs-jt`, `edit-2-cross-word-bs-vieejt-nam-bs4-s`. Mixed-edit-session case skipped (optional per plan; both committed cases already give 0 % error, so a third case would not add baseline signal).

**Cross-engine observation (2026-05-04):** EVKey running the same chaos corpus on the same host at the same 1 ms inter-key pace produced 11 / 11 PASS. Confirms chaos FAILs are NexusKey TelexEngine state-machine bugs, not pace-induced — see HANDOFF "EVKey cross-engine check" section. Sprint 1 scope unchanged (Rule #11 + code health); engine bug fixes tracked under Phase D outcome B (D12.5).

- **Q1:** Add 2-3 cases to `sustained.toml` covering:
  - `inline-typo-correction` — small typo in middle of a word, BS, retype.
  - `cross-word-edit-fix` — type a word wrong, BS past committed text, retype (the user's `việt có dấu → BS×7 → viết có dấu` example, but with engine-verified key counts).
  - (optional) `mixed-edit-session` — 50-word forward + 3 typos + 1 cross-word edit.
- **Q2:** Adds corpus cases only. No code change.
- **Q3:** Considered hand-constructing telex sequences from memory; **explicitly rejected** (memory: `feedback_never_hand_encode_telex` — Claude makes errors when fluent-generating telex). Each scenario is built from `text:` segments (auto-converted via `StrToTelex`) plus explicit `\b` interjections. Engine state after BS is **verified by running the actual engine** before locking the expected output.
- **Test-first:** the corpus case is the test artifact. Verification = run on actual engine, observe output, set `expected` to that.
- **Implementation procedure (per case):**
  1. Draft scenario in `sustained.toml` with placeholder `expected = "<TBD>"`.
  2. Build a `text_with_edits` mini-DSL: lines are either `{text: ...}` (auto-converted) or `{bs: N}` (raw BS).
  3. Cite kTable line numbers in comments for any hand-written segment.
  4. Run case against current branch's NexusKey build. Capture actual output.
  5. If output is what a typing user would expect (semantic check, not exact match), set `expected = <actual>` and lock.
  6. If output diverges from typing user's expectation, the engine has a bug — that's a chaos case candidate, not a sustained-baseline case. Park it, pick a different scenario, repeat.
- **DoD:**
  - 2-3 new cases in `sustained.toml` with locked `expected`.
  - Baseline `docs/baselines/perf-baseline-<sha>-sustained-edit.csv` + `.xml` + `.md` committed.
  - Each case has a comment citing kTable lines for hand-written portions.
- **Commit:** `Sprint 1 D2: lock sustained edit baseline (typo + cross-word, <N> cases)`

---

## Phase A — Diagnostic Spike (D3–D4)

**Goal:** Answer one question before committing the full refactor — *"Does removing the three hook-thread mutex acquisitions, with no other change, fix any of the 6 chaos FAILs and reduce the sustained error rate?"*

### D3 — Lock pre-spike snapshot baseline

- **Q1:** New artifacts only — capture chaos+sustained outputs on this exact commit.
- **Q2:** None. Pure measurement.
- **Q3:** Could trust the D1/D2 baselines and the locked `perf-baseline-43fb4c1`, but capturing once more confirms zero drift from D2 to D3 (no rogue change since A0).
- **Test-first:** existing chaos corpus + new sustained corpus.
- **Implementation:** build current branch (no code change), run both corpora, commit `perf-baseline-<sha>-pre-spike-{chaos,sustained}.{csv,xml,md}`.
- **DoD:** chaos result identical to `perf-baseline-43fb4c1` (5 PASS / 6 FAIL, p99 ±2 ms). Sustained result identical to D1/D2. If any drift, abort and investigate.
- **Commit:** `Sprint 1 D3: lock pre-spike snapshot (chaos + sustained), zero drift`

### D4 — Spike: minimal mutex drop

- **Q1:** Three lines only — `HookEngine.cpp:647, 677, 705`. No headers, no new files.
- **Q2:** Hook callbacks read state without a lock; will race with main-thread writers (`OnFocusChanged`, `ApplyConfig`, `ToggleVietnameseMode`, `Reload*`). Torn-read risk knowingly accepted *for the spike only* to isolate the question. Phase B fixes the races properly.
- **Q3:** Better than this spike = full RCU + atomic publication (Phase B). The spike's purpose is **not** to ship — it is to **measure**. A minimal change is the cleanest probe.
- **Test-first:** chaos corpus + sustained corpus results ARE the answer.
- **Implementation:** comment out (do not delete) the three `lock_guard` lines, leaving comments referencing this plan. Build. Run both corpora.
- **DoD:** numerical answer captured to `docs/baselines/perf-baseline-<sha>-spike-{chaos,sustained}.{csv,xml,md}`:
  - count of chaos FAILs flipped to PASS,
  - count of chaos PASSes regressed to FAIL,
  - sustained forward error rate delta vs D1,
  - sustained edit error rate delta vs D2,
  - p99 delta vs D3 baseline.
- **Commit:** `Sprint 1 D4 SPIKE: drop 3 hook-thread mutex acquisitions, capture chaos + sustained delta`

### Decision gate after D4

| Outcome | Meaning | Phase B response |
|---|---|---|
| **A** — chaos: ≥ 1 flip, 0 regress; sustained: error rate down | Single-owner is genuinely fixing race-induced bugs at both granularities | Continue D5 with confidence; expect more flips after full RCU |
| **B** — chaos unchanged, sustained unchanged | Mutex was not the bug source. Refactor is cleanup (still satisfies Pillar #1 / Rule #11) but the merge gate's "≥ 1 FAIL flip" + "30 % sustained reduction" is NOT met by single-owner alone | Continue D5 — Sprint 1 still cleans up Rule #11 violations — but add **D12.5** engine-level fix for the cheapest single FAIL (likely 5.2 `uống` vowel routing or 3.3 `trường`). Sustained gate may need to soften to "no regression." |
| **C** — any chaos PASS regressed OR sustained error rate increased | A lock was protecting something the spike broke | Pause. Identify which composition manipulation broke. Phase B scope expands to cover that field's publication |

The spike outcome is committed alongside the corpus output; future readers of this branch can see the experiment's data immediately.

---

## Phase B — Foundation refactor (D5–D7)

**Goal:** Replace the locked state reads with atomic + RCU patterns from Rule #11.3.

### D5 — Atomic flags for primitive state

- **Targets:** primitive fields that the hook callback reads — `vietnameseMode_`, `currentMode_`, profile/exclude flags. Audit list compiled from the protected critical sections at `HookEngine.cpp:647`/`677`/`705`.
- **Q1:** `HookEngine.h` field declarations + `HookEngine.cpp` writer/reader sites. No new file.
- **Q2:** Fields change layout from "plain + guarded" to `std::atomic<T>`. Writers must explicitly `.store(value, std::memory_order_release)`; readers use `.load(std::memory_order_acquire)`. No SharedState change → no Rule #5 versioning bump.
- **Q3:** Considered seqlock, rejected — overkill for primitive scalars. Rule #11.3 explicitly prescribes atomic acquire/release here.
- **Test-first:** new GTest `tests/HookEngineAtomicTests.cpp` asserts main-write / hook-read visibility for each migrated field.
- **DoD:** GTest passes on Linux; chaos + sustained on Windows show same delta as D4 (no new regressions).
- **Commit:** `Sprint 1 D5: migrate <N> primitive flags to std::atomic with acquire/release`

### D6 — RCU `shared_ptr` for `TypingConfig`

- **Targets:** `TypingConfig` and any other complex struct that hook callback reads.
- **Q1:** `HookEngine.h` (member type change) + `HookEngine.cpp` (writer/reader sites) + `TypingConfig.h` (mark const-readable).
- **Q2:** Lifetime semantics change. Writer holds new `shared_ptr` until `atomic_store` succeeds, then drops. Hook reader's `atomic_load` returns its own `shared_ptr` whose lifetime extends past the publish. Old config object is destroyed when last reader drops it. No allocation on hook hot path (only `shared_ptr` ref-count bump, ~5 ns).
- **Q3:** Considered seqlock for `TypingConfig` (precedent: `HookContextAnchor`), rejected — `TypingConfig` is in-process, lifetime can use `shared_ptr`. Seqlock is reserved for cross-process structs that cannot use heap pointers.
- **Test-first:** `tests/TypingConfigRCUTests.cpp` — concurrent reader during writer swap, assert no torn read, assert old config kept alive while reader holds it.
- **DoD:** GTest passes; chaos + sustained delta unchanged from D5.
- **Commit:** `Sprint 1 D6: publish TypingConfig via RCU shared_ptr`

### D7 — Audit hook-thread reads, prove zero `stateMutex_` acquisitions on hot path

- **Q1:** Audit script + plan annotation only. No code change unless audit finds residue.
- **Q2:** Static guarantee added; no runtime change.
- **Q3:** Could rely on review, but a grep-based static check lives in CI and prevents regression.
- **Test-first:** add a CI/build-time check: `tools/audit/check_hook_thread_no_mutex.sh` greps for `stateMutex_` inside the call graph reachable from `LowLevelKeyboardProc | LowLevelMouseProc | WinEventProc` and fails the build if it finds anything.
- **DoD:** audit script returns zero hits; runs on every CI build.
- **Commit:** `Sprint 1 D7: enforce zero hook-thread mutex acquisitions via static audit`

---

## Phase C — MainThreadWorker (D8–D10)

**Goal:** Single home for non-hot-path work (Pillar #2 — Nhẹ: one thread, three responsibilities).

### D8 — Worker scaffolding

- **New files:** `src/app/system/MainThreadWorker.h` + `.cpp`.
- **Q1:** New module under `src/app/system/` (process-local, alongside `HookEngine`). Not in `core/` because worker is process-specific.
- **Q2:** +1 thread per process. Removed at end of phase: existing `SetTimer(200 ms)` for CJK poll (memory: `project_cjk_layout_detection`). Net thread count is +0 by end of Sprint 1 if CJK timer migrates here.
- **Q3:** Considered Win32 thread pool (`SubmitThreadpoolWork`); rejected — explicit thread is simpler and lifetime is clearer.
- **Test-first:** `tests/MainThreadWorkerTests.cpp` — start, idle wait, clean shutdown.
- **Implementation:** empty thread that calls `WaitForMultipleObjects([shutdownEvent], INFINITE)` and returns when signaled.
- **DoD:** test passes; start/stop completes in < 100 ms.
- **Commit:** `Sprint 1 D8: MainThreadWorker scaffolding (start/stop only)`

### D9 — Wire config-event channel

- **Q1:** `MainThreadWorker.cpp` accepts a config-event handle; existing `ConfigEvent` plumbing in `src/core/config/ConfigEvent.cpp` continues to be the named-event source.
- **Q2:** Replaces (does not duplicate) any existing config-poll code in main message loop. Audit `src/app/main.cpp` for prior config polling.
- **Q3:** Considered `WaitOnAddress` directly on `configEpoch_` atomic; rejected for now because Win32 named event integrates with the existing `ConfigEvent` API. `WaitOnAddress` deferred to V2 (memory: brainstorm Phase 6.3 SCAMPER §S).
- **Test-first:** test simulates config change → asserts worker wakes and calls `ApplyConfig` within 50 ms.
- **DoD:** test passes; manually open Settings → change mode → corpus picks up new mode without hook stutter.
- **Commit:** `Sprint 1 D9: MainThreadWorker handles config-changed event`

### D10 — Wire heartbeat tick + CJK layout poll

- **Q1:** Single `WaitForMultipleObjects([shutdownEvent, configEvent], 200 ms)` in worker. `WAIT_TIMEOUT` branch handles heartbeat + CJK poll.
- **Q2:** Removes the existing 200 ms `SetTimer` for CJK detection. Removes any separate heartbeat thread.
- **Q3:** Considered separate timers; rejected — Pillar #2 says one thread, multiple responsibilities, when no real-time conflict.
- **Test-first:** simulate stale `lastHookAlive_` → assert heartbeat detects within 3 s and triggers reinstall path.
- **DoD:** test passes; no `SetTimer(200 ms)` remains in `src/app/main.cpp`.
- **Commit:** `Sprint 1 D10: MainThreadWorker handles heartbeat + CJK layout poll`

---

## Phase D — Drop `recursive_mutex` (D11–D12)

### D11 — `recursive_mutex` → `std::mutex`

- **Q1:** `HookEngine.h:305` declaration only; all writer sites already use `lock_guard`/`unique_lock` which work with both.
- **Q2:** Build will fail at any site that re-acquires the lock recursively. Each failure is a real bug surfaced by the type system — fix by hoisting lock acquisition to the outer scope or by extracting the inner method to not require the lock.
- **Q3:** Could leave as `recursive_mutex` — but it would mask future recursion bugs. Pillar #2 (Nhẹ) prefers the smaller primitive when it works.
- **Test-first:** existing GTest suite + chaos + sustained corpora.
- **DoD:** clean build, all 250 GTest pass.
- **Commit:** `Sprint 1 D11: replace recursive_mutex with std::mutex on main-thread writers`

### D12 — Full chaos + sustained validation against gate

- Run `NextKeyTestRunner --corpus chaos.toml` and `--corpus sustained.toml` against the post-D11 build.
- **DoD (HANDOFF gate, revised after D1):**
  - Chaos: ≥ 5 PASS (no regression vs `perf-baseline-43fb4c1`)
  - Chaos: ≥ 1 FAIL flipped to PASS
  - Sustained forward: ≤ 0.41 % error (no regression vs D1 baseline `3459642`)
  - Sustained edit: no regression vs D2 baseline (≥ 30 % improvement is target — TBD when D2 baseline exists)
  - Hook callback p99: ≤ 16 ms (chaos burst input)
  - Hook L1 sustained: mean ≤ 53 ms / p99 ≤ 62 ms / max ≤ 67 ms (no regression vs D1)
- If gate not met:
  - Outcome A path (D4): identify which Phase B/C migration introduced the regression; fix and re-run.
  - Outcome B path (D4): insert **D12.5** — engine-level fix for the cheapest single FAIL (likely `5.2 uống` per failure-categorization table in `perf-baseline-43fb4c1.md`). Re-run corpora.
- **Commit:** `Sprint 1 D12: full corpora run, gate <PASS|FAIL> details`

---

## Phase E — Baseline + PR prep (D13)

- Capture new `perf-baseline-<final-sha>-{chaos,sustained}.{csv,xml,md}` to `docs/baselines/` (replaces, does not overwrite, the locked `perf-baseline-43fb4c1`).
- Update `HANDOFF.md` with:
  - Sprint 1 outcome (chaos FAILs flipped, sustained error rate movement, p99 movement, any deferred items),
  - Sprint 2 spec (T3 IOutputInjector — already drafted in brainstorm Phase 7.4 row 2).
- Open PR. Title: `Sprint 1: single-owner hook engine refactor`. Body links: foundation commit `0e5322e`, this plan, the per-phase commits, the spike outcome (D4), the new baselines.
- **DoD:** PR open, CI green, gate met.
- **Commit:** `Sprint 1 D13: capture new baselines, update HANDOFF for Sprint 2`

---

## Anti-abandon rules (carried from Phase 0a)

- Daily commit (10 LOC counts).
- User-reported bugs unrelated to Sprint 1 → issue tracker, **do not** fix inline.
- Crash / data-loss bugs are the exception — pause Sprint, fix, return.
- Do not merge a half-baked branch into master. Ship full sprints.

---

## Open items the plan deliberately defers

| Item | Why deferred | Where revisited |
|---|---|---|
| `WaitOnAddress` for `configEpoch` | Existing `ConfigEvent` named-event suffices for D9 | V2 backlog (brainstorm 2026-05-03 Phase 6.3 §S) |
| Hook fast-path foreground detection | Profile switch latency not yet a measured pain point | V2 (brainstorm Phase 6.3 §R sub-reverse) |
| ETW tracing | Post-mortem log parse works for now | V2 |
| `IOutputInjector` factory | Sprint 2 scope | brainstorm Phase 7.4 row 2 |
| `noexcept` enforcement / SEH = WER | Sprint 3 scope | brainstorm Phase 7.4 row 3 |
| Mixed-edit-session sustained case (50 forward + typo + cross-word) | Optional in D2; encode if D2 leaves slack | After Sprint 1 if not done |

---

## How a reader picks this up cold

1. Read `docs/PHILOSOPHY.md` (10 minutes).
2. Read `docs/CODING_RULES/11-hook-system-rules.md` (5 minutes).
3. Read this file (10 minutes).
4. Run `git log --oneline 43fb4c1..HEAD` and read the body of every commit on this branch.
5. Run `./build-linux/tests/NextKeyTests` to confirm 250/250 still pass.
6. Pick up at the next `Sprint 1 D<n>` entry above.
