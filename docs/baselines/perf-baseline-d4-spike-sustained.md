# Perf Baseline — Sustained D4 spike @ branch HEAD `a28f1ea` + 3 lock_guard comment-out

**Captured:** 2026-05-04 by Phat (Windows host).
**NexusKey runtime SHA:** `a28f1ea` + uncommitted spike (3 hook-thread `lock_guard` lines commented out).
**Test runner SHA:** `a28f1ea`.
**Anchor:** `perf-baseline-a28f1ea-pre-spike-sustained.md` (D3 capture, pre-spike).
**Purpose:** confirm the spike does not regress the sustained dimension at realistic typing pace.

## Summary — zero drift

| Case | D3 (pre-spike) | D4 (spike) | Δ |
|---|---|---|---|
| `forward-200wpm-sustained` | 5 chars / 0.41 % / mean 52 / p99 60 / max 63 | 5 chars / 0.41 % / mean 52 / p99 60 / max 65 | identical verdict; max +2 ms (within scheduler noise) |
| `edit-1-inline-tone-fix-vieet-bs-jt` | 0 / 0.00 % / mean 54 / p99 56 | 0 / 0.00 % / mean 54 / p99 58 | identical verdict; p99 +2 ms |
| `edit-2-cross-word-bs-vieejt-nam-bs4-s` | 0 / 0.00 % / mean 53 / p99 59 | 0 / 0.00 % / mean 53 / p99 58 | identical verdict; p99 −1 ms |

**All three cases are zero-regression** at realistic 50 ms inter-key. The spike does not break sustained-dimension behavior.

## Per-case result

| Case | Verdict | Mode | Err chars | Err % | Wall ms | L1 n | L1 mean | L1 p99 | L1 max |
|---|---|---|---:|---:|---:|---:|---:|---:|---:|
| `forward-200wpm-sustained` | ✅ PASS | edit_distance | 5 | 0.41 % | 86 704 | 1 631 | 52 | 60 | 65 |
| `edit-1-inline-tone-fix-vieet-bs-jt` | ✅ PASS | edit_distance | 0 | 0.00 % | 861 | 7 | 54 | 58 | 58 |
| `edit-2-cross-word-bs-vieejt-nam-bs4-s` | ✅ PASS | edit_distance | 0 | 0.00 % | 1 239 | 14 | 53 | 58 | 58 |

## Observations

1. **Sustained dimension is unaffected by the spike.** Removing the three hook-thread mutex acquisitions did not change forward typing accuracy, edit-path accuracy, or hook L1 timing in any meaningful way. Sustained at 50 ms inter-key is fully tolerant of the lock-free hook path.

2. **No latency improvement either.** L1 mean and p99 are within ±2 ms of D3 anchor. The lock was not measurably costing any latency on the hot path — consistent with the chaos baseline observation that the engine had ample headroom under the 300 ms `LowLevelHooksTimeout`.

3. **This is the safety-check side of D4.** The chaos result determines whether the spike fixes anything (Outcome A vs B). The sustained result determines whether the spike *broke* anything (Outcome C). Sustained passing here rules out Outcome C, leaving Outcome A vs B to be decided by chaos. Chaos verdict (Outcome B — see `perf-baseline-d4-spike-chaos.md`) confirms mutex was not the bug source.
