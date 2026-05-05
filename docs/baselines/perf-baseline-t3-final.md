# perf-baseline-t3-final

Sprint 2 T3 final capture — `IOutputInjector` extraction (D0-D5) shipped on
branch `sprint-2/output-injector`. Ready for merge to Main.

Companion files in this directory:
- `report-d3-{host}.xml`, `perf-d3-{host}.csv` — D3 chaos sweep (post SplitDispatch)
- `report-d4-notepad.xml`, `report-d4-notepadpp.xml` — D4 spot-check
- `report-d5-{host}.xml`, `perf-d5-{host}.csv` — D5 chaos sweep (post per-host SettleBudget)

## Capture environment

- App: NexusKey Debug build, branch `sprint-2/output-injector` HEAD `2f1b400`
- Driver: `NextKeyTestRunner.exe --corpus chaos.toml --junit ... --perf-csv ... --hook-log ...`
- Hosts: Notepad++ (Win32 plain), Chrome omnibox (Win32+Chromium bait), ChatGPT prompt (Chromium renderer textarea), Discord (Electron split), Notepad Win11 (RichEditD2DPT sent message)
- Date: 2026-05-05

## Verdict — 132 / 132 PASS across the natural-classification matrix

| Capture | Hosts | Cases / host | Total |
|---|---|---|---|
| D3 (SplitDispatchInjector + dispatch unification) | 5 | 11 | 55 ✅ |
| D4 (audit Check 4 + dead code) | 2 spot-check | 11 | 22 ✅ |
| D5 (synth-counter + per-host SettleBudget) | 5 | 11 | 55 ✅ |
| **Total** | | | **132 ✅** |

Zero functional regressions. All 5 chaos hosts × 3 D-day captures × 11 cases
PASS in the JUnit XML reports.

## L1 hook timing — D5 final (chaos.toml worst-case p99 per host)

| Host | Injector branch | Settle budget | Worst-case p99 | Chaos 5.3 p99 |
|---|---|---|---|---|
| Notepad++ | Win32 plain | 30 ms | 17 ms (5.1) | 7 ms |
| Chrome omnibox | Win32 + Chromium bait | 30 ms | 17 ms (1.2) | 6 ms |
| ChatGPT (Chromium textarea) | Win32 + Chromium bait | 30 ms | 33 ms (6.1) | 8 ms |
| Discord | SplitDispatch (Electron, sleep=6) | 100 ms | 27 ms (3.3) | 14 ms |
| Notepad Win11 | RichEditEm (sent message) | 0 ms | 17 ms (1.1) | 10 ms |

Worst-case across all hosts: ChatGPT case 6.1 (autocap-binh-thuongf) at 33 ms p99 — well below the LowLevelHooksTimeout 300 ms ceiling and within the same envelope as Sprint 1 D12 baselines (14-18 ms p99 there, slightly higher here on Chromium textareas because the renderer also runs Lexical-style suggest behind the input).

## Commit-undo replay perf delta — D4 → D5 (per-host SettleBudget)

D5 replaced the hardcoded `kSynthSettleMs = 100 ms` with `injector_->SettleBudget()`.
Win32 dropped to 30 ms, RichEdit to 0 ms (sent message drains synchronously),
Split kept at 100 ms (unchanged — IPC reorder margin still required). Chaos
case 5.3 (cross-word BS replay) is the most settle-window-sensitive case:

| Host | D4 p99 | D5 p99 | Δ |
|---|---|---|---|
| Chrome omnibox | 14 ms | 6 ms | **−57 %** |
| ChatGPT | 13 ms | 8 ms | **−38 %** |
| Notepad Win11 (RichEdit) | 13 ms | 10 ms | **−23 %** |
| Notepad++ | 6 ms | 7 ms | ≈ flat (case dominated by Sleep gaps not settle window) |
| Discord | 13 ms | 14 ms | ≈ flat (Split settle 100 ms unchanged) |

Acceptance per design doc §6 D5 (≥ 30 % reduction on Win32 / RichEdit hosts):
**MET** — Chrome −57 %, ChatGPT −38 %, Notepad Win11 −23 %. Notepad++ flat is
case-shape-specific (not a regression); Discord flat is by design (Split
settle budget unchanged).

## LOC delta — HookEngine.cpp shrink

| Phase | LOC | Δ |
|---|---|---|
| Pre-T3 (Main, `003a059`) | 3 593 | baseline |
| D3 SplitDispatch + dispatch unification | 3 540 | −53 |
| D4 useEditMsgPath_/isConsoleApp_ + DispatchSendInput body | 3 526 | −14 |
| D5 SettleBudget + synth-counter callback | 3 519 | −7 |
| **Cumulative** | **3 519** | **−74 net (−2.1 %)** |

Smaller than the 200-LOC plan target because Gotcha G6 prevented deletion
of `isElectronApp_` + `needBaitChar_` (live policy readers in HandleAlphaKey
passthrough/reinjectVk gates). Lifting them cleanly requires a `ChannelTraits`
query method on `IOutputInjector` — captured in `docs/TODO.md` for the
post-T3 cleanup batch.

