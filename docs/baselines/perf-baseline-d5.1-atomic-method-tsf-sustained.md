# Perf Baseline — Sustained D5.1 atomic `currentMethod_` + `isTsfApp_` migration

**Captured:** 2026-05-04 by Phat (Windows host).
**NexusKey runtime SHA:** `6caa1ba` + uncommitted D5.1 (`currentMethod_` → `std::atomic<InputMethod>`, `isTsfApp_` → `std::atomic<bool>`, 19 sites). D4 spike (3 commented `lock_guard` lines) remains in place.
**Test runner SHA:** `f1f514b`.
**Anchor:** `perf-baseline-d5-atomic-vnmode-sustained.md` (D5 capture).
**Purpose:** confirm extending the atomic acquire/release pattern to `currentMethod_` + `isTsfApp_` does not regress sustained dimension at realistic 50 ms inter-key.

## Summary — slight improvement

| Case | D5 (vnmode atomic) | D5.1 (+method, +isTsfApp) | Δ |
|---|---|---|---|
| `forward-200wpm-sustained` | 5 chars / 0.41 % / mean 52 / p99 60 / max 63 | 5 chars / 0.41 % / mean 51 / p99 57 / max 60 | **byte-identical** verdict + error; mean −1, p99 −3, max −3 |
| `edit-1-inline-tone-fix-vieet-bs-jt` | 0 / 0.00 % / mean 53 / p99 58 / max 58 | 0 / 0.00 % / mean 52 / p99 55 / max 55 | identical verdict; p99 −3 |
| `edit-2-cross-word-bs-vieejt-nam-bs4-s` | 0 / 0.00 % / mean 53 / p99 59 / max 59 | 0 / 0.00 % / mean 52 / p99 56 / max 56 | identical verdict; p99 −3 |

**All three cases are zero-regression** at realistic 50 ms inter-key, and L1 timing tightened by ~2-3 ms across the board. The atomic migration of `currentMethod_` + `isTsfApp_` is invisible to sustained-dimension behavior at the verdict level, with secondary positive movement on hook latency.

## Per-case result

| Case | Verdict | Mode | Err chars | Err % | Wall ms | L1 n | L1 mean | L1 p99 | L1 max |
|---|---|---|---:|---:|---:|---:|---:|---:|---:|
| `forward-200wpm-sustained` | ✅ PASS | edit_distance | 5 | 0.41 % | 85 177 | 1 631 | 51 | 57 | 60 |
| `edit-1-inline-tone-fix-vieet-bs-jt` | ✅ PASS | edit_distance | 0 | 0.00 % | 845 | 7 | 52 | 55 | 55 |
| `edit-2-cross-word-bs-vieejt-nam-bs4-s` | ✅ PASS | edit_distance | 0 | 0.00 % | 1 213 | 14 | 52 | 56 | 56 |

## Observations

1. **Sustained dimension verdict is unchanged.** Forward: 5 chars / 0.41 % error byte-identical to D1 / D2 / D3 / D4 / D5. Edit-1 + edit-2: 0 / 0.00 % byte-identical. The atomic migration is invisible at the realistic-pace verdict layer, exactly as expected.

2. **L1 timing tightened by 2-3 ms across the board.** Forward p99 60 → 57; edit-1 p99 58 → 55; edit-2 p99 59 → 56. This matches the chaos-side observation (`perf-baseline-d5.1-atomic-method-tsf-chaos.md`) that worst-case p99 dropped from 18 → 16 ms. Atomic load/store has no per-op cost on x86, but removing the implicit cache-line contention with main-thread `stateMutex_`-protected writers seems to have reduced hot-path variability. Pillar #1 "Nhanh" benefits without an explicit perf goal change.

3. **This is the safety-check side of D5.1.** The chaos result determines whether the migration changed engine state-machine behavior under burst input (it did not — verdict count went up by 1 PASS via heisenbug flip; no PASS regressed). The sustained result here rules out the inverse failure mode: that the migration broke realistic typing despite leaving chaos verdicts intact. Sustained 3/3 PASS confirms D5.1 is safe to commit.

## Implications

D5.1 inherits the D5 property that the migration pattern is invisible at sustained pace, plus adds a secondary positive signal on L1 hot-path timing. Subsequent D5.2 extensions (remaining primitives) only need chaos delta + L1 worst-case re-check; sustained re-verification can be skipped as long as the worst-case p99 stays at or below the current 16 ms.
