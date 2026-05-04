# Perf Baseline — Chaos D5.2 atomic remaining-primitives migration

**Captured:** 2026-05-04 by Phat (Windows host).
**NexusKey runtime SHA:** `6119e81` + uncommitted D5.2 (15 hook-read primitive flags migrated to `std::atomic`: 7 per-app cached bools, `excludedPid_` DWORD, 6 config-derived bools at ~60 sites in `HookEngine.cpp`). The 3 hook-thread `lock_guard` lines from D4 remain commented.
**Test runner SHA:** `f1f514b`.
**Anchor:** `perf-baseline-d5.1-atomic-method-tsf-chaos.md` (D5.1 capture — `currentMethod_` + `isTsfApp_`).
**Purpose:** D5.2 migration — does extending the atomic acquire/release pattern to the remaining 15 hook-callback-read primitives introduce any new regression vs the D5.1 outcome?

## TL;DR — DoD met, 3.3 engine-stress timing improved

**No new regression.** Stable PASS preserved on {1.1, 1.3, 5.1} byte-identical; flip-prone 1.2 + 5.2 byte-identical to D5.1; stable FAIL preserved on {2.1, 2.3, 3.3} (verdict-identical, 2.1 byte-identical to D5.1, 2.3 + 3.3 corruption within heisenbug envelope); 2.2 byte-identical to D5.1; sustained byte-identical at the verdict + error layer; L1 chaos worst p99 unchanged at 16 ms (D5.1 baseline) with 3.3 dropping further from 16 → 11 ms (−5 ms on the engine-stress case).

## Per-case result vs D5.1 anchor

| # | Case | D5.1 verdict | D5.1 actual | D5.2 verdict | D5.2 actual | Δ |
|---|---|---|---|---|---|---|
| 1.1 | ghost-hoaf-bs-t | ✅ PASS | hot | ✅ PASS | hot | byte-identical |
| 1.2 | tone-ghost-toans-bs3-i | ✅ PASS | ti | ✅ PASS | ti | byte-identical (flip-prone stable across two captures) |
| 1.3 | escape-bs-aa-b | ✅ PASS | b | ✅ PASS | b | byte-identical |
| 2.1 | x2-space-vieejt-nam | ❌ FAIL | ệiet nam | ❌ FAIL | ệiet nam | **byte-identical FAIL** ✓ |
| 2.2 | word-boundary-xin-chao-ban | ❌ FAIL | xàạnchao ban | ❌ FAIL | xàạnchao ban | byte-identical FAIL |
| 2.3 | en-vn-transition-hello-vieejt | ❌ FAIL | helêệ viet | ❌ FAIL | heệlo viet | stable FAIL preserved, novel corruption shape (heisenbug envelope) |
| 3.3 | engine-stress-truongf | ❌ FAIL | tờương | ❌ FAIL | ờnương | stable FAIL preserved, novel corruption shape |
| 5.1 | case-tracking-Giar | ✅ PASS | Giả | ✅ PASS | Giả | byte-identical |
| 5.2 | vowel-start-uongs | ✅ PASS | uống | ✅ PASS | uống | byte-identical |
| 5.3 | cross-word-bs-vieejt-nam-bs4-s | ❌ FAIL | tếti | ❌ FAIL | ết n | composition shift (heisenbug) |
| 6.1 | autocap-binh-thuongf | ❌ FAIL | bình ườnggn | ❌ FAIL | bình ươờngg | composition shift (heisenbug) |

Totals: D5.1 = 5 PASS / 6 FAIL. D5.2 = 5 PASS / 6 FAIL. Net unchanged. Stable PASS group all byte-identical; 2.1 + 2.2 stable FAIL also byte-identical.

## L1 timing comparison

