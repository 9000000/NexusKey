# Architecture Review Design — Single-Writer Composition State + Off-Hook Config Reload

**Date**: 2026-05-19
**Status**: Draft — design only, no implementation in this session
**Scope**: Phase-by-phase design responding to external architecture review (2 rounds of refinement, see Context).
**Pre-req commits**: `6171a82` (CODING_RULES drift refresh, no semantic changes)

---

## 0. Context

External architecture reviewer surfaced 5 concerns over 2 rounds of dialogue:

1. **Thread ownership of composition state in `HookEngine` is not clean** — `WinEventProc` callback runs on **main thread** (the thread that called `SetWinEventHook` with `WINEVENT_OUTOFCONTEXT`), but mutates `engine_`, `previousComposition_`, `inputHistory_`, `commitStack_`, `rawMacroBuffer_` which are also mutated from the hook thread via `LowLevelKeyboardProc`. This is a data race, not a style issue.
2. **Hot path still contains slow operations** — `QuickSyncFromSharedState` slow path performs `ReloadFromToml` inside the LL hook callback (`HookEngine.cpp:540-543`). Violates CODING_RULES Rule 11.2 and 11.3.
3. **`HookEngine.cpp` (3563 LOC) is over-extended** — orchestrates hook install, focus tracking, config reload, macro state, output injection, app classification, commit-undo FSM all in one class.
4. **No per-stage p50/p95/p99 measurement** — chaos perf baselines aggregate at request level, not stage level. Can't prove a refactor doesn't regress p99 jitter.
5. **Replay/chaos harness for concurrency scenarios is missing** — current 1551 unit tests + `tools/run-chaos.ps1` are timing-driven, not concurrency-driven. No tests for focus-pending-then-keydown interleaving, config-reload-mid-burst, injector-class-switch-mid-composition.

After 2 rounds of refinement, reviewer's recommendations converged on:
- **PostThreadMessage wake + atomic coalesced command bits** (not heap payload, not SPSC ring).
- **Drain barrier at LL callback start** (sync barrier, no eventual-consistency).
- **8-bucket log-scale histogram** with runtime hidden toggle (not HDR for hot path).
- **Replay harness** for concurrency invariants, not "more unit tests".

This design adapts those recommendations to NexusKey-specific reality (Sprint 1 D5/D6/D8-D10 RCU + atomic flags already shipped, Sprint 2 T3 IOutputInjector strategy already shipped, H1-H7 method decomposition already shipped) and to CODING_RULES (Rule 11.1 Tier 2 30ms p99 budget, Rule 11.3 two-phase focus pattern, Rule 11.2 TOML on background thread prescription).

---

## Migration Order (anh decision 2026-05-19)

Reviewer's implicit order was #1 → #2 → #3 → #4 → #5. This design **promotes #4 to first** so that the ownership refactor has a measurement baseline. Per Memory `project_core_philosophy` ("nhanh / nhẹ / mượt / mở rộng-không-ảnh-hưởng-perf") + `project_test_first` (write failing test before code), no baseline = no proof of non-regression.

| Phase | Reviewer ID | Scope | Gate |
|---|---|---|---|
| 1 | #4-light | Per-stage histogram + `diagFlags` toggle | Histogram baseline captured across 5 chaos hosts |
| 2 | #1 | Single-writer composition state (PostHookCommand mailbox + two-phase focus) | Phase 1 histogram shows p99 not regressed |
| 3 | #2 | Config reload moved to worker thread + RCU `ConfigSnapshot` | `config_reload` stage disappears from hook thread histogram |
| 4 | #5 | Replay harness for focus/config/injector concurrency scenarios | 30 new gtests pass + chaos extension scenarios pass |
| 5 | #3 | HookEngine class split into 4 services — **CONDITIONAL** | Phases 2-3 ship + LOC reduction + memory `project_h1_decomposition_pattern` revisit |

---

## Phase 1 — Per-stage histogram + `diagFlags`

### Goal

Capture p50/p95/p99/max per stage as gold reference before any ownership change. Without this, Phase 2 ships on hope, not proof.

### Stage definition

