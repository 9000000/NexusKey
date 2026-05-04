# Perf Baseline — Sustained D6 RCU `shared_ptr<TypingConfig>`

**Captured:** 2026-05-04 by Phat (Windows host).
**NexusKey runtime SHA:** `d25ef82` + uncommitted D6 (`config_` → `std::atomic<std::shared_ptr<const TypingConfig>>`, 7 sites). D4 spike (3 commented `lock_guard` lines) remains in place.
**Test runner SHA:** `f1f514b`.
**Anchor:** `perf-baseline-d5.2-atomic-rest-sustained.md` (D5.2 capture).
**Purpose:** confirm the RCU migration of `TypingConfig` does not regress sustained dimension at realistic 50 ms inter-key.

## Summary — byte-identical verdict, slight L1 improvement

| Case | D5.2 (15 primitives atomic) | D6 (+ TypingConfig RCU) | Δ |
|---|---|---|---|
| `forward-200wpm-sustained` | 5 chars / 0.41 % / mean 52 / p99 58 / max 67 | 5 chars / 0.41 % / mean 52 / p99 58 / max 66 | **byte-identical** verdict + error; max −1 ms |
| `edit-1-inline-tone-fix-vieet-bs-jt` | 0 / 0.00 % / mean 53 / p99 56 / max 56 | 0 / 0.00 % / mean 52 / p99 55 / max 55 | identical verdict; mean / p99 / max all −1 ms |
| `edit-2-cross-word-bs-vieejt-nam-bs4-s` | 0 / 0.00 % / mean 52 / p99 58 / max 58 | 0 / 0.00 % / mean 52 / p99 55 / max 55 | identical verdict; p99 / max −3 ms |

**All three cases are zero-regression** at realistic 50 ms inter-key. Verdict + error count byte-identical to the D1 → D5.2 history. L1 timing tightened by 1-3 ms across all cases — the RCU pattern adds no measurable cost on the realistic-pace hook path.

## Per-case result

| Case | Verdict | Mode | Err chars | Err % | Wall ms | L1 n | L1 mean | L1 p99 | L1 max |
|---|---|---|---:|---:|---:|---:|---:|---:|---:|
| `forward-200wpm-sustained` | ✅ PASS | edit_distance | 5 | 0.41 % | (per CSV) | 1 631 | 52 | 58 | 66 |
| `edit-1-inline-tone-fix-vieet-bs-jt` | ✅ PASS | edit_distance | 0 | 0.00 % | (per CSV) | 7 | 52 | 55 | 55 |
| `edit-2-cross-word-bs-vieejt-nam-bs4-s` | ✅ PASS | edit_distance | 0 | 0.00 % | (per CSV) | 14 | 52 | 55 | 55 |

## Observations

1. **Sustained verdict is byte-identical across the full Sprint 1 migration history.** Forward typing produces the same 5-character error footprint that's been stable since D1. Edit cases produce zero-error output. Plain assignment (D3) → atomic primitives (D5/D5.1/D5.2) → atomic shared_ptr complex struct (D6) — none of the migrations have shifted the realistic-pace verdict layer. This is exactly the safety property the migrations need to demonstrate: structural-correctness changes that are invisible to the typing experience.

2. **L1 timing tightened on all three cases.** Forward max −1 ms; edit-1 mean / p99 / max all −1 ms; edit-2 p99 / max −3 ms. The RCU pattern's per-call cost (refcount bump + internal lock acquisition) is small enough that it disappears into scheduler noise on the realistic-pace path, and may be incidentally helping by removing cache-line contention with main-thread writers (consistent with the trajectory observed across D5 → D5.1 → D5.2). Forward p99 unchanged at 58 ms — same as D5.2.

3. **This is the safety-check side of D6.** The chaos result (`perf-baseline-d6-rcu-config-chaos.md`) determines whether RCU migration changed engine state-machine behavior under burst input (it did not at the verdict layer; one heisenbug variance observation at 3.3 p99 = 22 ms in the first capture was not reproducible on re-run). The sustained result here rules out the inverse failure mode: that the migration broke realistic typing despite leaving chaos verdicts intact. Sustained 3/3 PASS with byte-identical errors confirms D6 is safe to commit.

## Implications

D6 closes Phase B's foundation refactor. All hook-read state on `HookEngine` (18 primitive flags + 1 complex struct) is now Rule #11.3-compliant. The pattern is fully baselined across D5 / D5.1 / D5.2 / D6 captures. D7 (audit script) is the next Phase B step — the script can now formalize the guarantee that hook-callback paths use neither `stateMutex_` nor plain non-atomic state access.
