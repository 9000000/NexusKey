# Perf Baseline — Sustained Edit @ commit `3459642`

**Captured:** 2026-05-04 by Phat (Windows host).
**NexusKey runtime SHA:** `43fb4c1` (master `Add self healing hook`) — same Phase 0a state measured by the chaos and sustained-forward baselines. The branch `refactor/phase-1-single-owner` adds tooling, docs, and corpus only; no runtime change yet.
**Test runner SHA:** `c75db8a` (`Sprint 1 D1: lock forward-200wpm sustained baseline`). Branch HEAD when this run was captured. Sustained-edit corpus cases drafted on top, uncommitted at the moment of run; commit follows in `Sprint 1 D2: lock sustained-edit baseline`.

This file is the **frozen reference** for the *sustained-edit* dimension that Phase 1+ refactor work measures itself against. It complements:
* `perf-baseline-43fb4c1.md` — chaos corpus (binary correctness on race-prone short cases at 1–5 ms inter-key).
* `perf-baseline-3459642-sustained-forward.md` — sustained forward (200-word continuous typing at 50 ms inter-key).

Sustained-edit closes the gap between chaos (race pressure) and sustained-forward (no edits): it captures user-perceptible smoothness on **edit paths** (typo correction, cross-word backspace) at realistic typing pace.

## Source artifacts

* `perf-baseline-3459642-sustained-edit.csv` — per-case CSV (canonical numeric data; includes the forward case as a co-run sanity check)
* `junit-baseline-3459642-sustained-edit.xml` — JUnit XML (CI-friendly verdict)
* Corpus: `tools/NextKeyTestRunner/corpus/sustained.toml` (3 cases: 1 forward + 2 edit; this baseline focuses on the 2 edit cases)

## Reproduction

```powershell
# 1. Build
cmake --build build --config Debug --target NextKeyTestRunner
cmake --build build --config Debug --target NexusKey

# 2. Start NexusKey debug
.\build\Debug\NexusKey.exe

# 3. Open a fresh empty Notepad

# 4. From a separate console (PowerShell)
.\build\tools\Debug\NextKeyTestRunner.exe `
  --corpus    tools\NextKeyTestRunner\corpus\sustained.toml `
  --hook-log  build\Debug\NexusKey_hook.log `
  --junit     docs\baselines\junit-baseline-XXXX-sustained-edit.xml `
  --perf-csv  docs\baselines\perf-baseline-XXXX-sustained-edit.csv `
  --delay-ms=5000

