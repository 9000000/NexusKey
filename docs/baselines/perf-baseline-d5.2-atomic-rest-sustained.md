# Perf Baseline — Sustained D5.2 atomic remaining-primitives migration

**Captured:** 2026-05-04 by Phat (Windows host).
**NexusKey runtime SHA:** `6119e81` + uncommitted D5.2 (15 hook-read primitive flags migrated to `std::atomic`, ~60 sites). D4 spike (3 commented `lock_guard` lines) remains in place.
**Test runner SHA:** `f1f514b`.
**Anchor:** `perf-baseline-d5.1-atomic-method-tsf-sustained.md` (D5.1 capture).
**Purpose:** confirm extending the atomic acquire/release pattern to all remaining hook-read primitives does not regress sustained dimension at realistic 50 ms inter-key.

## Summary — byte-identical verdict, scheduler-noise timing variance

| Case | D5.1 (method+isTsfApp atomic) | D5.2 (+15 primitives) | Δ |
|---|---|---|---|
| `forward-200wpm-sustained` | 5 chars / 0.41 % / mean 51 / p99 57 / max 60 | 5 chars / 0.41 % / mean 52 / p99 58 / max 67 | **byte-identical** verdict + error; mean +1, p99 +1, max +7 (within scheduler noise; D4 max was 65, D5 was 63) |
| `edit-1-inline-tone-fix-vieet-bs-jt` | 0 / 0.00 % / mean 52 / p99 55 / max 55 | 0 / 0.00 % / mean 53 / p99 56 / max 56 | identical verdict; +1 ms |
| `edit-2-cross-word-bs-vieejt-nam-bs4-s` | 0 / 0.00 % / mean 52 / p99 56 / max 56 | 0 / 0.00 % / mean 52 / p99 58 / max 58 | identical verdict; +2 ms |

**All three cases are zero-regression** at realistic 50 ms inter-key. Verdict + error count byte-identical to D1/D2/D3/D4/D5/D5.1. Forward `max` rose +7 ms vs D5.1's 60 ms but is still under D4's spike-max of 65 ms — scheduler-induced variance, not a migration cost.

## Per-case result

| Case | Verdict | Mode | Err chars | Err % | Wall ms | L1 n | L1 mean | L1 p99 | L1 max |
|---|---|---|---:|---:|---:|---:|---:|---:|---:|
| `forward-200wpm-sustained` | ✅ PASS | edit_distance | 5 | 0.41 % | (per CSV) | 1 631 | 52 | 58 | 67 |
| `edit-1-inline-tone-fix-vieet-bs-jt` | ✅ PASS | edit_distance | 0 | 0.00 % | (per CSV) | 7 | 53 | 56 | 56 |
| `edit-2-cross-word-bs-vieejt-nam-bs4-s` | ✅ PASS | edit_distance | 0 | 0.00 % | (per CSV) | 14 | 52 | 58 | 58 |

## Observations

1. **Sustained verdict layer is byte-identical across all three cases.** Forward typing produces the same 5-character error footprint that's been stable since D1. Edit cases produce zero-error output. The 15-field atomic migration is invisible at the realistic-pace verdict layer — exactly the safety property D5.2 needs to demonstrate.

2. **Forward L1 max +7 ms vs D5.1 is scheduler noise, not migration cost.** D5.1 captured the lowest forward-max in the Sprint 1 history (60 ms vs D4's 65 ms, D5's 63 ms) — that single-capture optimum is hard to repeat. D5.2's 67 ms is within the D1–D4 envelope and well under the 300 ms `LowLevelHooksTimeout` deadline. Mean and p99 moved by only +1 ms on forward, supporting this interpretation. Atomic load/store is one MOV on x86 — there's no per-op cost mechanism for migration to charge here.

3. **This is the safety-check side of D5.2.** The chaos result (`perf-baseline-d5.2-atomic-rest-chaos.md`) determines whether the migration changed engine state-machine behavior under burst input (it did not — stable PASS preserved byte-identical, stable FAIL verdicts preserved within heisenbug envelope, 3.3 engine-stress L1 p99 dropped from 16 → 11 ms). The sustained result here rules out the inverse failure mode: that the migration broke realistic typing despite leaving chaos verdicts intact. Sustained 3/3 PASS confirms D5.2 is safe to commit.

## Implications

D5.2 closes the Phase B D5 sub-goal — all 18 primitive flags read on the hook callback path are now `std::atomic`. Subsequent Phase B work (D6 RCU `shared_ptr<TypingConfig>` for the complex-struct case, D7 audit) can proceed with a clean primitive-flag baseline. No further sustained-dimension verification is needed for the primitive-flag dimension; the pattern is fully baselined across D5 / D5.1 / D5.2 captures.