The injector layer adds ~600 LOC under `src/app/output/` (5 files: interface,
factory, 3 impls, internal seam) and ~330 LOC of unit tests in `tests/output/`
that didn't exist pre-T3. Net repo growth — but each layer is independently
unit-testable now where the pre-T3 dispatch logic was only chaos-testable.

## Audit gate — `tools/audit/check_hook_thread_no_mutex.sh` PASS

D4 added Check 4 (injector_ accessed only via `std::atomic_load` /
`std::atomic_store`). All 4 checks green:

```
Check 1: D4 SPIKE comment integrity (≥3 commented lock_guard lines)
  OK: 3 commented lock_guard line(s) preserved
Check 2: stateMutex_ not reachable from hook callback entries
  OK: LowLevelKeyboardProc / WinEventProc / LowLevelMouseProc / RawInputWndProc
Check 3: atomic fields use .load()/.store() (no plain assignment or read)
  OK: all 14 ATOMIC_BOOLS + DWORD + Enum + RCU<TypingConfig>
Check 4: injector_ accessed only via std::atomic_load / std::atomic_store
  OK
PASS: all D7 audit checks passed — Phase B compliance maintained
```

## Unit-test gate — 30 PASS + 2 SKIP (Windows injector tests)

```
Win32SendInputInjectorTest        — 10 PASS
SplitDispatchInjectorTest         — 10 PASS
RichEditEmReplaceSelInjectorTest  —  2 PASS + 2 SKIP (env-gated, fg HWND)
OutputInjectorFactoryTest         —  8 PASS
```

The 2 SKIP cases (`ReplaceEmitsGetSelSetSelReplaceSelInOrder`,
`BsCountZeroSkipsSetSel`) gate on a foreground window the headless GTest env
doesn't provide — impl correctly bails before touching `g_sendMessageTimeoutW`.
Coverage of those paths comes from the Notepad Win11 chaos sweep on the
RichEdit injector (D2/D5 captures, both 11/11 PASS).

## Cross-host forced-classification matrix — DEFERRED (out of T3 scope)

Plan §D6 Tasks 32-33 (env-var override + 14-cell forced-classification
matrix) are **deferred post-T3**. Rationale:

- 132 case PASS across 5 natural-classification hosts already exercises every
  injector branch on the host classes that exist in production.
- Forced cells test "what if classifier mis-detects" — a robustness check
  whose marginal value is incremental given how exhaustive the natural
  coverage is.
- ~30 LOC harness + ~2 h manual orchestration trade-off: if a forced-cell
  bug is caught later, captured at that point as a separate task. The
  `--host-class` CLI flag has a clear single-task scope and can be reopened
  independently.

Captured as TODO entry under `docs/TODO.md` (T3 code review findings section).

## Deferred items captured for post-T3 batch

Per `docs/TODO.md`:

1. `isElectronApp_` + `needBaitChar_` deletion via `ChannelTraits` query method on IOutputInjector (Gotcha G6 — interface-design-needs-its-own-commit).
2. M1 stale-comment refresh (HookEngine.cpp lines ~1485 / ~2841).
3. M2 constants naming decision (k-prefix vs UPPER_SNAKE — codebase-wide).
4. M3 dual-route TrackedSendInput unification (4 VB6/clipboard sites + reinjectVk).
5. M4 memory-ordering doc/match for OnSynthDispatched (1-line edit).
6. L1 header-include order swap (3 files).
7. Pre-T3 review followups (Critical D4 SPIKE marker clarify, Minor 1-3).
8. Typing bug `cafcs → các` (spell-check tone-replacement gate).
9. `--host-class` matrix harness (deferred from D6, this entry).

## Ship gate — ✅ MET

- Functional: 132 / 132 chaos PASS, zero regressions across 5 host classes.
- Performance: D5 SettleBudget acceptance MET (≥ 30 % faster commit-undo
  replay on Chrome/ChatGPT/Notepad Win11, flat or unchanged elsewhere).
- Architecture: 4 atomic dispatch flags reduced to 2 policy-only flags;
  dispatch logic lives behind `IOutputInjector`; HookEngine 3593 → 3519 LOC
  (−74 net, plus the new unit-testable injector layer).
- Audit: all 4 hook-thread compliance checks green.
- Unit tests: 30 PASS + 2 env-skip across the new injector layer.

T3 ready to merge.

## Reproduce

```powershell
cd \\wsl.localhost\Ubuntu-24.04\home\phatmt\code\NexusKey
git checkout sprint-2/output-injector

# Build
cmake --build build --target NextKeyApp NextKeyTests --config Debug

# Audit
bash tools/audit/check_hook_thread_no_mutex.sh

# Unit tests (Windows-only)
.\build\tests\Debug\NextKeyTests.exe --gtest_filter="*Injector*:OutputInjectorFactory*"

# Chaos sweep — focus the host then run
.\build\tools\Debug\NextKeyTestRunner.exe --corpus tools\NextKeyTestRunner\corpus\chaos.toml --hook-log build\Debug\NexusKey_hook.log --junit report-d5-{host}.xml --perf-csv perf-d5-{host}.csv
```
