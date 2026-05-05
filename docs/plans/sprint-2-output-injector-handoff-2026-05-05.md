# Sprint 2 T3 IOutputInjector — Mid-Sprint Handoff (2026-05-05)

> **Status**: D0, D1, D2 shipped on `sprint-2/output-injector` (origin pushed).
> **Remaining**: D3 → D4 → D5 → D6 → final PR (4 D-days, ~3-4 active days).
> **Branch**: `sprint-2/output-injector` (no PR yet — opens at D6).
> **Foundation docs**:
> - [`sprint-2-output-injector.md`](sprint-2-output-injector.md) — design + 5-question gate
> - [`sprint-2-output-injector-plan.md`](sprint-2-output-injector-plan.md) — full TDD task list (34 tasks, 96 steps)
> - [`../CODE_GOVERNANCE.md`](../CODE_GOVERNANCE.md) — 5-question pre-code gate (apply before any new code)

This handoff captures **current state, gotchas discovered during D0-D2, and detailed instructions for D3-D6** so any of the 3 collaborators (or AI agents) can pick up cleanly.

---

## Current state — what shipped

### Branch commits (origin/sprint-2/output-injector)

```
f4f5782  Sprint 2 D2: RichEditEmReplaceSelInjector + remove useEditMsgPath_ TryEditMessagePaste branches
ab0f3a5  Sprint 2 D1: Win32SendInputInjector + integrate default Win32 path
2676b99  Sprint 2 D0: IOutputInjector scaffolding + factory stub + test seam
230690e  docs: note pre-T3 code review followups for post-T3 batch
```

### Files in tree (all on branch)

```
src/app/output/                          ── 11 production files
  IOutputInjector.h                      [interface — 3 methods, β+ shape]
  Internal.{h,cpp}                       [test seams: g_sendInput / g_sendMessageW
                                          / g_sendMessageTimeoutW / g_sleep
                                          + TrackedSendInput + kNexusKeyExtraInfo]
  OutputInjectorFactory.{h,cpp}          [WindowClassification + ClassifyWindow stub
                                          + Create dispatcher (RichEdit branch wired,
                                          Electron/Console branches stubbed for D3)]
  Win32SendInputInjector.{h,cpp}         [final, IMPLEMENTED — D1 batch SendInput +
                                          bait-char prefix gate]
  RichEditEmReplaceSelInjector.{h,cpp}   [final, IMPLEMENTED — D2 EM_GETSEL/SETSEL/
                                          REPLACESEL with timeout + redraw suppress]
  SplitDispatchInjector.{h,cpp}          [final, STUB — D3 to implement]

tests/output/                            ── Windows-only unit tests
  InjectorTestBase.h                     [shared fixture, swaps all 4 seams]
  Win32SendInputInjectorTest.cpp         [6 tests, all PASS]
  RichEditEmReplaceSelInjectorTest.cpp   [4 tests; 2 PASS + 2 SKIP in headless env]

src/app/system/HookEngine.{h,cpp}        ── Modified
  injector_ member: std::atomic<std::shared_ptr<IOutputInjector>> (RCU)
  ctor seeds via Output::Create({}) so atomic_load never returns null
  OnFocusChanged: builds WindowClassification from existing flags + atomic_store
  4 useEditMsgPath_ TryEditMessagePaste sites: replaced with inj->Replace calls
  InjectKey: replaced raw INPUT[] builder with inj->SendKey

CMakeLists.txt                            ── Modified
  src/app/output/*.cpp added to NextKeyApp + NextKeyLite (Win32-only targets)
  tests/output/*.cpp + impl sources added to NEXTKEY_TEST_SOURCES Windows-only block
```

### Test gates green at D2

- Linux engine GTest: 1405/1405 PASS (no regression)
- Windows Win32SendInputInjectorTest: 6/6 PASS
- Windows RichEditEmReplaceSelInjectorTest: 2 PASS + 2 SKIP (correct env-gated skip)
- Notepad Win11 chaos: 11/11 PASS (D2 PRIMARY GATE — RichEdit channel exercised)
- Notepad++ chaos: 11/11 PASS (no regression)
- Chrome omnibox chaos: 11/11 PASS (no regression)

---

## Gotchas discovered during D0-D2 (NOT in original plan)

### G1 — `[[nodiscard]]` on `Replace` triggers MSVC `/WX`

The interface declares `[[nodiscard]] virtual bool Replace(...)`. MSVC treats discarded return as warning C4834, and the project compiles with warnings-as-errors (`/WX`).

**When you call `inj->Replace(...)` in HookEngine, you MUST consume the return value.** The pattern:

```cpp
if (!inj->Replace(count, view)) {
    HOOK_LOG(L"  injector reported partial delivery / channel failure");
    // optionally: caller's fallback
}
```

Don't write `inj->Replace(...);` standalone — it WILL break the Windows build.

### G2 — `g_sendMessageTimeoutW` test seam was added in D2 (not in original plan)

Original plan Task 14 used `g_sendMessageW` only. RichEdit's hang protection
needed `SendMessageTimeoutW(SMTO_ABORTIFHUNG, 50ms)`, so D2 added a parallel
seam `g_sendMessageTimeoutW`. **D3+ should bind the new seam in test fixtures
when needed** — `InjectorTestBase::SetUp/TearDown` already handles it.

### G3 — Factory wiring must be in same commit as classification refactor

D2's first attempt shipped with `OutputInjectorFactory::Create()` still
returning `Win32SendInputInjector` for everything (D0 stub left untouched).
Notepad Win11 chaos passed 11/11 anyway because Win32 SendInput "happened to
work" on this system — but the intended RichEdit channel was never exercised.

The bug was caught only when reviewing `OutputInjectorFactory.cpp` content
right before commit. **Lesson for D3**: when you wire `c.isElectron` /
`c.isConsole` / `c.isChromium` in HookEngine's classification, immediately
update `Create()` to dispatch them and add a smoke test (e.g., a factory
test that asserts `Create({.isElectron=true})` returns a `SplitDispatchInjector*`).
The chaos sweep is NOT a sufficient correctness check for factory dispatch —
the original Win32 path may pass by coincidence.

### G4 — `RichEditEmReplaceSelInjector::SendKey` constructs a temp `Win32SendInputInjector`

To avoid duplicating the `MakeKeyEvent` helpers, `RichEdit::SendKey` does
`Win32SendInputInjector(/*needsBaitCharPrefix=*/false).SendKey(vkCode);` —
constructs a stack-local Win32 injector per call (~80 bytes, no heap). Cheap.

If profiling shows this as hot, extract `SendKey` logic into a free function
in `Internal.h` and let both impls call it.

### G5 — `RichEdit` unit tests SKIP gracefully in headless env

Tests `ReplaceEmitsGetSelSetSelReplaceSelInOrder` and `BsCountZeroSkipsSetSel`
call `GTEST_SKIP()` if no foreground window exists (impl correctly bails
before touching `g_sendMessageTimeoutW`). The other 2 tests (`SendMessageReturning
ZeroOnReplaceSelReturnsFalse` and `SendKeyFallsThroughToSendInput`) don't
need foreground and always run.

Coverage of the SKIPped paths comes from the chaos sweep on real Notepad
Win11. **Don't treat the skip as a test gap** — it's an env limitation, not a
missing assertion.

### G6 — `useEditMsgPath_` flag still read at line 1471 (passthrough policy gate)

D2 replaced 4 dispatch branches but left line 1471's read intact:

```cpp
const bool editMsgPath = useEditMsgPath_.load(std::memory_order_acquire);
// ... at line 1490:
if (... && !editMsgPath && ...) { /* enter passthrough */ }
```

This is **policy** (does the host's renderer require force-routing alpha keys
through the synth channel?) not dispatch (which channel to use). D3 / D4
needs to decide:
- Keep the flag (cheapest, no functional change)
- Add a query method to `IOutputInjector` (e.g., `bool RequiresForcedReplaceForAlphaKeys()`) — heavier interface bloat, defer
- Use `SettleBudget() == 0ms` as a proxy signal — tied to D5

For now the flag stays. D4 cleanup may refactor.

### G7 — `ShouldUseClipboard()` / VB6 path still calls `TryEditMessagePaste`

Line ~2999 (inside `if (ShouldUseClipboard())`) still calls
`TryEditMessagePaste(toSend, bsCount)` directly. This is the VB6 / ANSI-
internal path where EM_REPLACESEL is the **preferred** channel before
falling back to clipboard. It's NOT one of the 4 useEditMsgPath_ branches.

D3 / D4 may choose to:
- Route this through injector_ too (would need a way to force-RichEdit-attempt regardless of host class)
- Leave as-is (TryEditMessagePaste body remains in HookEngine until truly dead)
- Add a separate `VbAnsiInjector` impl

**For D3 → just leave it alone.** It works today. D4 cleanup decides.

### G8 — `kNexusKeyExtraInfo = 0x4E4B` constant duplicated