| Stage | Site | Budget (Rule 11.1) |
|---|---|---|
| `engine_push` | `engine_->PushChar` + `Peek` | <1µs (Tier 1) |
| `top_guard` | `RunTopGuards` entry → return | <16µs |
| `injector` | `injector_->Replace` inner call | <4ms |
| `replace` | `ReplaceComposition` entry → return | <16ms |
| `focus_classify` | `ClassifyFocusedWindow` (post Phase 2 split) | <5ms |
| `config_reload` | `ReloadFromToml` body (Phase 3 moves off hook) | <50ms |
| `total_keydown` | LL callback entry → return | <30ms p99 (Tier 2) |

### Bucket boundaries (8-bucket log-scale)

```
<1µs   (Tier 1 engine cliff)
<16µs
<256µs
<1ms   (stage budget cliff)
<4ms
<16ms  (chaos baseline p99 cliff — current 14-17ms worst-case)
<64ms  (LowLevelHooksTimeout/3 ~100ms danger cliff)
≥64ms
```

### Record path

```cpp
#ifdef NEXTKEY_PERF_HIST
struct PerfScope {
    PerfStage stage;
    LARGE_INTEGER t0;
    explicit PerfScope(PerfStage s) noexcept : stage(s) {
        if (!PerfHistogram::Enabled()) { t0.QuadPart = 0; return; }
        QueryPerformanceCounter(&t0);
    }
    ~PerfScope() noexcept {
        if (!t0.QuadPart) return;
        LARGE_INTEGER t1; QueryPerformanceCounter(&t1);
        PerfHistogram::Record(stage, t1.QuadPart - t0.QuadPart);
    }
};
#define PERF_SCOPE(s) PerfScope _scope##__LINE__(s)
#else
#define PERF_SCOPE(s) ((void)0)
#endif
```

Stack-only RAII (Rule 11.2 forbids heap in hot path). `QueryPerformanceCounter` ~30ns, well under stage budgets.

### Toggle + flush

- **`SharedState.diagFlags` bit 0** = histogram enabled. Default OFF.
- Settings hidden TOML key `[debug] perf_histogram = true` — anh can flip without rebuild.
- Flush target: `%APPDATA%/VKey/perf-histogram-<pid>-<startTs>.log`, TSV `stage\tbucket_µs\tcount\tepoch_secs`.
- Flush cadence: append every 60s when enabled + full dump on `Stop()`.
- **Privacy**: zero key/text/HWND/title logged. Counter-only.

### Implementation site

- New file `src/app/system/PerfHistogram.{h,cpp}` (~150 LOC).
- Hot-path entries gain `PERF_SCOPE(...)` lines (compile-out with `NEXTKEY_PERF_HIST` undef).
- Build flag `NEXTKEY_PERF_HIST` ON for Debug, ON-by-default for Release (overhead negligible when toggle OFF).

### Verify gate

- Gtest `PerfHistogramTest.cpp` — bucket boundary correctness + thread-safety of atomic counter increment + `Enabled()` gate.
- Chaos 55/55 PASS unchanged.
- Compare histogram with/without `NEXTKEY_PERF_HIST` defined → release-build delta ≤1% at p99.

---

## Phase 2 — Single-writer composition state

### Goal

Move all mutation of 16 composition-state fields to **hook thread only**. Eliminate the Rule 11.3 violation where `WinEventProc → OnFocusChanged` mutates `engine_`/`previousComposition_`/`autoCapState_` from main thread.

### Cross-thread mutation sites (verified by grep)

| Site | Current thread | Mutates | Migration |
|---|---|---|---|
| `WinEventProc` → `OnFocusChanged` | main (WINEVENT_OUTOFCONTEXT) | engine_, previousComposition_, autoCapState_, currentExe_, injector_ | Two-phase: classify on main, apply on hook |
| `OnTickPoll` (200ms `MainThreadWorker`) | main | appProfileCache_ GC + layout check | post `kTickPoll` bit |
| `ApplyConfig` (settings save) | main | engine_ replace via EngineFactory | post `kConfigApply` bit |
| `ToggleVietnameseMode` (hotkey/tray) | any | engine_ reset + commit | post `kToggleVN` bit |
| `CheckLayoutChange` (via Focus/Tick) | main | engine_ recreate on HKL change | inside drain |

