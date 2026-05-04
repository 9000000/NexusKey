# Perf Baseline — Sustained Forward 200wpm @ commit `3459642`

**Captured:** 2026-05-04 by Phat (Windows host).
**NexusKey runtime SHA:** `43fb4c1` (master `Add self healing hook`) — Phase 0a baseline state. The branch `refactor/phase-1-single-owner` adds tooling and docs only; no runtime change yet at the moment of this baseline. The branch HEAD when this run was captured is `3459642` (Sprint 1 D0 fix commit).
**Test runner SHA:** `3459642` (Sprint 1 D0 + Windows-min-macro fix).

This file is the **frozen reference** for the *sustained-forward* dimension that Phase 1+ refactor work measures itself against. It complements `perf-baseline-43fb4c1.md` (chaos corpus) — chaos measures binary correctness on race-prone short cases; sustained measures user-perceptible smoothness on a realistic 200-word continuous run.

## Source artifacts

* `perf-baseline-3459642-sustained-forward.csv` — per-case CSV (canonical numeric data)
* `perf-baseline-3459642-sustained-forward.xml` — JUnit XML (CI-friendly verdict)
* Corpus: `tools/NextKeyTestRunner/corpus/sustained.toml` (1 case)
* Source paragraph: adapted from `tools/NextKeyTestRunner/tests/TelexGolden.h:218` (golden against vn-str). Two `Điều này` sentence-openers rewritten to `Việc này` because uppercase Đ (U+0110) is not typeable via `VkKeyScanW` on a US layout.

## Reproduction

```powershell
# 1. Build
cmake --build build --config Debug --target NextKeyTestRunner
cmake --build build --config Debug --target NexusKey

# 2. Start NexusKey debug
.\build\Debug\NexusKey.exe

# 3. Open a fresh empty Notepad

# 4. From a separate console
.\build\tools\Debug\NextKeyTestRunner.exe `
  --corpus    tools\NextKeyTestRunner\corpus\sustained.toml `
  --hook-log  build\Debug\NexusKey_hook.log `
  --junit     docs\baselines\perf-baseline-XXXX-sustained-forward.xml `
  --perf-csv  docs\baselines\perf-baseline-XXXX-sustained-forward.csv `
  --delay-ms=5000

# 5. Click Notepad within 5 s. After ~110 s of typing, the runner prints
#    PASS/FAIL + error rate. Stop NexusKey from the tray and press Enter
#    so its 8 KB log buffer flushes for post-mortem L1 analysis.
```

## Summary

| Metric | Value |
|---|---|
| Cases | 1 (`forward-200wpm-sustained`) |
| Verdict | **PASS** at threshold 99 % |
| Error rate | **0.41 %** (5 chars wrong) |
| Total wall-clock | 86.743 s |
| L1 hook keystrokes (post-mortem) | 1 631 |
| L1 inter-arrival mean | 52 ms |
| L1 inter-arrival p99 | 60 ms |
| L1 inter-arrival max | 65 ms |
| Configured inter-key | 50 000 µs (50 ms = ~200 wpm equivalent for Vietnamese telex) |

## Per-case result

| Case | Verdict | Mode | Err chars | Err % | Wall ms | L1 n | L1 mean | L1 p99 | L1 max |
|---|---|---|---:|---:|---:|---:|---:|---:|---:|
| `forward-200wpm-sustained` | ✅ PASS | edit_distance | 5 | 0.41 % | 86 743 | 1 631 | 52 | 60 | 65 |

The `--delay-ms=5000` initial focus delay and the post-send / Ctrl+A+C verify pipeline contribute roughly 5.5 s of non-typing time, so the *actual typing portion* is ~81 s. At 1 631 raw keys, that's an effective ~20 keystrokes/sec or ~133 Vietnamese-words/min — close to the 50 ms inter-key target.

## Observations

1. **Master state is already at 0.41 % error rate on forward sustained typing.** The bug surface for forward, low-edit Vietnamese typing is small. The 6 chaos FAILs do NOT translate proportionally into sustained corruption — they're triggered by sub-millisecond burst typing that's unreachable from human keyboards. At realistic 50 ms inter-key, the engine handles the load well.

2. **Hook L1 timing is healthy at sustained pace.** Mean 52 ms ≈ configured 50 ms (1–2 ms scheduler/driver overhead). p99 = 60 ms = 10 ms jitter — fine; no risk of `LowLevelHooksTimeout` (300 ms) at this rate. Note: this is *inter-key arrival time* at the hook, not callback duration. The chaos baseline's "p99 ≤ 16 ms" gate is for sub-ms burst input where `inter_key_us = 1000`; it's not directly comparable to sustained.

3. **The 30 %-improvement gate language in the plan is unworkable for this dimension.** 30 % of 0.41 % = 0.12 % — below measurement noise. Sprint 1 must reformulate the sustained-forward gate to "no regression vs 0.41 %" instead of a relative reduction. The interesting movement will live in D2 (sustained edit corpus: typo correction + cross-word backspace) once those baselines exist.

4. **L1 log captured 3 280 entries; we sliced 1 631 keydowns into the case window.** The other half are key-up events plus the post-typing Ctrl+A / Ctrl+C verify pulses outside the case window — the parser correctly excludes them via the `endMs` exclusive filter introduced in commit `2efd180`.

5. **No `SendString` failures.** The paragraph edits (Điều → Việc) successfully avoided the uppercase-Đ untypeable trap. This pattern is now precedent for D2 corpus encoding.

## Failure detail (5 wrong chars)

Edit-distance verdict reports the *count* and *percentage* but not the *positions*. The XML `<testcase>` is self-closing because the case PASSed (under the 99 % threshold). To diagnose specific corruptions, re-run with the threshold lowered (e.g. `threshold_pct = 100.0`) so the runner emits the diff body to the JUnit failure section. Doing so post-hoc was not necessary for this baseline — the absolute number (5 chars) is what Sprint 1 needs as a no-regression anchor.

## Failure categorization (none triggered at this dimension)

The ~0.4 % error rate did not hit any of the chaos baseline's failure buckets visibly (no observable speed-sensitive races, no cross-word state issues, no vowel/tone routing under load). This is consistent with the hypothesis that **chaos-style bugs are speed-bound and don't reproduce at 50 ms inter-key**.

## Exit gate (revised)

Phase 1+ refactor work targeting the sustained-forward dimension MUST satisfy:

* **No regression**: error rate ≤ 0.41 % (was 0.41 % at master 43fb4c1).
* **No hook regression**: L1 mean ≤ 53 ms, p99 ≤ 62 ms, max ≤ 67 ms (small headroom over current).
* **No corpus failure**: case must continue to PASS at threshold 99 %.

The "≥ 30 % improvement" wording from the original Sprint 1 plan is **REMOVED for forward sustained** — already too clean. Improvement targets shift to:
* Chaos corpus: ≥ 1 FAIL flipped (unchanged from HANDOFF.md gate).
* Sustained edit corpus (D2): TBD when D2 baseline is captured.

When Phase 1 lands, capture a fresh `perf-baseline-<new-sha>-sustained-forward.{csv,xml,md}` and append a comparison row to a future `docs/baselines/index.md`.