`Internal.h` defines `kNexusKeyExtraInfo = 0x4E4BULL` matching
`HookEngine::NEXUSKEY_EXTRA_INFO = 0x4E4B`. Two sources of truth, intentional
to avoid HookEngine.h include from output/. **If either changes, the other
MUST be updated.** A static_assert isn't possible without including
HookEngine.h in Internal.cpp; consider adding a `// MUST MATCH` comment cross-
reference (already in Internal.h:33).

### G9 — Notepad Win11 chaos passed BEFORE the RichEdit injector was actually wired

(Sub-symptom of G3.) This means our chaos corpus, in this env, doesn't
strongly distinguish posted-message from sent-message channels. Sprint 1
D12's verdict ("RichEditD2DPT requires sent-message") was based on a
specific failure mode that the current Notepad/system version may have
patched. **Don't read "chaos passed" as proof that RichEdit dispatch is
correct** — verify by inspecting `OutputInjectorFactory::Create()` returns
the right impl, plus ideally instrument with a hook log noting which impl
is active.

---

## D3-D6 — detailed steps

Plan reference: [`sprint-2-output-injector-plan.md`](sprint-2-output-injector-plan.md)
Tasks 19-34. Below is the high-level for the handoff; full step-by-step is
in the plan file.

### D3 — SplitDispatchInjector + remove Electron/Console flags (~1 day)

**Goal**: SplitDispatchInjector implemented; HookEngine's `isElectronApp_` /
`isConsoleApp_` no longer read for dispatch (still set in OnFocusChanged
until D4); `DispatchSendInput()` body deleted; duplicate split block at
line ~3068 deleted; Discord chaos 11/11 + ChatGPT chaos 11/11.

**Plan tasks**: 19-23 (`sprint-2-output-injector-plan.md`).

**Concrete steps**:

1. **Write 5 unit tests** in `tests/output/SplitDispatchInjectorTest.cpp`
   per plan Task 19 (split BS-then-Sleep-then-chars order, bsCount=0 skips
   Sleep, text empty skips second send, sleepMs from constructor, partial
   first send returns false). Add to CMake `NEXTKEY_TEST_SOURCES` Win32 block
   alongside the impl source `src/app/output/SplitDispatchInjector.cpp`.

2. **Implement `SplitDispatchInjector::Replace`** per plan Task 20:
   - Stack `std::array<INPUT, 256>` for BS batch and char batch
   - `MakeKeyEvent / MakeUnicodeChar` helpers (duplicate from Win32 impl OK
     for now — D4 cleanup may consolidate to `Internal.h`)
   - Pattern: send BS batch via `TrackedSendInput`, if has chars also do
     `g_sleep(sleepMs_)` then send char batch
   - `bsCount=0` → no Sleep, single char-batch send
   - `text.empty()` → no second send, no Sleep
   - On any partial send → return false immediately (don't attempt the
     subsequent batch)

3. **Update `OutputInjectorFactory::Create`** per plan Task 21:
   ```cpp
   if (c.isRichEditD2DPT) return make_shared<RichEditEmReplaceSelInjector>();
   if (c.isElectron)      return make_shared<SplitDispatchInjector>(6);
   if (c.isConsole)       return make_shared<SplitDispatchInjector>(5);
   return make_shared<Win32SendInputInjector>(c.isChromium);
   ```
   Add factory tests asserting each branch returns the right type via
   `dynamic_cast`. Per **Gotcha G3** — wire and TEST the dispatch in the
   SAME commit.

4. **Move classification logic** from HookEngine `OnFocusChanged` into
   `OutputInjectorFactory::ClassifyWindow`. The HookEngine block at
   lines ~2548-2604 (the `bool isBrowser, isElectron, isQtApp, isVB6,
   localConsole;` + `ClassifyWindow(activeHwnd, isBrowser, ...)` + the
   subsequent exe-name scan for `notepad.exe` / Outlook / Excel / WebView2
   detection) needs to be ported wholesale. Don't try to refactor too —
   just port and rename. The free function `IsKnownElectronExe`,
   `IsKnownConsoleExe`, `IsWebView2App` (or whatever the actual functions
   are called) need to either move with the classification logic or stay
   accessible.

   **Important**: HookEngine still wants to know `isElectron`,
   `isOutlook`, etc. for non-dispatch concerns (passthrough policy at line
   1490, retry-loop gating at line 2959). For D3 keep both: the existing
   atomic stores at lines 2611-2617 stay; the factory call uses the same
   classification result. The atomic stores get deleted in D4.