# 5. Click Notepad within 5 s. Forward case runs ~87 s, edit cases <2 s each.
#    Stop NexusKey from the tray, press Enter to flush log buffer for L1.
```

## Summary

| Metric | Value |
|---|---|
| Edit cases | 2 (`edit-1-inline-tone-fix-vieet-bs-jt`, `edit-2-cross-word-bs-vieejt-nam-bs4-s`) |
| Verdict (both) | **PASS** at threshold 99 % |
| Error rate (both) | **0.00 %** (0 chars wrong) |
| Configured inter-key | 50 000 µs (50 ms; matches forward) |
| Wall-clock (edit-1) | 0.863 s |
| Wall-clock (edit-2) | 1.228 s |
| L1 inter-arrival mean (both) | 53 ms |
| L1 inter-arrival p99 | 58–59 ms |
| L1 inter-arrival max | 58–59 ms |

## Per-case result

| Case | Verdict | Mode | Err chars | Err % | Wall ms | L1 n | L1 mean | L1 p99 | L1 max |
|---|---|---|---:|---:|---:|---:|---:|---:|---:|
| `edit-1-inline-tone-fix-vieet-bs-jt` | ✅ PASS | edit_distance | 0 | 0.00 % | 863 | 7 | 53 | 58 | 58 |
| `edit-2-cross-word-bs-vieejt-nam-bs4-s` | ✅ PASS | edit_distance | 0 | 0.00 % | 1 228 | 14 | 53 | 59 | 59 |

Forward case ran in the same invocation (consistency check); its numbers (5 chars / 0.41 % / 86.8 s / 1 631 keys, mean 52 ms / p99 60 ms / max 67 ms) match D1 baseline `perf-baseline-3459642-sustained-forward.md` within ±80 ms scheduler noise. Zero drift confirms runtime SHA `43fb4c1` was unchanged between D1 and D2 captures.

## Observations

1. **Engine produces semantically exact output on both edit cases at 50 ms inter-key.** No corruption, no off-by-one, no tone drift. `edit-2`'s key sequence (`vieejt nam\b\b\b\bs`) is *byte-identical* to chaos `5.3-cross-word-bs-vieejt-nam-bs4-s`, which FAILs at 1 ms inter-key with output `etết` / `itết` (heisenbug variance — see `perf-baseline-43fb4c1.md` line 77 and HANDOFF "Heisenbug variance" section). At 50 ms the same sequence produces `viết` cleanly. Cross-engine + cross-version check on 2026-05-04: EVKey runs the same chaos corpus on the same host at 1 ms pace at 11 / 11 PASS, AND **NexusKey v2.1 (older release of this same project)** runs the same chaos corpus at PASS-clean per Phat's test. The chaos failures are therefore **regressions introduced by feature work between v2.1 and current**, not a pace-induced limitation. Sprint 1 single-owner refactor's value is restoring the architectural stability v2.1 had — disentangling shared mutable state so feature additions stop silently regressing prior chaos behavior. See HANDOFF "Cross-engine + cross-version check" for the full methodology note.

2. **Edit-path L1 timing is indistinguishable from forward.** Mean 53 ms ≈ configured 50 ms + ~3 ms driver/scheduler overhead. p99 max 59 ms vs forward's 60 ms. No edit-specific latency penalty observable at this pace. The edit code path (BS handling, composition refresh, tone re-apply) executes within the same per-key budget as plain forward typing when the key arrival rate is realistic.

3. **The "≥ 30 % improvement" gate from the plan is unworkable for sustained-edit too.** Already at 0.00 % error — relative reduction is undefined. Plan needs revision: sustained-edit joins sustained-forward as a **no-regression anchor**. The interesting Sprint 1 movement signal lives in:
   - Chaos corpus FAIL flips (≥ 1 of 6, gate unchanged from `perf-baseline-43fb4c1.md`).
   - L1 timing tightening (single-owner eliminates mutex contention; expected to drop p99 from current 60 ms → < 50 ms post-D11).

4. **Sample size for edit cases is small (n=7, n=14).** Statistical power for detecting subtle regressions is limited compared to forward (n=1631). Future Sprint should consider:
   - A `mixed-edit-session` case (50 forward + 3 typo + 1 cross-word) for a denser edit-path sample, OR
   - Running edit cases multiple times in one corpus invocation, OR
   - Lowering `inter_key_us` to 10 000 (10 ms) for an "aggressive but human-reachable" edit case to bridge the gap toward chaos.
   - Decision deferred to Sprint 1.5 / V2 backlog; current 0.00 % baseline is sufficient for the no-regression Sprint 1 gate.

5. **`expected = <user-intent>` matched `expected = <actual>` on first run.** Per plan A0 D2 step 5, the cases are locked as drafted — no need to update `expected` to observed output. This is the "happy path" of D2 procedure; if any case had diverged from intent, plan step 6 would have parked it as a chaos candidate instead.

6. **The case design avoided overlap with chaos.** `edit-1` (inline tone correction `vieet\bjt`) is a new pattern not present in chaos. `edit-2` is the same key sequence as chaos 5.3 but at 50 ms vs 1 ms — by design, they measure orthogonal dimensions (race pressure vs human-paced edit) of the same engine behavior. This gives the Sprint 1 verifier a way to attribute regressions: a chaos 5.3 regression with no edit-2 regression points to mutex contention; a regression in BOTH points to a structural edit-path bug.

## Exit gate (revised)

Phase 1+ refactor work targeting the sustained-edit dimension MUST satisfy:

* **No regression**: error rate ≤ 0.00 % (was 0.00 % at master `43fb4c1`).
* **No hook regression**: L1 mean ≤ 54 ms, p99 ≤ 60 ms, max ≤ 60 ms (1 ms headroom over current).
* **Both edit cases continue to PASS at threshold 99 %.**
* **Engine output must remain byte-equal to current `expected`** (`việt` for edit-1, `viết` for edit-2). Any drift here is a behavioral regression even if edit_distance verdict still nominally passes.

The "≥ 30 % improvement" wording from the original Sprint 1 plan is **REMOVED for sustained-edit** — already at floor. Plan §A0 D2 will be amended in the Sprint 1 D2 commit to reflect this.

When Phase 1 lands, capture a fresh `perf-baseline-<new-sha>-sustained-edit.{csv,xml,md}` and append a comparison row to a future `docs/baselines/index.md`.
