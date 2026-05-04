# Perf Baseline — Sustained pre-spike snapshot @ commit `a28f1ea`

**Captured:** 2026-05-04 by Phat (Windows host).
**NexusKey runtime SHA:** `43fb4c1` (unchanged).
**Test runner SHA:** `a28f1ea`.
**Purpose:** D3 pre-spike anchor. D4 spike result will compare against this snapshot.

## Source artifacts

* `perf-baseline-a28f1ea-pre-spike-sustained.csv`
* `junit-baseline-a28f1ea-pre-spike-sustained.xml`
* Corpus: `tools/NextKeyTestRunner/corpus/sustained.toml` (3 cases: 1 forward + 2 edit, locked at D2)

## Summary — zero drift vs D1/D2

| Case | D1/D2 baseline | D3 capture | Drift |
|---|---|---|---|
| `forward-200wpm-sustained` | 5 chars / 0.41 % / mean 52 ms / p99 60 / max 67 | 5 chars / 0.41 % / mean 52 / p99 60 / max 63 | within ±5 ms (max −4 ms; p99 identical) |
| `edit-1-inline-tone-fix-vieet-bs-jt` | 0 chars / 0.00 % / mean 53 / p99 58 / max 58 | 0 chars / 0.00 % / mean 54 / p99 56 / max 56 | within ±2 ms |
| `edit-2-cross-word-bs-vieejt-nam-bs4-s` | 0 chars / 0.00 % / mean 53 / p99 59 / max 59 | 0 chars / 0.00 % / mean 53 / p99 59 / max 59 | identical |

All three cases match D1/D2 within scheduler noise. Sustained dimension is **stable across runs** — unlike chaos.

## Per-case result

| Case | Verdict | Mode | Err chars | Err % | Wall ms | L1 n | L1 mean | L1 p99 | L1 max |
|---|---|---|---:|---:|---:|---:|---:|---:|---:|
| `forward-200wpm-sustained` | ✅ PASS | edit_distance | 5 | 0.41 % | 86 728 | 1 631 | 52 | 60 | 63 |
| `edit-1-inline-tone-fix-vieet-bs-jt` | ✅ PASS | edit_distance | 0 | 0.00 % | 855 | 7 | 54 | 56 | 56 |
| `edit-2-cross-word-bs-vieejt-nam-bs4-s` | ✅ PASS | edit_distance | 0 | 0.00 % | 1 240 | 14 | 53 | 59 | 59 |

## Observations

1. **Sustained dimension is deterministic at realistic pace.** 50 ms inter-key gives the engine enough headroom to fully drain composition state between keystrokes. No heisenbug variance here — same input produces same output run-to-run.

2. **D4 spike evaluation — sustained gate.** D4 (drop 3 hook-thread mutex acquisitions) result evaluated against this snapshot must show:
   - **No regression on forward**: error rate ≤ 0.41 %.
   - **No regression on edit cases**: both must remain at 0.00 % error.
   - **No timing regression**: L1 p99 ≤ 62 ms forward, ≤ 60 ms edit (2 ms headroom).

3. **Sustained is the safety check for D4.** Chaos has heisenbug noise that obscures interpretation. Sustained is clean. If D4 introduces any sustained regression, the spike has broken something — abort and investigate before Phase B+.

This file is **NOT frozen** — it's the snapshot for D4 comparison only.
