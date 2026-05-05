# Perf Baseline — Chaos D4 spike @ branch HEAD `a28f1ea` + 3 lock_guard comment-out

**Captured:** 2026-05-04 by Phat (Windows host).
**NexusKey runtime SHA:** `a28f1ea` + uncommitted spike (3 hook-thread `lock_guard` lines commented out at `HookEngine.cpp:647, 677, 705`).
**Test runner SHA:** `a28f1ea` (Sprint 1 D2 commit).
**Anchor:** `perf-baseline-a28f1ea-pre-spike-chaos.md` (D3 capture, pre-spike).
**Purpose:** D4 spike measurement — does removing the three hook-thread mutex acquisitions, with no other change, fix any of the 6 chaos FAILs?

## TL;DR — Outcome B per plan §A decision gate

**Mutex was not the bug source.** Stable FAILs unchanged with byte-identical corruption; stable PASSes unchanged; sustained zero-regression; L1 timing within scheduler noise. Plan instructs: continue D5+ for code-health value, insert D12.5 engine-level fix for at least one stable FAIL.

## Per-case result vs D3 anchor

| # | Case | D3 verdict | D3 actual | D4 verdict | D4 actual | Δ |
|---|---|---|---|---|---|---|
| 1.1 | ghost-hoaf-bs-t | ✅ PASS | hot | ✅ PASS | hot | stable PASS |
| 1.2 | tone-ghost-toans-bs3-i | ❌ FAIL | in | ✅ **PASS** | ti | flip-prone, flipped to PASS |
| 1.3 | escape-bs-aa-b | ✅ PASS | b | ✅ PASS | b | stable PASS |
| 2.1 | x2-space-vieejt-nam | ❌ FAIL | ệiet nam | ❌ FAIL | ệiet nam | **stable FAIL identical** |
| 2.2 | word-boundary-xin-chao-ban | ❌ FAIL | xiàạnhao ban | ❌ FAIL | xinhàoạn ban | composition shift |
| 2.3 | en-vn-transition-hello-vieejt | ❌ FAIL | helệo viet | ❌ FAIL | helệo viet | **stable FAIL identical** |
| 3.3 | engine-stress-truongf | ❌ FAIL | tờương | ❌ FAIL | tờương | **stable FAIL identical** |
| 5.1 | case-tracking-Giar | ✅ PASS | Giả | ✅ PASS | Giả | stable PASS |
| 5.2 | vowel-start-uongs | ✅ PASS | uống | ❌ **FAIL** | ốngg | flip-prone, flipped to FAIL |
| 5.3 | cross-word-bs-vieejt-nam-bs4-s | ❌ FAIL | ết n | ❌ FAIL | ệtết | composition shift |
| 6.1 | autocap-binh-thuongf | ❌ FAIL | bình tươờng | ❌ FAIL | ình ườnggnh | composition shift, **worse** |

Totals: D3 = 4 PASS / 7 FAIL. D4 = 4 PASS / 7 FAIL. Net unchanged.

## L1 timing comparison

| # | Case | D3 mean / p99 / max | D4 mean / p99 / max | Δ p99 |
|---|---|---|---|---|
| 1.1 | ghost-hoaf-bs-t | 13 / 16 / 16 | 13 / 17 / 17 | +1 |
| 1.2 | tone-ghost-toans-bs3-i | 8 / 12 / 12 | 8 / 12 / 12 | 0 |
| 1.3 | escape-bs-aa-b | 9 / 12 / 12 | 9 / 11 / 11 | −1 |
| 2.1 | x2-space-vieejt-nam | 4 / 7 / 7 | 4 / 7 / 7 | 0 |
| 2.2 | word-boundary-xin-chao-ban | 3 / 10 / 10 | 4 / 6 / 6 | −4 |
| 2.3 | en-vn-transition-hello-vieejt | 4 / 10 / 10 | 4 / 8 / 8 | −2 |
| **3.3** | **engine-stress-truongf** | 4 / 12 / 12 | 4 / **17** / **17** | **+5** ← only notable jump |
| 5.1 | case-tracking-Giar | 6 / 10 / 10 | 5 / 10 / 10 | 0 |
| 5.2 | vowel-start-uongs | 7 / 11 / 11 | 7 / 12 / 12 | +1 |
| 5.3 | cross-word-bs-vieejt-nam-bs4-s | 4 / 9 / 9 | 4 / 10 / 10 | +1 |
| 6.1 | autocap-binh-thuongf | 8 / 12 / 12 | 7 / 11 / 11 | −1 |