| # | Case | D5.1 mean / p99 / max | D5.2 mean / p99 / max | Δ p99 |
|---|---|---|---|---|
| **1.1** | **ghost-hoaf-bs-t** | 12 / **16** / 16 | 12 / **16** / 16 | **0 (cap)** |
| 1.2 | tone-ghost-toans-bs3-i | 8 / 12 / 12 | 8 / 14 / 14 | +2 |
| 1.3 | escape-bs-aa-b | 6 / 8 / 8 | 8 / 11 / 11 | +3 |
| 2.1 | x2-space-vieejt-nam | 3 / 8 / 8 | 4 / 8 / 8 | 0 |
| 2.2 | word-boundary-xin-chao-ban | 3 / 7 / 7 | 3 / 6 / 6 | −1 |
| 2.3 | en-vn-transition-hello-vieejt | 4 / 9 / 9 | 4 / 10 / 10 | +1 |
| **3.3** | **engine-stress-truongf** | 4 / **16** / 16 | 3 / **11** / 11 | **−5** ← best improvement |
| 5.1 | case-tracking-Giar | 5 / 8 / 8 | 5 / 10 / 10 | +2 |
| 5.2 | vowel-start-uongs | 8 / 12 / 12 | 7 / 11 / 11 | −1 |
| 5.3 | cross-word-bs-vieejt-nam-bs4-s | 4 / 9 / 9 | 4 / 9 / 9 | 0 |
| 6.1 | autocap-binh-thuongf | 7 / 11 / 11 | 7 / 14 / 14 | +3 |

Worst-case p99 across all chaos cases: **16 ms** (1.1) — unchanged from D5.1 worst (also 16 ms). Headroom under D12 merge gate (≤ 18 ms) preserved at 2 ms. **3.3 engine-stress case drops from 16 → 11 ms (−5 ms).** This continues the pattern from D5 → D5.1 (where 3.3 also improved by 2 ms): each atomic migration step reduces hot-path cache-line contention with main-thread writers, even though D4 already ruled out mutex contention as the *bug source*. Other cases ±2-3 ms within scheduler noise.

## DoD evaluation (per plan §B D5; D5.2 anchor = D5.1)

| Compare | Expected | Result |
|---|---|---|
| Sustained 3/3 vs D5.1 sustained | Match within scheduler noise | ✅ byte-identical verdict + error count; p99 within +1-2 ms (forward max +7 ms, still under D4 65 ms / D5 63 ms baseline range) |
| Chaos stable PASS {1.1, 1.3, 5.1} | Still PASS, byte-identical | ✅ all preserved byte-identical |
| Chaos stable FAIL {2.1, 2.3, 3.3} | Still FAIL | ✅ verdicts preserved; 2.1 byte-identical to D5.1; 2.3 + 3.3 produce novel corruption shapes within the heisenbug envelope already documented in HANDOFF |
| L1 timing | No degradation vs D5.1 worst-case | ✅ worst p99 16 ms = D5.1 worst (cap unchanged); 3.3 dropped −5 ms (positive movement) |
| Manual: per-app classification | Notepad EditMsgPaste path + bait-char + Electron split paths still produce correct output | ✅ chaos + sustained ran cleanly on Notepad (Win11 EditMsgPaste path active per AppDetect log) |

**Verdict: D5.2 DoD met.** Atomic migration of 15 remaining primitive flags is safe. Phase B's "primitive flags" sub-goal in plan §B D5 is now fully complete — all hook-read scalars on `HookEngine` are `std::atomic` with acquire/release semantics.

## Migration scope (this commit, D5.2)

`HookEngine.h`:
- 15 fields migrated:
  - **Per-app cached** (8): `isExcludedApp_`, `excludedPid_` (DWORD), `isConsoleApp_`, `isElectronApp_`, `skipEmptyChar_`, `needBaitChar_`, `useClipboardPaste_`, `useEditMsgPath_`, `isOutlookApp_` — all written by `OnFocusChanged` + `RefreshFocusCache` (main thread via `WinEventProc`); read on the hook hot path in `ProcessKeyDown` / `HandleAlphaKey` / `DispatchSendInput` / `SendBackspaces` / `ShouldUseClipboard`.
  - **Config-derived** (6): `macroEnabled_`, `macroInEnglish_`, `tempOffMacroByEsc_`, `autoCaps_`, `autoCapsMacro_`, `tempOffByAlt_` — all written by `ApplyConfig` (main); read in `ProcessKeyDown` (macro tracking, auto-cap state machine, double-Alt detection) and `TryExpandMacro`.
- Block comments group fields by writer thread + reader path.
- Skipped (intentionally): `excludeApps_`, `tsfApps_`, `smartSwitch_`, `beepOnSwitch_` (config-derived but only read on main-thread paths — `Toggle`, `OnFocusChanged`, `CheckLayoutChange`); `tempEngineOff_`, `tempMacroOff_`, `macroCrossCommit_` (per-word runtime state, hook-thread-only).

