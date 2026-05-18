# Perf baseline — v3 hotkey unification + cleanup (2026-05-18)

**Commit:** `9748f98 fix(hotkey): mirror cancel-composition state into esc_restore_raw for TSF`
**Run tag:** `release-baseline`
**Build:** Release / x64 / MSVC 2022
**Corpus:** `tools/VKeyTestRunner/corpus/chaos.toml` (11 cases)
**Driver:** `run-chaos.ps1` post-PID-resolved-hook-log fix (d65d958)

Captured after the 3-commit cleanup sequence landed:
- `d2d2fd7` — drop dead legacy hotkey atomics + TypingConfig fields
- `9b2dec4` — ToggleEnabled non-modifier dispatch fix
- `9748f98` — TSF `esc_restore_raw` mirror inside `SaveHotkeyRegistry`

## Summary

| Host | Tests | Failures | Worst case (p99) |
|------|-------|----------|------------------|
| notepad   | 11 | 0 | 1.1-ghost-hoaf-bs-t @ 22ms |
| notepadpp | 11 | 0 | 6.1-autocap-binh-thuongf @ 31ms |
| chrome    | 11 | 0 | 6.1-autocap-binh-thuongf @ 31ms |
| discord   | 11 | 0 | 6.1-autocap-binh-thuongf @ 55ms |
| gpt       | 11 | 0 | 6.1-autocap-binh-thuongf @ 43ms |

**Total: 55/55 PASS. Hard ceiling 100ms not violated (worst 55ms).**

## Observations

1. **No functional regression** — HotkeyRegistry refactor (single registry replacing 3 legacy atomics) preserves chaos behavior across all 5 hosts.
2. **Non-Electron hosts** (notepad / notepadpp / chrome typing) — Release p99 ≈ 16-31ms, within Rule 12 Tier 2 budget (30ms p99) or near edge.
3. **Electron-class hosts** (discord native, gpt ChatGPT tab) — Release shows 40-55ms p99 on autocap + engine-stress cases. Consistent with the existing 100ms settle-window pattern in `docs/baselines/perf-baseline-d12-chrome-cross-app.md`. Wall-clock timing dominated by IPC reorder margin, not engine CPU.

## Compared to d6-rcu-config baseline (single-host notepad)

| Case | d6 p99 | This run (notepad) p99 |
|------|--------|------------------------|
| 1.1-ghost-hoaf-bs-t | 15ms | 22ms |
| 3.3-engine-stress-truongf | 14ms | 11ms |
| 5.3-cross-word-bs | 8ms | 12ms |
| 6.1-autocap-binh-thuongf | 18ms | 16ms |

Mixed deltas in noise range; HotkeyRegistry refactor adds 1 atomic shared_ptr load per HandleCommitUndo and 1 per HandlePreDispatch — sub-microsecond, not visible in chaos p99.

## Files

- `perf-baseline-9748f98-v3-cleanup-{host}.csv` — per-case L1 timing CSVs
- `junit-baseline-9748f98-v3-cleanup-{host}.xml` — JUnit verdict reports
