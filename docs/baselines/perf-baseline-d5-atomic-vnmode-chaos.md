# Perf Baseline — Chaos D5 atomic `vietnameseMode_` migration

**Captured:** 2026-05-04 by Phat (Windows host).
**NexusKey runtime SHA:** `f1f514b` + uncommitted D5 (`vietnameseMode_` migrated to `std::atomic<bool>` with acquire/release at 13 sites in `HookEngine.cpp` + accessor in `HookEngine.h`). The 3 hook-thread `lock_guard` lines from D4 remain commented.
**Test runner SHA:** `f1f514b`.
**Anchor:** `perf-baseline-d4-spike-chaos.md` (D4 spike capture).
**Purpose:** D5 incremental migration — does replacing `vietnameseMode_` plain read with `std::atomic` acquire/release introduce any new regression vs the D4 spike outcome?

## TL;DR — DoD met

**No new regression.** Stable PASS preserved on {1.1, 1.3, 5.1}; stable FAIL preserved on {2.1, 2.3, 3.3} (verdict-identical, 2.3 also byte-identical, 2.1 + 3.3 within heisenbug envelope); flip-prone {1.2, 5.2} swung within their documented envelope; sustained zero-drift; L1 p99 worst case 18 ms vs D4 17 ms (+1 ms scheduler noise). Atomic load/store on x86 is functionally identical to plain load/store for single-byte primitives at this access pattern, so this result corroborates D4 Outcome B (mutex / memory-ordering is not the bug source).

## Per-case result vs D4 anchor

| # | Case | D4 verdict | D4 actual | D5 verdict | D5 actual | Δ |
|---|---|---|---|---|---|---|
| 1.1 | ghost-hoaf-bs-t | ✅ PASS | hot | ✅ PASS | hot | stable PASS preserved |
| 1.2 | tone-ghost-toans-bs3-i | ✅ PASS | ti | ❌ **FAIL** | ni | flip-prone, swung to FAIL (heisenbug noise) |
| 1.3 | escape-bs-aa-b | ✅ PASS | b | ✅ PASS | b | stable PASS preserved |
| 2.1 | x2-space-vieejt-nam | ❌ FAIL | ệiet nam | ❌ FAIL | vệet nam | stable FAIL preserved, corruption shifted |
| 2.2 | word-boundary-xin-chao-ban | ❌ FAIL | xinhàoạn ban | ❌ FAIL | xàạnchao ban | composition shift (heisenbug) |
| 2.3 | en-vn-transition-hello-vieejt | ❌ FAIL | helệo viet | ❌ FAIL | helệo viet | **stable FAIL byte-identical** ✓ |
| 3.3 | engine-stress-truongf | ❌ FAIL | tờương | ❌ FAIL | ươờngg | stable FAIL preserved, corruption shifted |
| 5.1 | case-tracking-Giar | ✅ PASS | Giả | ✅ PASS | Giả | stable PASS preserved |
| 5.2 | vowel-start-uongs | ❌ FAIL | ốngg | ✅ **PASS** | uống | flip-prone, swung to PASS (heisenbug favorable) |
| 5.3 | cross-word-bs-vieejt-nam-bs4-s | ❌ FAIL | ệtết | ❌ FAIL | itết | composition shift |
| 6.1 | autocap-binh-thuongf | ❌ FAIL | ình ườnggnh | ❌ FAIL | bình ườnggn | composition shift, slightly less corrupted |

Totals: D4 = 4 PASS / 7 FAIL. D5 = 4 PASS / 7 FAIL. Net unchanged.

## L1 timing comparison

| # | Case | D4 mean / p99 / max | D5 mean / p99 / max | Δ p99 |
|---|---|---|---|---|
| 1.1 | ghost-hoaf-bs-t | 13 / 17 / 17 | 12 / 16 / 16 | −1 |
| 1.2 | tone-ghost-toans-bs3-i | 8 / 12 / 12 | 8 / 11 / 11 | −1 |
| 1.3 | escape-bs-aa-b | 9 / 11 / 11 | 7 / 9 / 9 | −2 |
| 2.1 | x2-space-vieejt-nam | 4 / 7 / 7 | 4 / 9 / 9 | +2 |
| 2.2 | word-boundary-xin-chao-ban | 4 / 6 / 6 | 2 / 5 / 5 | −1 |
| 2.3 | en-vn-transition-hello-vieejt | 4 / 8 / 8 | 4 / 9 / 9 | +1 |
| **3.3** | **engine-stress-truongf** | 4 / **17** / **17** | 4 / **18** / **18** | **+1** |
| 5.1 | case-tracking-Giar | 5 / 10 / 10 | 5 / 8 / 8 | −2 |
| 5.2 | vowel-start-uongs | 7 / 12 / 12 | 8 / 13 / 13 | +1 |
| 5.3 | cross-word-bs-vieejt-nam-bs4-s | 4 / 10 / 10 | 4 / 8 / 8 | −2 |
| 6.1 | autocap-binh-thuongf | 7 / 11 / 11 | 7 / 11 / 11 | 0 |

