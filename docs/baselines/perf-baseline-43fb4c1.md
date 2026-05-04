# Perf Baseline — NexusKey @ commit `43fb4c1`

**Captured:** 2026-05-04 by Phat (Windows host).
**NexusKey runtime SHA:** `43fb4c1` (`Add self healing hook`) — master at the time Phase 0a Test Runner work began. The branch `refactor/phase-0a-test-runner` does not modify any NexusKey runtime code; only `tools/NextKeyTestRunner/` is added and a single comment in `HookEngine.cpp` (no behavioral change).
**Test runner SHA:** branch `refactor/phase-0a-test-runner` at commit `35ff824` (D9).

This file is the **frozen reference** that Phase 1+ refactor work measures itself against. Every commit that touches the NexusKey hook/engine must rerun the same corpus and verify it does not regress these numbers.

## Source artifacts

* `perf-baseline-43fb4c1.csv` — per-case CSV (canonical numeric data)
* `junit-baseline-43fb4c1.xml` — JUnit XML (CI-friendly verdict + diff)
* Corpus: `tools/NextKeyTestRunner/corpus/chaos.toml` (11 cases)
* Telex golden: `tools/NextKeyTestRunner/tests/TelexGolden.h` (201 cases — locked separately at commit `7d2ec62`)

## Reproduction

```powershell
# 1. Build runner + (debug) NexusKey
cmake --build build --target NextKeyTestRunner --config Debug
cmake --build build --target NextKeyApp        --config Debug

# 2. Start NexusKey debug build
.\build\Debug\NexusKey.exe

# 3. Open Notepad

# 4. Run corpus from a console
cd build\tools\Debug
.\NextKeyTestRunner.exe ^
    --corpus    ..\..\..\tools\NextKeyTestRunner\corpus\chaos.toml ^
    --hook-log  ..\..\Debug\NexusKey_hook.log ^
    --junit     ..\..\..\docs\baselines\junit-baseline-XXXX.xml ^
    --perf-csv  ..\..\..\docs\baselines\perf-baseline-XXXX.csv ^
    --delay-ms=5000

# 5. Click Notepad within 5 s. After the 11 cases run,
#    stop NexusKey from the tray and press Enter at the prompt
#    so its 8 KB log buffer flushes for post-mortem L1 analysis.
```

## Summary

| Metric | Value |
|---|---|
| Total cases | 11 |
| **PASS** | **5** (1.1, 1.2, 1.3, 5.1, 6.1) |
| **FAIL** | **6** (2.1, 2.2, 2.3, 3.3, 5.2, 5.3) |
| Total wall-clock | 5.847 s |
| L1 hook-callback p99 (worst case) | 16 ms |
| L1 floor (driver+OS limit, configured 500 µs → observed) | ~3 ms |

## Per-case results

| # | Case | Verdict | inter_key_us | wall ms | L1 n | L1 mean | L1 p99 | L1 max |
|---|---|---|---:|---:|---:|---:|---:|---:|
| 1.1 | ghost-hoaf-bs-t | ✅ PASS | 10 000 | 543 | 5 | 13 | 16 | 16 |
| 1.2 | tone-ghost-toans-bs3-i | ✅ PASS | 5 000 | 556 | 8 | 8 | 13 | 13 |
| 1.3 | escape-bs-aa-b | ✅ PASS | 5 000 | 507 | 3 | 7 | 9 | 9 |
| 2.1 | x2-space-vieejt-nam | ❌ FAIL | 1 000 | 520 | 9 | 3 | 6 | 6 |
| 2.2 | word-boundary-xin-chao-ban | ❌ FAIL | 1 000 | 523 | 13 | 3 | 6 | 6 |
| 2.3 | en-vn-transition-hello-vieejt | ❌ FAIL | 1 000 | 521 | 11 | 3 | 6 | 6 |
| 3.3 | engine-stress-truongf | ❌ FAIL | 500 | 515 | 7 | 4 | 16 | 16 |
| 5.1 | case-tracking-Giar | ✅ PASS | 5 000 | 506 | 4 | 5 | 9 | 9 |
| 5.2 | vowel-start-uongs | ❌ FAIL | 5 000 | 521 | 5 | 7 | 12 | 12 |
| 5.3 | cross-word-bs-vieejt-nam-bs4-s | ❌ FAIL | 1 000 | 541 | 14 | 4 | 7 | 7 |
| 6.1 | autocap-binh-thuongf | ✅ PASS | 5 000 | 593 | 13 | 8 | 16 | 16 |