5. **Wire HookEngine OnFocusChanged**:
   ```cpp
   NextKey::Output::WindowClassification c{};
   c.isRichEditD2DPT = localEditMsg;
   c.isElectron      = localElectronApp;  // now also wired in D3
   c.isConsole       = localConsole;       // now also wired in D3
   c.isChromium      = localNeedBait;      // now also wired in D3 (proxy for autocomplete-dismiss path — Sprint 1 may have a more precise flag)
   injector_.store(NextKey::Output::Create(c), std::memory_order_release);
   ```

6. **Replace `DispatchSendInput()` calls** at lines ~2887, ~3427, ~3068.
   Each becomes `inj->Replace(bsCount, text)`. The duplicated split block
   at line ~3068 deletes entirely.

   Some callers may have the data as `std::vector<INPUT> bsEvents,
   charEvents` rather than `(bsCount, text)`. The diff between
   previous/new composition is what produces these — go up the call stack
   and adjust caller to pass `(bsCount, text)` directly.

7. **Delete `DispatchSendInput()` body and declaration** in HookEngine
   if no callers remain. (If one caller is non-trivial to refactor, leave
   the body for D4 cleanup.)

8. **Verify gates**:
   - Linux: `cmake --build build-linux --target NextKeyTests && ./build-linux/tests/NextKeyTests` — 1405/1405 PASS
   - Windows: `--gtest_filter="*Injector*"` — 15+ tests PASS (or SKIP for env-gated)
   - Discord chaos: focus Discord, run `NextKeyTestRunner --corpus chaos.toml --junit report-d3-discord.xml --perf-csv perf-d3-discord.csv` — 11/11 PASS
   - ChatGPT chaos (Chrome renderer textarea): same with `report-d3-gpt.xml` — 11/11 PASS
   - Notepad Win11 chaos: re-run, verify still 11/11 PASS
   - Notepad++ chaos: re-run, verify still 11/11 PASS
   - Chrome omnibox chaos: re-run, verify still 11/11 PASS

9. **Commit**: `Sprint 2 D3: SplitDispatchInjector + remove isElectronApp_/isConsoleApp_ branches`. Push.

### D4 — Audit script update + dead code removal (~½ day)

**Goal**: 4 atomic flags deleted, dead helpers deleted, audit script updated,
HookEngine.cpp ≤ ~3300 LOC (currently 3593).

**Plan tasks**: 24-27.

**Concrete steps**:

1. **Delete 4 atomic field declarations** from `HookEngine.h`:
   `useEditMsgPath_`, `isElectronApp_`, `isConsoleApp_`, `needBaitChar_`.
   Each was migrated to `std::atomic<bool>` in Sprint 1 D5.2 — the
   declarations live in the atomic-bool member section.

2. **Delete `.store(...)` writes** in `OnFocusChanged` for those 4 fields.
   Currently lines 2611-2617 area.

3. **Resolve last readers**:
   - `useEditMsgPath_.load` at lines 922 / 1285 / 1471 / 2959 — refactor
     each to use the appropriate signal (for the policy-gate at 1471, may
     keep the flag if simpler; the dispatch gates at 922/1285/2959 should
     route through injector unconditionally OR via `SettleBudget()` proxy
     — see G6).
   - `needBaitChar_.load` — last read should already be inside Win32 impl
     (via `needsBaitCharPrefix_` constructor flag). Verify with `grep -n
     "needBaitChar_" src/app/system/HookEngine.cpp` — expect 0 results.
   - `isElectronApp_.load` / `isConsoleApp_.load` — readers should be
     fully replaced by D3. Verify same way.

4. **Delete dead helpers**:
   - `HookEngine::DispatchSendInput` (decl + body)
   - `HookEngine::TryEditMessagePaste` (decl + body) — unless G7 still needs it
   - `HookEngine::TrackedSendInput` (was member; now `Internal::TrackedSendInput`)
   - `HookEngine::SendBackspaceEvents` / `SendCharEvents` if unused after
     simplification

5. **Update audit script** `tools/audit/check_hook_thread_no_mutex.sh`:
   - Remove from `ATOMIC_BOOLS` regex: `useEditMsgPath_|isElectronApp_|isConsoleApp_|needBaitChar_`
   - Add Check 4: grep that `injector_` is accessed only via
     `std::atomic_load` / `std::atomic_store` (no plain `injector_->...`
     outside an atomic op).
   - Update Check 1 comment block to clarify the regression-trap intent
     (per `docs/TODO.md` Review 2026-05-05 critical finding).

6. **Verify gates**:
   - `bash tools/audit/check_hook_thread_no_mutex.sh` — exit 0
   - `wc -l src/app/system/HookEngine.cpp` — target ≤ 3300
   - All chaos hosts re-verified

