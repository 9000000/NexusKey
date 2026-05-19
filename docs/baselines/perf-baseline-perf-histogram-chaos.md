# perf-baseline-perf-histogram-chaos

Phase 1 of the 2026-05-19 architecture review design
(`docs/plans/2026-05-19-architecture-review-design.md` §Phase 1). 8-bucket
log-scale per-stage histogram + hidden `[debug] perf_histogram` toggle +
stack-only RAII `Perf::Scope` instrumentation wired into 7 hook-pipeline
stages.

This baseline locks in the Release p99 reference Phase 2 (single-writer
composition state) must beat. Captured the same day as a pre-Phase-1
verification bisect against the commit immediately before Phase 1 work
started (`2af9454`); both legs ran on identical hardware, the same Release
flags, and within ~30 minutes of each other to control for ambient system
load.

Companion artefacts in this directory:
- `junit-baseline-perf-histogram-{host}.xml` — JUnit per host (run 2 of 3,
  the median pass; 5 hosts × 11 cases).
- `perf-baseline-perf-histogram-{host}.csv` — per-keystroke L1 timing
  capture matching the JUnit above.

## Capture environment

- App: **NexusKey Release build**, branch `feat/perf-histogram`, commit
  immediately before this baseline writeup landed.
- Comparison commit: `2af9454 docs(plans): architecture review design`
  (the design doc HEAD — last commit before any Phase 1 wiring landed).
- Driver: `tools\run-chaos.ps1 -Tag perf-hist-release-run{1,2,3}`
  (current branch) and `-Tag bisect-2af9454-run{1,2,3}` (verification
  worktree at `Z:\home\phatmt\code\nexuskey-2af9454`).
- Hosts: Notepad Win11 (RichEditD2DPT sent message), Notepad++ (Win32
  plain), Chrome omnibox (Win32 + Chromium bait), Discord (Electron
  split, sleep=6 ms), ChatGPT (Chromium textarea).
- Date: 2026-05-19.

## Verdict — 55 / 55 PASS, no regression from 2af9454

| Capture | Hosts | Cases / host | Total |
|---|---|---|---|
| `feat/perf-histogram` Release × 3 runs | 5 | 11 | **165 ✅** |
| `2af9454` Release × 3 runs (bisect) | 5 | 11 | **165 ✅** |

330/330 functional tests pass across the bisect sweep. Zero regressions.

## L1 hook timing — chaos.toml worst-case p99 per host (3-run median)

Each cell is the **worst-case p99 across all 11 cases**, then **median
across 3 chaos runs** per host. Multi-run median is used because
Electron/Chromium hosts historically show ±5–10 ms single-run variance;
single-run comparison would not be statistically distinguishable from
noise.

| Host | 2af9454 median p99 | `feat/perf-histogram` median p99 | Δ |
|---|---:|---:|---:|
| Notepad (Win11 RichEdit) | 21 ms | **18 ms** | **−3 ms** |
| Notepad++ (Win32 plain) | 25 ms | **21 ms** | **−4 ms** |
| Chrome omnibox | 30 ms | **30 ms** | **0 ms** (flat) |
| Discord (Electron) | 33 ms | **33 ms** | **0 ms** (flat) |
| ChatGPT (Chromium textarea) | 27 ms | **27 ms** | **0 ms** (flat) |

**Phase 1 is net better or flat on every host.** `PERF_SCOPE` overhead
with the histogram gate OFF (default state) compiles to one atomic load
+ one branch + one zero-initialised `time_point` per stage — ~50 ns
across all 7 stages per keystroke, well below chaos.toml's
millisecond-resolution grain. The earlier single-run impression of
"+7 ms on Chrome / +35 ms on Discord" was noise: 2af9454 run 1 had
Chrome p99 = 33 ms, run 2 = 30 ms, run 3 = 27 ms — the variance was
present pre-Phase-1.

### Per-run breakdown (worst-case p99 per host)

| Host | 2af9454 run1 / run2 / run3 | current run1 / run2 / run3 |
|---|---|---|
| Notepad | 21 / 18 / 22 | 18 / 17 / 20 |
| Notepad++ | 25 / 23 / 26 | 20 / 22 / 21 |
| Chrome | 33 / 30 / 27 | 30 / 32 / 29 |
| Discord | 31 / 33 / 33 | 35 / 33 / 33 |
| ChatGPT | 27 / 30 / 27 | 27 / 26 / 27 |

### Tier 2 budget posture

Two hosts still ride at or above the Rule 11.1 Tier 2 30 ms p99 target:
**Chrome (30 ms)** and **Discord (33 ms)**, both on case 3.3 (engine
stress) or 6.1 (autocap). Both are pre-existing — they hit the same
range on 2af9454 — and well below the 100 ms hard ceiling
(`LowLevelHooksTimeout/3`). These remain the natural targets for Phase 2
to improve once the single-writer refactor opens new optimisation
surface.

## Source-level invariants

- Linux GTest **1789 / 1789** PASS (14 new `PerfHistogramTest` cases +
  1 new `SharedStateTest.DiagFlags_PerfHistogramBit_Roundtrip`).
- Existing audit `tools/audit/check_hook_thread_no_mutex.sh` still passes
  — `PerfHistogram::Record` is lock-free (single `fetch_add`), the only
  mutex inside `PerfHistogram.cpp` (`g_flushMutex`) is taken on the
  200 ms tick path (`OnTickPoll`), never on the LL hook thread.
- `Perf::Scope` is stack-only RAII (Rule 11.2) — zero heap allocation on
  the hot path.

## Notes for future captures

- Compile flag `VKEY_PERF_HIST` is **ON by default** for Release builds
  (and Debug). The runtime gate is OFF by default (`diagFlags` bit 0).
  Users opt in via `[debug]\nperf_histogram = true` in `config.toml`.
  When OFF, per-keystroke overhead is ~50 ns and undetectable here.
- When ON, expect ~150–300 ns per keystroke for the `clock::now()` pair
  + atomic counter increment across 7 stages. Still well below the
  chaos.toml millisecond grain.
- Phase 2 (single-writer composition state) verify gate per the design
  doc: "Phase 1 histogram shows p99 not regressed". The medians above
  are the reference points.
- 2af9454 bisect was run from a temporary worktree (`Z:\home\phatmt\code
  \nexuskey-2af9454`) and removed after capture. Re-run by checking out
  `2af9454`, building Release, and pointing `run-chaos.ps1` at it.