### Mailbox + drain

```cpp
struct FocusClassification {  // POD — Rule 11.3 Phase 1 produces, hook consumes
    HWND hwnd;
    std::wstring exeName;
    bool isExcluded, isTsf, isElectron, isRichEditD2DPT, isConsole;
    InputMethod methodOverride;
    CodeTable encodingOverride;
};

struct HookCommandMailbox {
    std::atomic<uint32_t> bits{0};
    std::atomic<std::shared_ptr<const FocusClassification>> pendingFocus;
    std::atomic<bool> wakePosted{false};
};

void PostHookCommand(uint32_t bit, std::shared_ptr<const FocusClassification> cls = nullptr) {
    if (cls) mailbox_.pendingFocus.store(std::move(cls), std::memory_order_release);
    mailbox_.bits.fetch_or(bit, std::memory_order_release);
    if (!mailbox_.wakePosted.exchange(true, std::memory_order_acq_rel)) {
        PostThreadMessage(hookThreadId_, WM_APP_HOOK_COMMAND, 0, 0);
    }
}

void DrainHookCommands() {  // hook thread only
    mailbox_.wakePosted.store(false, std::memory_order_release);     // ORDERING: BEFORE exchange
    uint32_t b = mailbox_.bits.exchange(0, std::memory_order_acquire);
    if (!b) return;
    if (b & kConfigApply)  ApplyConfigOnHookThread();
    if (b & kFocusChanged) ApplyFocusOnHookThread(mailbox_.pendingFocus.exchange(nullptr, std::memory_order_acquire));
    if (b & kTickPoll)     ApplyTickPollOnHookThread();
    if (b & kToggleVN)     ApplyToggleVNOnHookThread();
}
```

### Critical ordering rule

In `DrainHookCommands`, **`wakePosted=false` MUST happen BEFORE `bits.exchange(0)`**. Otherwise a poster between exchange and wakePosted-clear sees `wakePosted==true`, skips post, and its bit is stranded until the next keydown. Documented inline; gtest verifies.

### Drain position in Rule 11.4 hierarchy

Insert after step 4 (`sending_` re-entrant guard), before step 5 (English mode pass):

```
1. dwExtraInfo == VKEY_EXTRA_INFO → return (own synthetic events)
2. nCode < 0 → return
3. !self → return
4. sending_ → return
5. mailbox.bits → DrainHookCommands()    ← inserted here
6. English mode → pass
7. Modifier keys → pass
8. Engine processing
```

Reason: synthetic events come from `injector_->Replace` itself; draining there would re-enter classification.

### Re-entrancy contract

Drain handlers MUST NOT call: `SendInput`, `SetWindowsHookEx`, nested `PeekMessage`/`DispatchMessage`, `injector_->Replace`. May only mutate composition state + atomic stores. Verified by:
- Debug-only `inDrainHookCommands_` flag with `assert(!inDrainHookCommands_)` at forbidden entry points.
- Gtest call-graph verification (mock injector tracks calls).

### Two-phase focus

```cpp
// Phase 1 — CLASSIFY (main thread, WinEventProc): no lock, no state writes
void CALLBACK WinEventProc(HWINEVENTHOOK, DWORD event, HWND hwnd, ...) {
    if (event == EVENT_SYSTEM_MINIMIZEEND) hwnd = GetForegroundWindow();
    if (!hwnd) return;
    auto cls = std::make_shared<const FocusClassification>(ClassifyFocusedWindow(hwnd));
    s_instance->PostHookCommand(kFocusChanged, std::move(cls));
}

// Phase 2 — APPLY (hook thread, drain): mutate only, no Win32 calls
void ApplyFocusOnHookThread(std::shared_ptr<const FocusClassification> cls) {
    assert(GetCurrentThreadId() == hookThreadId_);
    ResetComposition();
    autoCapState_ = AutoCapState::Idle;
    currentExe_ = cls->exeName;
    isTsfApp_.store(cls->isTsf, std::memory_order_release);
    // ... injector swap, encoding override ...
}
```

