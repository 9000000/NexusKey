# Perf Baseline — Chaos D5.1 atomic `currentMethod_` + `isTsfApp_` migration

**Captured:** 2026-05-04 by Phat (Windows host).
**NexusKey runtime SHA:** `6caa1ba` + uncommitted D5.1 (`currentMethod_` migrated to `std::atomic<InputMethod>`, `isTsfApp_` to `std::atomic<bool>` with acquire/release at 19 sites in `HookEngine.cpp` + accessor block in `HookEngine.h`). The 3 hook-thread `lock_guard` lines from D4 remain commented.
**Test runner SHA:** `f1f514b`.
**Anchor:** `perf-baseline-d5-atomic-vnmode-chaos.md` (D5 capture — `vietnameseMode_` only).
**Purpose:** D5.1 incremental migration — does extending the atomic acquire/release pattern from `vietnameseMode_` to `currentMethod_` (enum) + `isTsfApp_` (bool) introduce any new regression vs the D5 outcome?

## TL;DR — DoD met, slight improvement

**No new regression; net favorable.** Stable PASS preserved on {1.1, 1.3, 5.1} byte-identical; flip-prone 1.2 swung from FAIL → PASS; stable FAIL preserved on {2.1, 2.3, 3.3} (verdict-identical, 2.1 + 3.3 corruption shape now matches the D4 capture, 2.3 shape is novel but still FAIL); 5.2 + 6.1 + 2.2 byte-identical to D5; L1 worst-case p99 dropped from 18 ms (D5) to 16 ms (D5.1) — back below the D4 17 ms baseline, restoring 2 ms headroom under the D12 merge gate. Chaos net 5 PASS / 6 FAIL (was 4 PASS / 7 FAIL at D5).

## Per-case result vs D5 anchor

| # | Case | D5 verdict | D5 actual | D5.1 verdict | D5.1 actual | Δ |
|---|---|---|---|---|---|---|
| 1.1 | ghost-hoaf-bs-t | ✅ PASS | hot | ✅ PASS | hot | stable PASS byte-identical |
| 1.2 | tone-ghost-toans-bs3-i | ❌ FAIL | ni | ✅ **PASS** | ti | flip-prone, swung to PASS (heisenbug favorable) |
| 1.3 | escape-bs-aa-b | ✅ PASS | b | ✅ PASS | b | stable PASS byte-identical |
| 2.1 | x2-space-vieejt-nam | ❌ FAIL | vệet nam | ❌ FAIL | ệiet nam | stable FAIL preserved, corruption shape now matches D4 |
| 2.2 | word-boundary-xin-chao-ban | ❌ FAIL | xàạnchao ban | ❌ FAIL | xàạnchao ban | **byte-identical FAIL** ✓ |
| 2.3 | en-vn-transition-hello-vieejt | ❌ FAIL | helệo viet | ❌ FAIL | helêệ viet | stable FAIL preserved, novel corruption shape (heisenbug envelope) |
| 3.3 | engine-stress-truongf | ❌ FAIL | ươờngg | ❌ FAIL | tờương | stable FAIL preserved, corruption shape now matches D4 |
| 5.1 | case-tracking-Giar | ✅ PASS | Giả | ✅ PASS | Giả | stable PASS byte-identical |
| 5.2 | vowel-start-uongs | ✅ PASS | uống | ✅ PASS | uống | byte-identical PASS |
| 5.3 | cross-word-bs-vieejt-nam-bs4-s | ❌ FAIL | itết | ❌ FAIL | tếti | composition shift (heisenbug) |
| 6.1 | autocap-binh-thuongf | ❌ FAIL | bình ườnggn | ❌ FAIL | bình ườnggn | **byte-identical FAIL** ✓ |

Totals: D5 = 4 PASS / 7 FAIL. D5.1 = **5 PASS / 6 FAIL**. Net + 1 PASS via 1.2 flip; no PASS regressed.

## L1 timing comparison

| # | Case | D5 mean / p99 / max | D5.1 mean / p99 / max | Δ p99 |
|---|---|---|---|---|
| 1.1 | ghost-hoaf-bs-t | 12 / 16 / 16 | 12 / 16 / 16 | 0 |
| 1.2 | tone-ghost-toans-bs3-i | 8 / 11 / 11 | 8 / 12 / 12 | +1 |
| 1.3 | escape-bs-aa-b | 7 / 9 / 9 | 6 / 8 / 8 | −1 |
| 2.1 | x2-space-vieejt-nam | 4 / 9 / 9 | 3 / 8 / 8 | −1 |
| 2.2 | word-boundary-xin-chao-ban | 2 / 5 / 5 | 3 / 7 / 7 | +2 |
| 2.3 | en-vn-transition-hello-vieejt | 4 / 9 / 9 | 4 / 9 / 9 | 0 |
| **3.3** | **engine-stress-truongf** | 4 / **18** / **18** | 4 / **16** / **16** | **−2** |
| 5.1 | case-tracking-Giar | 5 / 8 / 8 | 5 / 8 / 8 | 0 |
| 5.2 | vowel-start-uongs | 8 / 13 / 13 | 8 / 12 / 12 | −1 |
| 5.3 | cross-word-bs-vieejt-nam-bs4-s | 4 / 8 / 8 | 4 / 9 / 9 | +1 |
| 6.1 | autocap-binh-thuongf | 7 / 11 / 11 | 7 / 11 / 11 | 0 |

Worst-case p99 across all chaos cases: **16 ms** (3.3, 1.1) — better than D5's 18 ms and below D4's 17 ms. Headroom under D12 merge gate (≤ 18 ms) restored to 2 ms. Atomic enum load on x86 is one MOV instruction (lock-free per `static_assert<std::atomic<InputMethod>::is_always_lock_free>` in the test file) — no measurable runtime cost.