Worst-case p99 across all chaos cases: **18 ms** (3.3) — exactly at D12 merge gate (≤ 18 ms, 1 ms headroom over D4 worst-of-17 ms). All other p99 within ±2 ms of D4. Atomic load/store has no measurable cost over plain access on x86 for `bool`-sized primitives.

## DoD evaluation (per plan §B D5)

| Compare | Expected | Result |
|---|---|---|
| Sustained 3/3 vs D4 sustained | Match within scheduler noise | ✅ byte-identical verdict + error count; mean / p99 within ±1 ms |
| Chaos stable PASS {1.1, 1.3, 5.1} | Still PASS | ✅ all PASS, all byte-identical actuals |
| Chaos stable FAIL {2.1, 2.3, 3.3} | Still FAIL | ✅ all FAIL; 2.3 byte-identical; 2.1 + 3.3 corruption shifted within heisenbug envelope (consistent with D4-vs-D3 6.1 shift documented as noise) |
| L1 timing | No degradation | ✅ worst p99 +1 ms = scheduler noise; matches D12 gate ≤18 ms |
| Sciter UI mode toggle (manual) | Tray click → V↔E switch + icon update | ✅ verified by Phat post-rebuild |

**Verdict: D5 DoD met.** Atomic migration of `vietnameseMode_` is functionally equivalent to the D4 spike at the chaos / sustained / L1 dimensions. Sprint 1 D5 lands cleanly.

## Migration scope (this commit)

`HookEngine.h`:
- Field type: `bool vietnameseMode_ = true;` → `std::atomic<bool> vietnameseMode_{true};`
- Accessor: `IsVietnameseMode()` reads via `.load(std::memory_order_acquire)`.
- Block comment documents the writer/reader split (main thread `.store(release)` from `ApplyConfig` / `ToggleVietnameseMode` / `CheckLayoutChange` / `OnFocusChanged`-SmartSwitch; hook callback `.load(acquire)` from `ProcessKeyDown`).

`HookEngine.cpp` — 13 sites updated:
- 7 writer sites: `ApplyConfig`, `ToggleVietnameseMode` (×2 — stale-excluded path + main toggle), `CheckLayoutChange` (×2 — entering CJK + leaving CJK), `OnFocusChanged` (SmartSwitch save + restore).
- 6 reader sites: `ProcessKeyDown` fast-English exit + commit-undo Primed gate + non-VN-mode early-return + `NotifyModeChange` + `CheckLayoutChange` log + `OnFocusChanged` SmartSwitch save + log.

`SettingsDialog::vietnameseMode_` (line 122) is a separate, dialog-local field and is **out of scope** for D5 — it is not on the hook hot path.

## Implications for Sprint 1

1. **D5 demonstration confirms the atomic migration pattern is safe.** D5.x can extend to `currentMethod_`, `isTsfApp_`, and other primitive flags without re-running the full DoD evaluation each time (the pattern is now baselined; subsequent fields just need the same writer/reader audit).

2. **Heisenbug envelope is preserved.** D4 already established that the 6 chaos FAILs are TelexEngine state-machine bugs, not memory-ordering bugs. D5 expected zero verdict change on those cases — and got it. This is positive evidence that the Sprint 1 single-owner refactor is on a safe path: each migration step preserves the architectural baseline, with the bug fix work isolated to D12.5 (engine-level).

3. **3.3 p99 of 18 ms is now at the D12 merge gate.** No further headroom for hot-path additions before D12. If subsequent migrations push 3.3 above 18 ms, the gate must be re-examined (against the underlying budget of 300 ms `LowLevelHooksTimeout`, 18 ms is still <6% — the gate is conservative for code-health, not a hard limit).

## Restoration before merge

The 3 commented-out `lock_guard` lines from D4 at `HookEngine.cpp` remain commented through D5. They are still pending replacement by the full atomic + RCU pattern across Phase B (D5 = primitives in progress, D6 = `TypingConfig` RCU, D7 = audit). Sprint 1 cannot ship until D7 confirms zero `stateMutex_` acquisitions reachable from the three hook callbacks. If Phase B+ aborts, restore via `git revert` of the D4 commit (`f1f514b`).