### Debug thread-id assertion

Insert `assert(GetCurrentThreadId() == hookThreadId_)` at 8 mutation entry points:
- `ResetComposition`, `CommitComposition`, `ClearWordState`, `ReplaceComposition`, `ReplayCommittedChars` (reviewer's 5)
- `HandleAlphaKey`, `HandleBackspace`, `ApplyConfigOnHookThread` (Claude additions from grep)

Release builds elide via `#ifdef NDEBUG`.

### Fix incorrect comment

`HookEngine.cpp:794-799` currently asserts "WinEventProc runs on the hook thread per the existing architecture". This is **false** per Win32 `WINEVENT_OUTOFCONTEXT` semantics — the callback fires on the thread that called `SetWinEventHook`, i.e., main. Rewrite to explain: "WinEventProc fires on the installer thread (main, per WINEVENT_OUTOFCONTEXT). Safe to access `self` via atomic load; all state mutation deferred to hook thread via `PostHookCommand`."

### Verify gate

- Phase 1 histogram delta: p99 of `total_keydown` and `focus_classify` not worse than Phase 1 baseline.
- Chaos 55/55 PASS.
- New gtests in Phase 4 prove drain ordering, focus interleaving, mailbox coalesce.

---

## Phase 3 — Config reload out of keydown

### Goal

Close the Rule 11.3 violation at `HookEngine.cpp:540-543` (lock + `ReloadFromToml` on the hook thread slow path). Rule 11.2 already prescribes the fix verbatim: *"TOML parse 1-10ms → Background thread + atomic swap"*.

### Affected methods (verified by grep)

| Method | LOC | Mutates |
|---|---|---|
| `ReloadFromToml` (line 597) | ~80 | all of the below |
| `ReloadAppOverrides` (line 2771) | ~15 | `appEncodingOverrides_`, `appInputMethodOverrides_` |
| `ReloadExcludedApps` (line 2786) | ~10 | `excludedAppSet_` |
| `ReloadTsfApps` (line 2796) | ~10 | `tsfAppSet_` |
| `ReloadMacroTable` (line 2804) | ~13 | `macroTable_`, `spaceMacroKeys_` |

### RCU snapshot pattern (Rule 11.3 extension of existing `config_` pattern)

```cpp
struct ConfigSnapshot {
    std::unordered_map<std::wstring, CodeTable>    appEncodingOverrides;
    std::unordered_map<std::wstring, InputMethod>  appInputMethodOverrides;
    std::unordered_set<std::wstring>               excludedAppSet;
    std::unordered_set<std::wstring>               tsfAppSet;
    std::unordered_map<std::wstring, std::wstring> macroTable;
    std::unordered_set<wchar_t>                    spaceMacroKeys;
    uint32_t                                       generation;
};

// Hook thread reader (zero-cost):
auto snap = configSnapshot_.load(std::memory_order_acquire);
if (snap->excludedAppSet.count(currentExe_)) { ... }

// Worker thread writer (off hook):
auto newSnap = std::make_shared<const ConfigSnapshot>(ParseAllFromToml());
configSnapshot_.store(std::move(newSnap), std::memory_order_release);
```

### Trigger flow

1. Settings save → `SharedState.configGeneration++`.
2. **Worker thread** (`MainThreadWorker` from Sprint 1 D8-D10, already on main): polls `configGeneration`, parses TOML (~1-10ms off hook), builds `ConfigSnapshot`, publishes via RCU, then `PostHookCommand(kConfigApply)` so hook thread can update `lastConfigGeneration_` and log.
3. **Hook thread drain**: handles `kConfigApply` cheaply — just `lastConfigGeneration_ = ...` + log + maybe recreate `engine_` if input method changed. **No TOML parsing.**

### Hook slow path update

```cpp
// BEFORE (violates Rule 11.3):
if (state.configGeneration != lastConfigGeneration_) {
    std::lock_guard _lock(stateMutex_);
    ReloadFromToml();   // 1-10ms TOML parse on hook thread!
}

// AFTER (compliant):
if (state.configGeneration != lastConfigGeneration_) {
    PostHookCommand(kRequestConfigReload);   // wake worker
    // lastConfigGeneration_ update deferred until worker publishes new snapshot
}
```

`kRequestConfigReload` posts back to main thread (cross-thread sibling to `PostHookCommand`), worker handles parse + publish.

### Verify gate

- Phase 1 histogram: `config_reload` stage disappears from hook thread; appears on worker thread (separate histogram).
- Phase 4 adds `ConfigReloadDuringTyping` gtest.
- Chaos extension: `--inject-config-reload N` scenario passes.

---

## Phase 4 — Replay harness for concurrency invariants

### Goal

Verify Phases 2-3 with deterministic replay. Existing chaos harness is timing-driven; this adds concurrency-scenario tests.

### Existing test landscape

- `tests/HookEngineAtomicTests.cpp` — atomic flag migration (Sprint 1 D5).
- `tests/MainThreadWorkerTests.cpp` — worker queue (Sprint 1 D8-D10).
- `tests/EngineBenchmarkTest.cpp` — hot-path latency budget.
- `tools/run-chaos.ps1` — production replay through 5×11 baseline hosts.

Missing: tests for focus/config/injector interleaving.

### New gtests

1. **`FocusInterleavingTest.cpp`** — focus pending before keydown drains correctly; focus coalesce keeps only latest HWND.
2. **`ConfigReloadBurstTest.cpp`** — RCU keeps in-flight composition's old snapshot alive; next-key sees new snapshot.
3. **`InjectorSwitchTest.cpp`** — composition continues correctly across injector class swap mid-word.
4. **`ThreadOwnershipTest.cpp`** — `EXPECT_DEATH` on mutation from non-hook thread (debug build only).

### Production chaos extension (`tools/run-chaos.ps1`)

- `--inject-focus-flap N`: synthetic `EVENT_SYSTEM_FOREGROUND` every N ms during typing.
- `--inject-config-reload N`: `SharedState.configGeneration++` every N keystrokes.
- Per-stage histogram p99 from Phase 1 must not degrade vs baseline.

### Test infrastructure

- `HookCommandMailbox` is injectable for gtest (Linux-portable, no Win32 `SetWindowsHookEx`).
- `ThreadIdProvider` interface (Sprint 1 D5 pattern): production reads `GetCurrentThreadId()`, tests inject fixed IDs.
- Reuse `MainThreadWorker` test harness from Sprint 1 D10 for worker simulation.

### Verify gate

- Gtest count rises from 1551 → ~1580.
- Chaos baseline rerun mandatory; ≤1% p99 delta vs pre-refactor.

---

## Phase 5 — HookEngine class split (CONDITIONAL)

### Defer details until after Phase 3

Memory `project_h1_decomposition_pattern` documents: H1 god-method decomposition (ProcessKeyDown 561→79 LOC) succeeded via **outcome-enum + per-step helpers**, not via class split. Class split before ownership is clean = two refactors entangled.

After Phase 2 (#1) exposes natural seams between "command intake" (main → hook) and "state mutation" (hook only), and Phase 3 (#2) factors out the config snapshot component naturally, the four service boundaries reviewer proposed will emerge from implementation, not from paper.

### Sketch (reviewer's proposed shape, decide after Phase 3 ships)

| Service | Responsibility | Current location in HookEngine.cpp |
|---|---|---|
| `CompositionSession` | `engine_`, `previousComposition_`, `inputHistory_`, `commitStack_`, `rawMacroBuffer_`, Reset/Commit/Clear/Replay/Replace | ~1000 LOC scattered |
| `FocusProfileService` | `currentExe_`, `appProfileCache_`, `appModeMap_`, `ClassifyFocusedWindow`, `OnFocusChanged` | ~500 LOC |
| `ConfigSnapshotProvider` | `configSnapshot_` (RCU), `ReloadFromToml` family, worker reload handler | ~300 LOC (post Phase 3) |
| `OutputCoordinator` | `injector_` (RCU), `OutputInjectorFactory` selection, retry policy | ~400 LOC |

Remaining `HookEngine` (~1500 LOC) = orchestrator: LL hook install/uninstall, `ProcessKeyDown` switch dispatch, mailbox drain, modifier tracking, hotkey dispatch.

### Go/no-go criteria

**GO** if:
1. Phase 2 + Phase 3 shipped, chaos baseline stable ≥1 week.
2. Histogram from Phase 1 shows 4 service boundaries align with perf bottleneck domains (e.g., `focus_classify` stage = `FocusProfileService` scope).
3. Memory `project_h1_decomposition_pattern` revisit: method-level decomposition is no longer sufficient.

**NO-GO** if:
- HookEngine.cpp <2500 LOC after Phase 2+3 cleanup (expected -800 LOC).
- Phase 4 test coverage adequately encapsulates concurrency invariants.

### Implementation pattern (if GO)

Per Sprint 2 T3 IOutputInjector precedent — abstract interface (Rule 4.1) + RCU-published shared_ptr (Rule 11.3) + Linux-portable DI seam (memory `project_h5_di_seam_pattern`). NO inheritance hierarchy, NO global service locator.

### Ship plan (if GO)

4 PRs sequential (1 service/PR), not one big bang. Each PR chaos 55/55 PASS gate. **Decision deferred to post-Phase-3 review.**

---

## Open Decisions

| # | Decision | Default | Anh confirm? |
|---|---|---|---|
| 1 | `NEXTKEY_PERF_HIST` default ON for Release | ON (overhead ~0 when toggle off) | TBD |
| 2 | Hidden toggle channel: `[debug] perf_histogram` TOML key vs tray menu chord | TOML key | TBD |
| 3 | `configSnapshot_` reload trigger: `MainThreadWorker` poll vs named-event signal | `MainThreadWorker` poll (200ms tick exists) | TBD |
| 4 | Phase 5 GO/NO-GO | Decide after Phase 3 ships | TBD |
| 5 | Reviewer relay for Phase 5 boundaries | One more round at GO time | TBD |

## Appendix A — Verified code locations

| Claim | File:Line | Verified |
|---|---|---|
| `SetWinEventHook` installer thread = main | `HookEngine.cpp:248-257` | ✓ |
| `WinEventProc` mutates engine via `OnFocusChanged` | `HookEngine.cpp:789-818`, `2983-3055` | ✓ |
| `ReloadFromToml` in keydown slow path | `HookEngine.cpp:521-543` | ✓ |
| `SplitDispatchInjector::Replace` Sleep 5-6ms | `SplitDispatchInjector.cpp:77` | ✓ |
| `ReplaceComposition` RichEdit retry 30ms cap | `HookEngine.cpp:3495` | ✓ |
| Incorrect WinEventProc-on-hook-thread comment | `HookEngine.cpp:794-799` | ✓ (rewrite in Phase 2) |
| 1551 gtests / 62 suites baseline | `PROJECT_MAP.md:152` | ✓ |

## Appendix B — Sprint history alignment

This design extends, not contradicts, prior sprint work:

- **Sprint 1 D5** atomic flags → Phase 2 expands to composition state.
- **Sprint 1 D6** RCU `config_` `shared_ptr` → Phase 3 generalizes to `ConfigSnapshot`.
- **Sprint 1 D8-D10** `MainThreadWorker` → Phase 3 reuses as TOML parse worker.
- **Sprint 1 D11** `recursive_mutex` → `std::mutex` → Phase 2-3 remove remaining lock usage on hook thread.
- **Sprint 2 T3** IOutputInjector strategy → Phase 5 `OutputCoordinator` formalizes if Phase 5 GO.
- **H1-H7** method-level decomposition → Phase 5 is the class-level next step, conditional.
- **H6b** (SPSC ring Hook→Engine) was wontfix 2026-05-09 for the **keystroke pipeline** (sync 0ms beats async +1-2ms). This design's `PostThreadMessage` mailbox is for **focus/config commands** (main → hook), a different scope; reviewer agreed.

## Appendix C — Commits

- `6171a82` (this session) — CODING_RULES mechanical refresh, no semantic changes. Pre-req for design clarity.
- Design implementation to land in separate PR series; see `gsd:plan-phase` for breakdown when ready.