7. **Commit**: `Sprint 2 D4: audit script update + remove dead dispatch code`. Push.

### D5 — SettleBudget integration (~½ day)

**Goal**: `kSynthSettleMs = 100` constant replaced with
`injector_->SettleBudget().count()`. On Win32 / RichEdit hosts, commit-undo
replay measurably faster (target ≥ 30%).

**Plan tasks**: 28-31.

**Concrete steps**:

1. **Replace constant read** at HookEngine.cpp line ~955:
   ```cpp
   // BEFORE
   if (... && (now - lastRealSynthTime_) < kSynthSettleMs && !isToneModifier) {
   // AFTER
   auto settleMs = static_cast<DWORD>(
       injector_.load(std::memory_order_acquire)->SettleBudget().count());
   if (... && (now - lastRealSynthTime_) < settleMs && !isToneModifier) {
   ```

2. **Delete `kSynthSettleMs` constant** from header.

3. **Run chaos** on all 5 hosts. Compare case 5.3 (cross-word BS replay)
   inter-keystroke delay vs Main baseline.

4. **Write baseline doc** `docs/baselines/perf-baseline-t3-settle-budget.md`
   with per-host case 5.3 mean / p99 + delta % vs Main.

5. **Acceptance**: ≥ 30% reduction on Win32 (Notepad++/Chrome) and RichEdit
   (Notepad Win11). Electron (Discord) unchanged. Functional 11/11 on all.

6. **Commit**: `Sprint 2 D5: per-host SettleBudget — Win32 30ms, RichEdit 0ms, Electron 100ms`. Push.

### D6 — Cross-host chaos sweep + final PR (~1 day)

**Plan tasks**: 32-34.

**Concrete steps**:

1. **Add `--host-class` flag** to `NextKeyTestRunner` per plan Task 32:
   - CLI flag: `--host-class={win32|richedit|electron|console}`
   - Mechanism: runner sets `NEXUSKEY_FORCE_HOST_CLASS` env var
   - HookEngine factory in `OnFocusChanged` reads env var and overrides
     classification before calling `Create()`

2. **Run cross-host matrix** (5 hosts × {natural, 1-2 forces} ≈ 14 cells × 11 cases ≈ 150 case runs). User does focused-app orchestration; runner outputs
   `report-d6-{host}-{class}.xml` + `perf-d6-{host}-{class}.csv`.

3. **Write baseline doc** `docs/baselines/perf-baseline-t3-final.md` with
   per-cell verdict + p99. Acceptance:
   - Natural cells: 55/55 PASS, p99 within 10% of Main baseline
   - Forced cells: PASS where mechanism is compatible
   - Commit-undo replay on Win32/RichEdit ≥ 30% faster (D5 win)
   - Functional regressions: 0

4. **Open final PR** `Sprint 2 T3: IOutputInjector — extract output channel + matrix harness`. PR description includes:
   - Linkback to design doc + plan + governance
   - D-day breakdown summary
   - Chaos delta table (Main vs T3-final)
   - LOC delta (HookEngine.cpp 3593 → ~3300)
   - D7 audit gate evidence

5. **Commit + push**: `Sprint 2 D6: cross-host chaos sweep — verdict + perf delta capture`.

### After T3 merges — pre-T3 review followups batch

Per `docs/TODO.md` "Review (2026-05-05) — Pre-T3 Code Review Followups":
- Critical: clarify D4 SPIKE regression-trap markers in HookEngine
- Minor 1: `LowLevelMouseProc` race investigation
- Minor 2: `QuickSyncFromSharedState` Rule #11 violation (highest impact)
- Minor 3: misleading comment at HookEngine.cpp line 779

Open a single small PR with one commit per finding for bisect clarity.

---

## Resume checklist for next session

1. `git checkout sprint-2/output-injector` (it's pushed; `git pull` if multi-machine)
2. Read this handoff section "Gotchas G1-G9" — most are subtle and easy to re-bite
3. Read `sprint-2-output-injector-plan.md` Tasks 19-23 (D3 task list)
4. Apply the 5-question gate (CODE_GOVERNANCE Part 1) at the top of any new D-commit message
5. Each D-commit pattern (already established by D0-D2):
   - Linux build + engine GTest 1405/1405 PASS first
   - Windows build (user) + injector unit tests
   - Chaos sweep on the affected host(s) + non-regression on others
   - Commit with detailed message including the 5-question gate answers
   - Push to origin
6. After D6, follow the final-PR checklist in §6 of the design doc.
