# Perf Baseline — Sustained D5 atomic `vietnameseMode_` migration

**Captured:** 2026-05-04 by Phat (Windows host).
**NexusKey runtime SHA:** `f1f514b` + uncommitted D5 (`vietnameseMode_` migrated to `std::atomic<bool>` with acquire/release at 13 sites). D4 spike (3 commented `lock_guard` lines) remains in place.
**Test runner SHA:** `f1f514b`.
**Anchor:** `perf-baseline-d4-spike-sustained.md` (D4 spike capture).
**Purpose:** confirm the atomic migration does not regress sustained dimension at realistic 50 ms inter-key.

## Summary — zero drift

| Case | D4 (spike) | D5 (atomic vnmode) | Δ |
|---|---|---|---|
| `forward-200wpm-sustained` | 5 chars / 0.41 % / mean 52 / p99 60 / max 65 | 5 chars / 0.41 % / mean 52 / p99 60 / max 63 | **byte-identical** verdict + error count; max −2 ms |
| `edit-1-inline-tone-fix-vieet-bs-jt` | 0 / 0.00 % / mean 54 / p99 58 | 0 / 0.00 % / mean 53 / p99 58 | identical verdict; mean −1 ms |
| `edit-2-cross-word-bs-vieejt-nam-bs4-s` | 0 / 0.00 % / mean 53 / p99 58 | 0 / 0.00 % / mean 53 / p99 59 | identical verdict; p99 +1 ms |

**All three cases are zero-regression** at realistic 50 ms inter-key. The atomic migration of `vietnameseMode_` is invisible to sustained-dimension behavior — exactly as expected for a single-byte primitive accessed off the hot critical path.

## Per-case result

| Case | Verdict | Mode | Err chars | Err % | Wall ms | L1 n | L1 mean | L1 p99 | L1 max |
|---|---|---|---:|---:|---:|---:|---:|---:|---:|
| `forward-200wpm-sustained` | ✅ PASS | edit_distance | 5 | 0.41 % | 86 400 | 1 631 | 52 | 60 | 63 |
| `edit-1-inline-tone-fix-vieet-bs-jt` | ✅ PASS | edit_distance | 0 | 0.00 % | 855 | 7 | 53 | 58 | 58 |
| `edit-2-cross-word-bs-vieejt-nam-bs4-s` | ✅ PASS | edit_distance | 0 | 0.00 % | 1 224 | 14 | 53 | 59 | 59 |

## Observations

1. **Sustained dimension is unaffected by the atomic migration.** Replacing the plain `vietnameseMode_` reads with `.load(std::memory_order_acquire)` did not change forward typing accuracy (5 / 1 631 chars in error, identical to D1 / D2 / D3 / D4), edit-path accuracy (0 errors on both cases), or hook L1 timing in any meaningful way.

2. **No latency cost.** L1 mean and p99 are within ±1 ms of D4 anchor. Atomic load/store on a `bool` is one MOV instruction on x86 — same as plain access. The acquire/release fences only serialize against other atomic operations on the same address, which there are none of in the hook hot path itself.

3. **This is the safety-check side of D5.** The chaos result (`perf-baseline-d5-atomic-vnmode-chaos.md`) determines whether the migration changed engine state-machine behavior under burst input (it didn't, modulo heisenbug envelope). The sustained result here rules out the inverse failure mode: that the migration broke realistic typing despite leaving chaos verdicts intact. Sustained 3/3 PASS confirms D5 is safe to commit.

## Implications

D5 establishes the migration pattern is invisible at sustained pace. D5.x extensions (other primitive flags) inherit this property — no further sustained-dimension verification needed for additional primitive migrations on the same hot path, only chaos delta + L1 worst-case re-check.