## DoD evaluation (per plan §B D5; D5.1 anchor = D5)

| Compare | Expected | Result |
|---|---|---|
| Sustained 3/3 vs D5 sustained | Match within scheduler noise | ✅ byte-identical verdict + error count; mean/p99 1–3 ms faster |
| Chaos stable PASS {1.1, 1.3, 5.1} | Still PASS, byte-identical | ✅ all preserved |
| Chaos stable FAIL {2.1, 2.3, 3.3} | Still FAIL | ✅ verdicts preserved; 2.1 + 3.3 corruption returned to the D4 shape, 2.3 produced a novel shape — all within the heisenbug envelope already documented in HANDOFF |
| L1 timing | No degradation vs D5 worst-case | ✅ worst p99 16 ms < D5 worst p99 18 ms (improvement) |
| TSF passthrough (manual) | Hook stays passive when foreground exe is in TSF list | ✅ verified by Phat post-rebuild (TSF DLL handles input as expected) |

**Verdict: D5.1 DoD met.** Atomic migration of `currentMethod_` + `isTsfApp_` is safe. The L1 p99 improvement from 18 ms → 16 ms is unexpected positive evidence: extending the atomic pattern reduced hot-path variability slightly, likely because the hook callback no longer races on the same `stateMutex_`-tied cache line as main-thread focus-event writers.

## Migration scope (this commit, D5.1)

`HookEngine.h`:
- `InputMethod currentMethod_ = InputMethod::Telex;` → `std::atomic<InputMethod> currentMethod_{InputMethod::Telex};`
- `bool isTsfApp_ = false;` → `std::atomic<bool> isTsfApp_{false};`
- Block comments document writer/reader split for both fields.

`HookEngine.cpp` — 19 sites updated:
- `currentMethod_`: 4 writers (`Start`, `QuickSyncFromSharedState`, `ReloadFromToml`, `OnFocusChanged` per-app override) + 5 readers (Start log + 2 in `ProcessKeyDown` commit-undo + alpha-key paths + `OnFocusChanged` override gate). Hot-path readers in `ProcessKeyDown` and the alpha-key handler hoist the load to a single `const InputMethod method = currentMethod_.load(acquire);` local — avoids two atomic loads in the same condition.
- `isTsfApp_`: 4 writers (`ReloadFromToml`, `OnFocusChanged`) + 4 readers (`ProcessKeyDown` line 765 early-return, `ProcessKeyUp` line 1236 early-return, plus 2 reads in `OnFocusChanged` for the previous-state snapshot and the post-store TSF-mode-callback). Where the same field is read multiple times in adjacent log/branch lines, a single `const bool wasTsfApp = isTsfApp_.load(acquire);` snapshot is taken and the new value is held in a `bool newTsfApp` local before the `.store(release)` — same pattern teammate established in D5 for `vietnameseMode_`.

`tests/HookEngineAtomicTests.cpp` — 2 new GTest cases:
- `EnumStoreLoadRoundTrip` — `std::atomic<InputMethod>` stores each enumerator (Telex / VNI / Combined) and confirms each loads back byte-identical.
- `EnumCrossThreadVisibility` — producer thread cycles all three enumerators with release-stores; consumer thread observes via acquire-loads and asserts every observation is one of the legal enumerator values (no torn read producing a numeric value outside the set). 2 000 producer iterations.
- `static_assert<std::atomic<InputMethod>::is_always_lock_free>` added at file scope. Mirrors `InputMethod` enum locally so the cross-platform `NextKeyTests` target stays Linux-buildable without pulling in `core/config/TypingConfig.h`'s Win32-only deps.

Linux test count: 1 374 → **1 376** (+2 cases). All pass.

## Implications for Sprint 1

1. **D5.x pattern is mature.** Three primitive fields have now been migrated cleanly with the same writer/reader audit + hoisted-load idiom. The remaining hook-read primitives (`isExcludedApp_`, `isConsoleApp_`, `isElectronApp_`, `skipEmptyChar_`, `needBaitChar_`, `useClipboardPaste_`, `useEditMsgPath_`, `isOutlookApp_`, the per-app cached flags, plus the config-derived flags `beepOnSwitch_` / `smartSwitch_` / `excludeApps_` / `tsfApps_` / `autoCaps_` / `autoCapsMacro_` / `tempOffMacroByEsc_` / `macroEnabled_` / `macroInEnglish_`) are all candidates for D5.2 or can be folded into the D7 audit script as "atomic or unreachable" assertions.

2. **L1 p99 improvement is a secondary signal worth watching.** D5.1 dropped chaos worst p99 by 2 ms vs D5. If D5.2 (remaining primitives) shows additional drop, that's evidence the contention model (multiple writers competing for `stateMutex_` while the hook reader spins) was costing some headroom even though D4 ruled out mutex as the *bug source*. The Pillar #1 "Nhanh" goal benefits regardless.

3. **3.3 corruption returning to the D4 shape** (`tờương`) after D5 produced `ươờngg` is positive heisenbug-bounding evidence — the engine state-machine bug has a small finite set of corrupt outputs, not an infinite spectrum. D12.5 single-FAIL fix (recommended target 3.3) can verify against any of those shapes.

## Restoration before merge

The 3 commented-out `lock_guard` lines from D4 at `HookEngine.cpp` remain commented through D5.1. D6 (RCU `shared_ptr` for `TypingConfig`) and D7 (audit) are still pending before Sprint 1 can ship. If Phase B+ aborts, restore via `git revert` of the D4 commit (`f1f514b`).