## Failure diffs (from JUnit XML)

```
2.1  expected: việt nam        actual: ệiet nam
2.2  expected: xin chào bạn    actual: xàạnchao ban
2.3  expected: hello việt      actual: helệo viet
3.3  expected: trường           actual: tờương
5.2  expected: uống             actual: ốngg
5.3  expected: viết             actual: etết
```

## Observations

1. **Driver/OS floor on inter-key timing.** Cases configured with `inter_key_us=500` and `inter_key_us=1000` consistently land at L1 mean 3–4 ms. SendInput + Windows scheduler cannot achieve sub-millisecond inter-key from a user-mode driver, regardless of `timeBeginPeriod(1)` and busy-wait. Phase 1+ refactor performance targets should be expressed against this measured floor (~3 ms), not against the configured value.

2. **Bugs reproduce at 3–4 ms inter-key.** Six cases fail and the FAIL distribution is the same shape as observed during D7 baseline runs (modulo 6.1 which is borderline — passed here, failed in earlier D7 run, indicating a heisenbug-ish race). The corpus is sufficient as a regression detector at currently-achievable typing speeds.

3. **Hook-callback p99 jitter is small.** No case exceeds 17 ms p99 even on the slowest configuration. The Windows `LowLevelHooksTimeout` is 300 ms, so even worst-case callbacks have ~280 ms of headroom. The original brainstorm hypothesis that mutex contention pushed callback time anywhere near the timeout limit is **not supported by this baseline** — bugs are functional (state machine / composition), not raw latency.

4. **5.1 (CapsLock substitute) passes.** Original brainstorm test wanted CapsLock+G+CapsOff; we type Shift+G to keep system state deterministic. Passing here confirms the case substitution is correct for this aspect of the bug. Add a separate manual-only CapsLock test if Phase 1+ wants to verify CapsLock-mid-word handling explicitly.

5. **Heisenbug awareness.** During D8 development we briefly switched the hook log buffer to `_IONBF` for real-time read; that slowed the hook enough that **all 11 cases passed** — a clear instrumentation-induced false positive. Future Phase 1+ work that adds instrumentation must validate against this baseline that bugs still reproduce at the same rate.

## Failure categorization (for Phase 1+ planning)

| Category | Cases | Hypothesis from brainstorm |
|---|---|---|
| Speed-sensitive (1 ms config, observed 3 ms) | 2.1, 2.2, 2.3, 5.3 | Mutex contention or event reorder when keystrokes arrive faster than engine can drain |
| BS / cross-word state | 5.3 | Word snapshot replay broken — engine drops or mis-routes characters when BS crosses a committed boundary |
| Vowel/tone routing | 3.3, 5.2 | Tone applied to wrong vowel position (e.g. `truongf` → `tờương` instead of `trường`); engine vowel-priority logic buggy under stress |
| State machine glitch | 2.1 (`ệiet`) | Tone applied to wrong char during fast input; classic mutex/race symptom |

## Exit gate

This file is committed alongside its CSV and XML artifacts. Phase 1 (Single-Owner refactor) **must not** be merged until a fresh corpus run on the post-refactor commit produces at least the same PASS count (5/11) AND fixes at least one of the six FAILs without introducing any regression.

When Phase 1 lands, capture a new baseline as `perf-baseline-<new-sha>.{md,csv,xml}` and append a comparison row to a top-level `docs/baselines/index.md` (TBD).