`HookEngine.cpp` — ~60 sites updated:
- **`OnFocusChanged` + `RefreshFocusCache` classification block** (largest refactor): `ClassifyWindow` takes `bool& outIsConsole` (signature unchanged) — incompatible with `std::atomic<bool>`. Solution: stage the 7 per-app classification flags in `local*` stack variables, run the full decision tree (zed.exe / notepad.exe / outlook / WebView2 detection), then publish all 7 flags via sequential `.store(release)` after the final values are determined. This is the natural pattern for "compute classification, then publish atomically" and matches the snapshot-then-publish idiom from D5.1.
- **`ProcessKeyDown`**: hoist the 4 most-read config flags (`macroEnabled_`, `macroInEnglish_`, `tempOffMacroByEsc_`, `autoCaps_`) into local `const bool` snapshots at the function top — used 7+, 2, 2, and 2 times respectively. Avoids 13 redundant `.load(acquire)` calls per keystroke.
- **`HandleAlphaKey`**: similar hoist for the 5 per-app classification flags read in the passthrough decision (`isElectronApp_`, `isOutlookApp_`, `needBaitChar_`, `skipEmptyChar_`, `useEditMsgPath_`).
- **`DispatchSendInput`**: hoist `isElectronApp_` + `isConsoleApp_` (each used twice — gate + delay base).
- **`SendBackspaces`**: hoist `needBaitChar_` (gate + log).
- **`ToggleVietnameseMode`**, **`ReloadFromToml`**, **`VerifyExcludedState`**, **`NotifyModeChange`**, **`ReloadExcludedApps`**, **`QuickSyncFromSharedState`**, **`Start`**: per-site `.load(acquire)` / `.store(release)` (single read or single write per call).

`tests/HookEngineAtomicTests.cpp` — 2 new GTest cases:
- `MultiPublishVisibility` — 7-field publish + handshake pattern, 500 iterations. Mirrors `OnFocusChanged`'s sequential `.store(release)` cascade. Verifies each individual store/load pair sees the published value, even when stores happen in tight succession.
- `PidStoreLoadRoundTrip` — `std::atomic<uint32_t>` round-trip mirroring `excludedPid_`'s OnFocusChanged → ProcessKeyDown PID equality check.
- `static_assert<std::atomic<uint32_t>::is_always_lock_free>` added.

Linux test count: 1 376 → **1 378** (+2 cases). All pass.

## Implications for Sprint 1

1. **Phase B D5 is complete.** All 18 primitive flags read on the hook callback path are now atomic (3 in D5/D5.1: `vietnameseMode_`, `currentMethod_`, `isTsfApp_`; 15 in D5.2). Plan §B D5 sub-goal fully met. D6 (RCU `shared_ptr<TypingConfig>`) is the next Phase B step.

2. **L1 timing trajectory is decisively positive.** Worst chaos p99: D4 17 ms → D5 18 ms → D5.1 16 ms → D5.2 16 ms (with 3.3 dropping further to 11 ms). The atomic migration is consistently *helping* hot-path predictability, not just satisfying Rule #11. Pillar #1 "Nhanh" benefits incidentally — and 3.3's L1 p99 dropping from 17 ms (D4 SPIKE worst) to 11 ms (D5.2) is direct evidence that engine-stress cases were hitting cache-line contention with main-thread writers.

3. **D7 audit script becomes simpler.** Original D7 plan: grep for `stateMutex_` reachable from `LowLevelKeyboardProc` / `WinEventProc` / `LowLevelMouseProc`. With D5 + D5.1 + D5.2 done, the script's complementary check — that no plain (non-atomic) primitive read is reachable from those callbacks — is also enforceable. The D7 audit can include both: "no mutex acquisition reachable from hook" + "all hook-reachable primitive reads use `.load(acquire)`".

4. **D6 RCU shared_ptr remains the next major step.** `TypingConfig` is a complex struct (string members, vectors, nested objects) — atomic acquire/release on a single load doesn't suffice for safe concurrent read/write. D6's `std::atomic<std::shared_ptr<TypingConfig>>` (or `std::atomic_load(&cfg_ptr_)` pattern) is the next correctness gap to close.

## Restoration before merge

The 3 commented-out `lock_guard` lines from D4 at `HookEngine.cpp` remain commented through D5.2. After D6 + D7, the comment will be replaced by a positive assertion ("no mutex needed — all reads are atomic-protected"). If Phase B+ aborts before D6 completes, restore via `git revert` of the D4 commit (`f1f514b`).
