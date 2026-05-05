# Perf Baseline — Chaos pre-spike snapshot @ commit `a28f1ea`

**Captured:** 2026-05-04 by Phat (Windows host).
**NexusKey runtime SHA:** `43fb4c1` (unchanged — branch added docs/corpus/baselines only since master).
**Test runner SHA:** `a28f1ea` (Sprint 1 D2 commit).
**Purpose:** D3 pre-spike anchor. D4 spike result will compare against this snapshot.

## Source artifacts

* `perf-baseline-a28f1ea-pre-spike-chaos.csv`
* `junit-baseline-a28f1ea-pre-spike-chaos.xml`
* Corpus: `tools/NextKeyTestRunner/corpus/chaos.toml` (11 cases, unchanged from D7)

## Summary

| Metric | Value | Locked baseline `43fb4c1` | Drift |
|---|---|---|---|
| **PASS / FAIL** | **4 / 7** | 5 / 6 | +1 FAIL (heisenbug) |
| L1 hook p99 (worst) | 16 ms (case 1.1) | 16 ms | identical |
| Stable PASS set | {1.1, 1.3, 5.1} | (subset of 5) | unchanged |
| Stable FAIL set | {2.1, 2.3, 3.3} | (subset of 6) | unchanged |
| Flip-prone set | {1.2, 5.2, 6.1} | — | expanded vs prior assumption |

## Per-case result

| # | Case | Verdict | Actual | L1 mean | L1 p99 | L1 max |
|---|---|---|---|---:|---:|---:|
| 1.1 | ghost-hoaf-bs-t | ✅ PASS | `hot` | 13 | 16 | 16 |
| 1.2 | tone-ghost-toans-bs3-i | ❌ **FAIL** | `in` | 8 | 12 | 12 |
| 1.3 | escape-bs-aa-b | ✅ PASS | `b` | 9 | 12 | 12 |
| 2.1 | x2-space-vieejt-nam | ❌ FAIL | `ệiet nam` | 4 | 7 | 7 |
| 2.2 | word-boundary-xin-chao-ban | ❌ FAIL | `xiàạnhao ban` | 3 | 10 | 10 |
| 2.3 | en-vn-transition-hello-vieejt | ❌ FAIL | `helệo viet` | 4 | 10 | 10 |
| 3.3 | engine-stress-truongf | ❌ FAIL | `tờương` | 4 | 12 | 12 |
| 5.1 | case-tracking-Giar | ✅ PASS | `Giả` | 6 | 10 | 10 |
| 5.2 | vowel-start-uongs | ✅ **PASS** | `uống` | 7 | 11 | 11 |
| 5.3 | cross-word-bs-vieejt-nam-bs4-s | ❌ FAIL | `ết n` | 4 | 9 | 9 |
| 6.1 | autocap-binh-thuongf | ❌ **FAIL** | `bình tươờng` | 8 | 12 | 12 |

## Diffs vs locked baseline `perf-baseline-43fb4c1.md`

```
1.2  flip PASS -> FAIL  (actual: in       — was: ti expected, locked PASS)
5.2  flip FAIL -> PASS  (actual: uống     — was: ốngg)
6.1  flip PASS -> FAIL  (actual: bình tươờng — was: bình thường expected, locked PASS)
2.2  composition shift  (actual: xiàạnhao ban — was: xàạnchao ban)
5.3  composition shift  (actual: ết n     — was: etết)
```

Cases 2.1, 2.3, 3.3 produce byte-identical corruption to locked baseline (truly stable FAIL set). Cases 1.1, 1.3, 5.1 PASS identically (truly stable PASS set).

## Observations

1. **Count drift is heisenbug variance, not regression.** Runtime SHA `43fb4c1` unchanged between locked baseline (April 2026) and this capture (2026-05-04). The verdict differences are caused by engine state non-determinism under sub-ms input — already documented in HANDOFF "Heisenbug variance" section. Empirically expanded to include case **1.2** (was assumed stable PASS; this capture reveals it's also flip-prone).

2. **Stable cases are the meaningful regression-detection targets.** {2.1, 2.3, 3.3} produce identical corruption every run (`ệiet nam`, `helệo viet`, `tờương`). These are deterministic engine bugs — debuggable cases for future TelexEngine fixes (post Sprint 1, per plan Phase D outcome B). {1.1, 1.3, 5.1} PASS identically every run.

3. **L1 timing is identical to locked baseline.** Same p99 16 ms worst case. No timing regression between April 2026 and 2026-05-04 — the 4-vs-5 PASS count is verdict-level only, not latency-level.

4. **D4 spike comparison anchor.** When D4 (drop 3 hook-thread mutex acquisitions) result is captured, comparison should be:
   - **Stable PASS regression**: any of {1.1, 1.3, 5.1} → FAIL on D4 = high-confidence regression caused by spike.
   - **Stable FAIL flip**: any of {2.1, 2.3, 3.3} → PASS on D4 = high-confidence improvement caused by spike.
   - **Flip-prone changes**: {1.2, 5.2, 6.1} verdict swaps are LOW confidence — could be heisenbug noise, not spike effect.
   - **Composition shifts** on 2.2, 5.3 are noise unless verdict flips.

## Exit gate (for D4 evaluation)

D4 spike result evaluated against this snapshot must show:

- **No regression on stable PASS set** {1.1, 1.3, 5.1} — these MUST remain PASS.
- **No timing regression** — L1 p99 ≤ 18 ms (2 ms headroom over current 16 ms).
- **Verdict flip on at least one stable FAIL** {2.1, 2.3, 3.3} for high-confidence "spike fixes a bug" claim. Without that, the spike is "neutral or noise" — Phase B refactor proceeds for code-health value alone (per Sprint 1 plan §A decision gate outcome B).

This file is **NOT frozen** — it's the snapshot for D4 comparison only. After D4, both D3 and D4 baselines are referenced by the D4 commit and the D12 final gate evaluation.
