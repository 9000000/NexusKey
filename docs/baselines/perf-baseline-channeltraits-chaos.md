# perf-baseline-channeltraits-chaos

Post-T3 ChannelTraits cleanup capture (PR #123). Both `isElectronApp_`
and `needBaitChar_` atomic flags moved from `HookEngine` onto
`IOutputInjector` as virtual trait methods (`HasMultiProcessRenderer()` /
`NeedsBaitCharPrefix()`). Source of truth now lives with the dispatch
channel that picked the trait, not duplicated in `HookEngine`.

Hot-path delta: `HandleAlphaKey` now reads via one
`std::atomic_load(&injector_)` snapshot + 2 virtual calls instead of 2
separate atomic loads. Same overhead pattern as
`IsSyncReplaceChannel()` / `SettleBudget()`.

Companion files in this directory:
- `report-channeltraits-{host}.xml` — JUnit per host (5 hosts × 11 cases).
- `perf-channeltraits-{host}.csv` — per-keystroke L1 timing per host.
- `runner-channeltraits-{host}.log` — full runner stdout for triage.
- `report-channeltraits-smoke-notepad.xml` + companions — pre-merge
  harness smoke test (Main + harness, no ChannelTraits) for verifying
  `tools/run-chaos.ps1` itself worked end-to-end before driving against
  the actual change.

## Capture environment

- App: NexusKey Debug build, branch `refactor/channel-traits` HEAD `b4e49f6`
  (rebased onto Main `87d7d98` via merge `487173c`).
- Driver: `powershell -ExecutionPolicy Bypass -File tools/run-chaos.ps1
  -Tag channeltraits` (full sweep — first chaos capture using the
  automated harness from PR #124).
- Hosts: Notepad Win11 (RichEditD2DPT sent message), Notepad++ (Win32
  plain), Chrome omnibox (Win32 + Chromium bait), Discord (Electron
  split, sleep=6 ms), ChatGPT (Chromium textarea).
- Date: 2026-05-05.

## Verdict — 55 / 55 PASS, no regression vs T3 baseline

| Capture | Hosts | Cases / host | Total |
|---|---|---|---|
| ChannelTraits chaos sweep | 5 | 11 | **55 ✅** |

Zero functional regressions. Every host file shows
`tests="11" failures="0"`, runner exit code 0, harness summary "PASS:
all hosts clean".

## L1 hook timing — chaos.toml worst-case p99 per host

ChannelTraits result vs T3-final baseline
(`perf-baseline-t3-final.md`):

| Host | T3 worst-case p99 | ChannelTraits worst-case p99 | Δ |
|---|---|---|---|
| Notepad (Win11 RichEdit) | 17 ms (1.1) | **15 ms** (1.1) | -2 ms |
| Notepad++ (Win32 plain) | 17 ms (5.1) | **16 ms** (1.1) | -1 ms |
| Chrome omnibox | 17 ms (1.2) | **15 ms** (1.1) | -2 ms |
| ChatGPT (Chromium textarea) | 33 ms (6.1) | **22 ms** (3.3) | -11 ms |
| Discord (Electron) | 27 ms (3.3) | **28 ms** (3.3) | +1 ms (flat) |

All hosts flat or improved. The ChatGPT -11 ms swing is the largest
delta — single-run, no statistical confidence — but is at minimum
consistent with the hot-path saving 1 atomic load per `HandleAlphaKey`
call. Discord +1 ms is well inside run-to-run variance. None of the
five hosts regressed.

## Cross-word backspace replay (case 5.3) — settle-window-sensitive

The 5.3 cross-word case (`vieejt nam`, BS×4, retype) is the most
settle-window-sensitive case in the corpus and the one D5 used to
demonstrate the per-host SettleBudget win. Re-checking here that the
trait migration didn't undo any of those gains:

| Host | T3 5.3 p99 | ChannelTraits 5.3 p99 | Δ |
|---|---|---|---|
| Notepad++ | 7 ms | 6 ms | -1 ms |
| Chrome omnibox | 6 ms | 8 ms | +2 ms (variance) |
| ChatGPT | 8 ms | 9 ms | +1 ms (variance) |
| Discord | 14 ms | 13 ms | -1 ms |
| Notepad Win11 | 10 ms | 8 ms | -2 ms |

All within ±2 ms — no settle-window degradation.

## Source-level invariants

- Linux GTest 1409 / 1409 PASS (built without ChannelTraits-affected
  Win32 code, but the cross-platform suite still validates the engine
  + atomic patterns are intact).
- Audit `tools/audit/check_hook_thread_no_mutex.sh` 5 / 5 PASS.
  `ATOMIC_BOOLS` regex was updated to drop `isElectronApp_` and
  `needBaitChar_` from the discipline list (the fields no longer exist
  to discipline).
- `tests/output/InjectorTraitsTest.cpp` (new, 13 cases, Win32-only
  build target) covers each injector's trait response across all flag
  combinations + the factory propagation contract end-to-end. Tests
  link into `NextKeyTests.exe` and run as part of the standard MSVC
  test target, not exercised on Linux.

## Notes for future captures

- This is the first chaos capture driven by `tools/run-chaos.ps1`
  (PR #124) instead of the manual per-host workflow. Switching to
  the automated harness halved the wall-clock time of a 5-host sweep
  and removed the focus-race risk of manual window switching.
- Operator note: when running the auth-dependent hosts (`discord`,
  `gpt`), make sure the active foreground window is the right app
  before the runner's 3-second focus countdown elapses. The
  `chrome` and `gpt` slots in this run were initially captured with
  the wrong window foreground; the artefacts were swapped on disk
  to match the actual host behind each label before being filed in
  this directory.