Worst-case p99 across all chaos cases: 17 ms (1.1, 3.3) — well under `LowLevelHooksTimeout` of 300 ms. Removing the lock did NOT meaningfully change hook callback latency. (Hypothesis the lock was a latency cost was already disproven by locked baseline obs #3; this confirms.)

## Decision gate evaluation

Per `docs/plans/sprint-1-single-owner-refactor.md` §A "Decision gate after D4":

| Outcome | Conditions | This run |
|---|---|---|
| **A** — single-owner fixing race-induced bugs | chaos ≥ 1 flip + 0 regress; sustained error down | NOT met — stable FAILs unchanged, no flip on {2.1, 2.3, 3.3}; sustained unchanged. The 1.2 / 5.2 flips cancel out (one each direction) — heisenbug noise, not a real flip signal. |
| **B** — mutex not the bug source | chaos unchanged, sustained unchanged | **MET.** Continue Sprint 1 for code-health value; insert D12.5 engine-level fix for one stable FAIL. |
| **C** — spike broke something | any PASS regressed OR sustained increased | NOT met — no stable PASS regression, sustained zero-drift. |

**Outcome B confirmed.** Mutex contention is not the cause of the 6 chaos FAILs. The bugs are in `TelexEngine` state-machine logic — consistent with locked chaos baseline observation #3 ("bugs are functional / state machine / composition, not raw latency") and with the v2.1 + EVKey cross-engine evidence in HANDOFF (which proved these bugs are NexusKey regressions, fixable at the TelexEngine layer).

## Implications for Sprint 1

1. **Phase B+ refactor proceeds as planned** — its value is Rule #11 compliance, threading-model code health, and V2 foundation, not "fix the chaos FAILs". Do not expect chaos verdict improvements from D5–D12 alone.

2. **D12.5 (insert) — engine-level single-FAIL fix.** Recommended target: **3.3 `truongwf` → `tờương`**.
   - Discrete logic (diphthong vowel-priority + tone routing) — debuggable as a single state-machine bug.
   - Stable corruption shape every run (deterministic) — easy to verify a fix.
   - L1 p99 only case to jump under spike (+5 ms post comment-out) — may indicate the engine path here is the most fragile to ordering, increasing fix urgency.
   - Alternatives: 2.1 `ệiet nam` (tone routing) or 2.3 `helệo viet` (English protection) are also stable.
   - 5.3 cross-word backspace replay is *not* recommended for D12.5 — composition shape varies run-to-run, harder to verify a fix.

   ### D12.5 finding (revised, 2026-05-05)

   **The "engine state-machine bug" hypothesis was wrong.** Pure-engine
   reproducers (`tests/TelexEngineTest.cpp::D12_5_Truongwf_FullCodaThenWThenTone`
   and `D12_5_Truongw_NoTone`) drive `TypingEngine` directly — no hook,
   no `SendInput`, no threads — and PASS at HEAD: `truongwf` produces
   `trường` and `truongw` produces `trương`. Existing
   `Horn_UO_NPrefix_WithFinal` (`nuowng → nương`) and
   `Horn_UO_AutoTransform_WAfterConsonant` (`huonw → hươn`) already
   covered the retro-horn paths, and the engine handles the
   ng-coda + late-w + tone-f case correctly.

   The chaos 3.3 corruption only emerges through the **hook → engine
   → SendInput-replay** loop at 500 µs inter-key on Windows. The
   shifting corruption shape across captures (`tờương`, `ờnương`)
   consistently *drops leading characters* — a SendInput-replay race
   signature, not a vowel-priority logic bug.

   **Action taken**:
   - The 2 unit tests are kept as **engine regression guards** —
     they lock in that the engine layer cannot regress here, so any
     future 3.3 corruption must be pinned to integration code.
   - D12.5 is closed without an engine code change.
   - Chaos 3.3 verdict flip is deferred to **D8 (MainThreadWorker)** as
     an expected side effect of removing hot-path sync on the hook
     thread. If D8 lands and 3.3 still FAILs byte-identically, the
     remaining cause is in the SendInput-replay sequencing
     (`HookEngine::ProcessKeyDown` output path), and a follow-up
     hook-integration fix is opened against that.
   - The "Plus D12.5 fix" clause in the revised D12 merge gate (item 3
     below) is dropped; D12 acceptance depends on D8 outcome instead.

3. **Sprint 1 D12 merge gate (revised)** — the original "≥ 1 FAIL flip" condition cannot be a merge gate because Outcome B says it won't happen via single-owner alone. Replace with:
   - No regression on stable PASS {1.1, 1.3, 5.1}.
   - No regression on stable FAIL corruption shape on {2.1, 2.3, 3.3} (must remain byte-identical).
   - Sustained zero regression vs D2 (forward 0.41 %, edit 0.00 %).
   - L1 p99 ≤ 18 ms across all chaos cases (1 ms headroom over current worst).
   - ~~**Plus D12.5 fix**: at least one of {2.1, 2.3, 3.3} flips to PASS via TelexEngine bug fix.~~ **Dropped (2026-05-05)** per D12.5 finding above — engine layer is clear; the chaos flip target moves to D8's hook-integration scope and is no longer a hard gate condition for the merge.

4. **6.1 corruption worsened post-spike** (`bình tươờng` → `ình ườnggnh`). This is a flip-prone case — heisenbug noise — but the corruption shape is meaningfully more degraded (lost two leading characters across both syllables). Suggests removing the lock can amplify race amplitude on edge cases even when net verdict is unchanged. Phase B atomic + RCU patterns must be evaluated against this case specifically. Not a Sprint 1 blocker.

## Restoration before merge

The 3 commented-out lines at `HookEngine.cpp:647, 677, 705` are a **measurement-only spike**. Sprint 1 cannot ship with those lines commented:

- Phase B replaces them with atomic acquire/release on primitive flags (D5) and RCU `shared_ptr` for `TypingConfig` (D6).
- D7 audit confirms zero `stateMutex_` acquisitions reachable from the three hook callbacks.
- D11 final mutex type change: `recursive_mutex` → `std::mutex`.
- D13 PR includes the audit script in CI to prevent regression.

If Phase B+ aborts for any reason, restore the three lines via `git revert` of the D4 commit before any merge to master.
