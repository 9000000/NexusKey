# VKey — Project Map

> Vietnamese IME for Windows. Hybrid TSF + Hook, C++20, Sciter.JS UI.

## Read First

> **`docs/PHILOSOPHY.md`** — the four pillars (Nhanh / Nhẹ / Mượt / Mở rộng), test-first development, and the three pre-code questions. Highest-level filter for all design and implementation. Read before touching code.
>
> **`docs/CODE_GOVERNANCE.md`** — Q1–Q5 pre-code gate (Layer / Perf / Native / No-Lock / Trade-off). MUST print answers to all five before proposing any architecture.
>
> **`docs/CODING_RULES/index.md`** — concrete rules operationalizing the philosophy. Rule #11 (Hook System) is mandatory before changing anything reachable from `LowLevelKeyboardProc`.
>
> **`docs/REFACTOR_STATUS.md`** — living inventory of refactor work (DONE / in-flight / backlog). Single source of truth — update as items ship.
>
> **`docs/TODO.md`** — multi-day sprint plans + open follow-ups (newest at top). Currently anchors the **T2.1 Vietnamese-rule consolidation** sprint (principle-grade per anh design philosophy 2026-05-08: *nhanh - gọn - nhẹ - mượt - plugin, không code phân mảnh*).
>
> **`HANDOFF.md`** — current sprint state, gates, and recent decisions.

## Build Targets

| Target | Type | Links | Description |
|---|---|---|---|
| `VKeyEngine` | Static lib | — | Pure C++ engines, no platform deps |
| `VKeyCore` | Static lib | VKeyEngine | Platform layer: SharedState, Config |
| `VKeyTSF` | Shared lib (DLL) | VKeyCore | TSF Text Input Processor |
| `VKeyApp` | Executable | VKeyCore | Main GUI app (Sciter UI + tray) |
| `VKeyLite` | Executable | VKeyCore | Classic Win32 native UI (no Sciter). Build: `-DVKEY_LITE_MODE=ON` |
| `VKeyTests` | Executable | VKeyCore, GTest | Google Test suite |

---

## Directory Map

```
VKey/
├── src/
│   ├── core/                          # ← VKeyEngine + VKeyCore
│   │   ├── AutoCapDecision.h          # Auto-caps gate decision logic (Linux-portable)
│   │   ├── AutoCapStateTransition.h   # Auto-caps FSM rule (Linux-portable, GTest-friendly)
│   │   ├── CjkSwitchDecision.h        # CJK layout V/E auto-switch decision
│   │   ├── CommitUndoExemption.h      # Commit-undo per-app exemption matcher
│   │   ├── CrashLog.cpp/h             # Top-level exception → file log
│   │   ├── Debug.h                    # Debug logging macros
│   │   ├── DigitLedWordDecision.h     # Digit-led word treat-as-English decision
│   │   ├── Logger.cpp/h               # Runtime-toggleable file logger (HOOK_LOG backend)
│   │   ├── MacroCase.cpp/h            # Macro case-mapping (H5 CaseMapper DI seam)
│   │   ├── MacroPrefix.h              # Macro prefix matching
│   │   ├── SmartSwitchManager.cpp/h   # Auto V/E switch by context
│   │   ├── SmartSwitchState.h         # State tracking for smart switch
│   │   ├── Strings.cpp/h              # Localized string dictionary (S() lookup)
│   │   ├── SystemConfig.h             # System-level constants
│   │   ├── UIConfig.h                 # UI-related constants
│   │   ├── Version.h                  # Version string
│   │   ├── WinStrings.h               # Wstring ↔ UTF8 conversion (Win32-only)
│   │   ├── engine/                    # Pure C++ input engines
│   │   │   ├── IInputEngine.h         # Interface: ProcessKey, GetResult, Reset
│   │   │   ├── TypingEngine.cpp/h     # Unified typing engine — Telex + VNI both routed through here (2000 LOC; post W7 framework, PushChar is a 73-LOC skeleton dispatching to engine rules)
│   │   │   ├── TypingAction.h         # TypingAction enum — engine output commands (Path G G-3)
│   │   │   ├── EnglishProtection.h    # Heuristics: HardEnglishStart/End, IsInvalidVietnameseCoda, V+C+V context
│   │   │   ├── PhonotacticsValidator.cpp/h  # Path 1 / hot-path syllable validator on CharState. Owns kVCPairRules per-nucleus allowed-coda bitmasks (T1 rename from SpellChecker, 813 LOC)
│   │   │   ├── IPhonotactics.h        # Interface for the rendered-text rule engine (Path G G-1)
│   │   │   ├── Phonotactics.cpp/h     # Path 2 / wstring_view rule engine — IsValidSyllable, TonePosition, CanComplete (492 LOC, T2 onset agreement + T3 N-group)
│   │   │   ├── VietnamesePhonologyData.h  # Shared phonology data — IsFrontBaseVowel today; VCPair/onset rules will land here (T2.1 phonology consolidation)
│   │   │   ├── CodeTableConverter.cpp/h  # Charset conversion TCVN3/VNI/Unicode (662 LOC)
│   │   │   ├── EngineFactory.cpp/h    # Creates engine by TypingMethod enum (Combined routes to TypingEngine)
│   │   │   ├── EngineHelpers.h        # Shared helper utilities (vowel/consonant scans, modifier targeting)
│   │   │   ├── VietnameseTables.h     # Static data: kDiphthongClassic/Modern, IsTriphthong, DiphthongVowelIndex, tone-position tables
│   │   │   └── rule/                  # ← W7 engine-rule plugin framework (2026-05-23)
│   │   │       ├── EngineRulePhase.h          # PreClassify / PostClassify enum
│   │   │       ├── EngineRuleResult.h         # Pass / Handled / Veto
│   │   │       ├── EngineRuleContext.h        # POD: keyChar, lower, isUpper, action, gate inputs + LIVE refs to states_/rawInput_/config_
│   │   │       ├── IEngineRule.h              # Plugin interface (RulePhase / Priority / Requires / Apply)
│   │   │       ├── EngineRuleRegistry.cpp/h   # Per-phase priority-sorted dispatch + engine-local gate eval (reuses Pipeline::GateMask)
│   │   │       ├── IToneExecutor.h            # Executor port for ToneRule → HandleToneFsm
│   │   │       ├── ToneRule.cpp/h             # PostClassify prio 10, Requires=ToneEscape — Clear / ApplyTone* actions
│   │   │       ├── IModifierExecutor.h        # Executor port for ModifierRule → HandleModifierAction
│   │   │       ├── ModifierRule.cpp/h         # PostClassify prio 20, Requires=0 — Telex / VNI / UserDefined modifiers
│   │   │       ├── IQuickConsonantExecutor.h  # Executor port for both QuickStart and QuickEnd rules
│   │   │       ├── QuickStartConsonantRule.cpp/h  # PreClassify prio 5, Requires=0 — 0a (f→ph, j→gi, w→qu) + 0a-cont undo + 0b (cc→ch family + uu→ươ)
│   │   │       └── QuickEndConsonantRule.cpp/h    # PostClassify prio 30, Requires=0 — 2c (g→ng, h→nh, k→ch after vowel)
│   │   ├── pipeline/                  # ← W1-W5 HookEngine-layer plugin framework (2026-05-22..23)
│   │   │   ├── Stage.h                # PreEngine / Engine / PostEngine
│   │   │   ├── Result.h               # Pass / Handled / Veto
│   │   │   ├── GateMask.h             # GateId enum (EnglishBias / SpellCheck / ToneEscape) + GateMaskFor()
│   │   │   ├── KeyContext.h           # POD passed to features (vk, keyChar, mods, session, reinjectVk)
│   │   │   ├── Intent.h               # Variant: Backspace / Text / Reinject / ConsumeKey / PassThrough
│   │   │   ├── IntentSink.h           # Output channel — Emit(Intent)
│   │   │   ├── IFeature.h             # Plugin interface
│   │   │   ├── IGate.h                # Gate interface — IsRaised(ctx)
│   │   │   ├── ICompositionSession.h  # PreviousRendered / EngineRendered / RawInput views
│   │   │   ├── HookCompositionSession.h  # ICompositionSession impl for HookEngine
│   │   │   ├── Coordinator.cpp/h      # Owns features + gates, HandleKey / HandleKeyAtStage dispatch
│   │   │   ├── OutputChannel.cpp/h    # Batches Intent emissions to flush at end of key
│   │   │   ├── IBackwardEditExecutor.h    # Executor port → BackwardEditFeature
│   │   │   ├── BackwardEditFeature.cpp/h  # PostEngine prio 10
│   │   │   ├── ICommitUndoExecutor.h      # Executor port → CommitUndoFeature
│   │   │   ├── CommitUndoFeature.cpp/h    # PreEngine prio 20 — backspace-into-committed-word FSM
│   │   │   ├── IMacroExecutor.h           # Executor port → MacroFeature
│   │   │   ├── MacroFeature.cpp/h         # PreEngine prio 30 — macro expansion
│   │   │   ├── IEscRestoreRawExecutor.h   # Executor port → EscRestoreRawFeature
│   │   │   ├── EscRestoreRawFeature.cpp/h # PreEngine prio 40 — ESC restores raw keystrokes
│   │   │   └── gates/
│   │   │       ├── EnglishBiasGate.h      # Reads vietnameseMode_ atomic
│   │   │       ├── SpellCheckGate.h       # Reads engine_->IsEnglishWord()
│   │   │       └── ToneEscapeGate.h       # Reads engine_->IsToneEscaped()
│   │   ├── config/                    # Configuration management
│   │   │   ├── TypingConfig.h         # Config struct: method, spellCheck, macros etc.
│   │   │   ├── ConfigManager.cpp/h    # TOML load/save (Win32-only, 24K cpp)
│   │   │   ├── ConfigEvent.cpp/h      # Named-event sync (Win32-only)
│   │   │   └── SettingMetadata.h      # Setting definitions: keys, types, defaults, UI labels
│   │   ├── ipc/                       # EXE ↔ DLL communication
│   │   │   ├── SharedState.h          # Memory-mapped struct: flags, config snapshot
│   │   │   ├── SharedConstants.h      # Shared memory names, sizes
│   │   │   ├── SecurityHelpers.h      # ACL / DACL helpers for named-object hardening
│   │   │   ├── SharedStateManager.cpp/h  # CreateFileMapping/MapViewOfFile (Win32)
│   │   │   └── (SharedState.h hosts the 429-LOC IPC contract)
│   │   └── hotkey/                    # Unified hotkey registry (cancel-composition / skip-macro / toggle-enabled)
│   │       ├── HotkeyRegistry.cpp/h   # Atomic RCU-published binding table (factory defaults + slot dispatch)
│   │       └── HotkeyLabel.cpp/h      # Human-readable binding labels for the settings UI
│   │
│   ├── tsf/                           # ← VKeyTSF (DLL)
│   │   ├── TextService.cpp/h          # ITfTextInputProcessorEx — TSF entry point
│   │   ├── KeyEventSink.cpp/h         # ITfKeyEventSink — keystroke handling (OnKeyDown must be self-sufficient: Chromium hosts skip OnTestKeyDown)
│   │   ├── EngineController.cpp/h     # Owns IInputEngine, orchestrates typing (17K cpp)
│   │   ├── CompositionManager.cpp/h   # ITfComposition management
│   │   ├── LanguageBarButton.cpp/h    # V/E toggle on language bar (11K cpp)
│   │   ├── EditSession.h             # ITfEditSession base wrapper
│   │   ├── CompositionEditSession.h   # Edit session for composition text
│   │   ├── InputScopeChecker.h        # Detect password/URL fields
│   │   ├── DisplayAttribute.h         # Underline style for composition
│   │   ├── Register.cpp              # DLL (un)registration in registry
│   │   ├── dllmain.cpp               # DLL entry point
│   │   ├── Globals.cpp/h             # DLL-wide globals (CLSID, refcount)
│   │   ├── ComUtils.h               # COM helper macros
│   │   ├── Define.h                  # TSF-specific constants
│   │   └── stdafx.h                  # Precompiled header
│   │
│   └── app/                           # ← VKeyApp (EXE)
│       ├── main.cpp                   # WinMain, message loop, init (23K)
│       ├── dialogs/                   # Sciter-based dialog windows
│       │   ├── SettingsDialog.cpp/h   # Main settings (45K cpp — largest file)
│       │   ├── SciterSubDialog.cpp/h  # Base class for subprocess dialogs
│       │   ├── SubDialogConfig.h      # Config struct for subdialogs
│       │   ├── ExcludedAppsDialog.cpp/h  # App exclusion list
│       │   ├── TsfAppsDialog.cpp/h    # TSF-enabled apps list
│       │   ├── MacroTableDialog.cpp/h # Macro editor
│       │   ├── ConvertToolDialog.cpp/h  # Charset converter UI (18K cpp)
│       │   └── AboutDialog.cpp/h      # About box
│       ├── output/                    # Output injection plugin layer (Sprint 2 T3 — `IOutputInjector` pattern, the canonical plugin example for the project)
│       │   ├── IOutputInjector.h      # Interface — Replace(bsCount, text), SendKey(vk), trait queries
│       │   ├── OutputInjectorFactory.cpp/h  # Builds the right injector from WindowClassification (Electron / Console / RichEditD2DPT / Win32)
│       │   ├── Win32SendInputInjector.cpp/h  # Default fast path — batch SendInput
│       │   ├── RichEditEmReplaceSelInjector.cpp/h  # Win11 New Notepad RichEditD2DPT (Sprint 1 D12)
│       │   ├── SplitDispatchInjector.cpp/h  # Electron (6 ms) + Console (5 ms) — split SendInput with sleep
│       │   └── Internal.cpp/h         # Shared TrackedSendInput primitive
│       ├── system/                    # System-level services
│       │   ├── HookEngine.cpp/h       # Coordinator + dispatch chain (3393 LOC post Wave 3; H1 decomposed `ProcessKeyDown` 561→79 LOC; Wave 3 carved focus / output / commit-state / lifecycle into separate owners — see below)
│       │   │                          # ── Wave 3 owner classes (carved from HookEngine, byte-identical refactor) ──
│       │   ├── HookLifecycle.cpp/h    # PR 3.1 — dedicated WH_KEYBOARD_LL/WH_MOUSE_LL hook thread, HHOOK handles, mailbox wake-up plumbing, start-time handshake CV
│       │   ├── FocusOwner.cpp/h       # PR 3.2 — WinEvent hooks (FOREGROUND+MINIMIZEEND), foreground PID/exe tracker, focused-child cache, AppProfile LRU classify cache, smart-switch map, CJK layout state, WebView2 detection (640 LOC cpp)
│       │   ├── OutputDispatcher.cpp/h # PR 3.3 — IOutputInjector RCU publish, ReplaceUnicode orchestrator (RichEdit retry → clipboard fallback → generic), synth-event counter, EM_REPLACESEL fast path, ClipboardPaste (551 LOC cpp)
│       │   ├── CommitState.h          # PR 3.4 — header-only commit-undo state machine (Idle/Ready/Primed FSM + CommitEntry LIFO stack + leading-trigger snapshot + input-history replay buffer)
│       │   ├── HookCommandMailbox.cpp/h  # Cross-thread bit-flag mailbox (FocusChange / ConfigReload / TickPoll / ToggleVN) — wake-once trampoline + FocusClassification POD
│       │   ├── HotkeyManager.cpp/h    # Global hotkey registration + LL-callback slot dispatch
│       │   ├── HotkeyWiring.cpp/h     # EXE-side hotkey ↔ feature wiring (cancel composition / skip macro / toggle V-E)
│       │   ├── MainThreadWorker.cpp/h # Sprint 1 D10 — main-thread tick worker (200ms cadence; OnTickPoll / config-reload / heartbeat publishing)
│       │   ├── HeartbeatPublisher.cpp/h  # Watchdog heartbeat (counter + timestamp into SharedState)
│       │   ├── WatchdogController.cpp/h  # External watchdog process supervisor — restart on hang/crash
│       │   ├── PendingDllApply.cpp/h  # DLL update pending-apply state (download done, waiting for reboot/relaunch)
│       │   ├── PerfHistogram.cpp/h    # Phase 1 perf scope timer + bucketed histogram log (PERF_SCOPE macro)
│       │   ├── DebugLogWarning.h      # `[[deprecated]]` shim — fires a build warning if a non-runtime log call sneaks in
│       │   ├── Win32CaseMapper.h      # CaseMapper DI (H5 seam): Win32 LCMapStringEx adapter for MacroCase tests
│       │   ├── TrayIcon.cpp/h         # System tray icon + menu (623 LOC cpp)
│       │   ├── QuickConvert.cpp/h     # Quick consonant shortcuts (714 LOC cpp)
│       │   ├── FloatingIcon.cpp/h     # Floating V/E indicator overlay (334 LOC cpp)
│       │   ├── DarkModeHelper.cpp/h   # Win32 dark mode detection + DWM attributes
│       │   ├── UpdateChecker.cpp/h    # GitHub release checker (373 LOC cpp)
│       │   ├── UpdateInstaller.cpp/h  # Auto-update installer (442 LOC cpp)
│       │   ├── UpdateSecurity.cpp/h   # Update signature verification (263 LOC cpp)
│       │   ├── TsfRegistration.cpp/h  # Register/unregister TSF DLL (312 LOC cpp)
│       │   ├── ToastPopup.cpp/h       # Toast notification popup (259 LOC cpp)
│       │   ├── SubprocessRunners.cpp/h  # Launch subdialogs as child processes (181 LOC cpp)
│       │   ├── SubprocessHelper.h     # Subprocess utilities
│       │   └── StartupHelper.h        # Windows startup registration (646 LOC — registry helpers, manifest helpers)
│       ├── sciter/                    # Sciter integration layer
│       │   ├── SciterHelper.cpp/h     # Init Sciter, load HTML, callbacks
│       │   ├── SciterArchive.cpp/h    # Embedded resource archive
│       │   └── ScaleHelper.h          # DPI scaling helpers
│       ├── classic/                    # ← VKeyLite (Classic Win32 UI, no Sciter)
│       │   ├── ClassicSettingsDialog.cpp/h  # Settings dialog (Win32 native controls)
│       │   ├── ClassicTheme.cpp/h     # Dark/light theme for Win32 controls
│       │   ├── VKeyLite.rc        # Win32 resource file
│       │   ├── VKeyLite.exe.manifest  # DPI + visual styles manifest
│       │   └── resource.h             # Resource IDs
│       ├── main_lite.cpp              # WinMain for Lite/Classic build
│       ├── helpers/
│       │   └── AppHelpers.h           # App-level utility functions
│       ├── ui/                        # HTML/CSS/JS for Sciter dialogs
│       │   ├── settings/              # settings.html/css/js
│       │   ├── excludedapps/          # excludedapps.html/css/js
│       │   ├── tsfapps/              # tsfapps.html/css/js
│       │   ├── macro/                # macro.html/css/js
│       │   ├── appoverrides/         # appoverrides.html/css/js
│       │   ├── convert-tool/         # convert-tool.html/css/js
│       │   ├── about/                # about.html/css
│       │   └── shared/               # Shared UI resources
│       │       ├── theme.css          # Design tokens, colors (10K)
│       │       ├── utils.js           # DOM helpers, event binding (11K)
│       │       ├── strings.js         # i18n string tables (6K)
│       │       ├── i18n.js            # Localization engine
│       │       ├── subdialog.css      # Common subdialog styles
│       │       ├── dropdown.css       # Custom dropdown component
│       │       ├── toggle.css         # Toggle switch component
│       │       └── base.css           # Reset/base styles
│       ├── resources.cpp              # Auto-generated packed UI (538K)
│       └── resources/                 # Icons (ico files)
│
├── tests/                             # Google Test (2011 tests / 117 suites as of 2026-05-24, feat/architecture-review-v3.1 post Wave 3 PR 3.5)
│   ├── TelexEngineTest.cpp            # ★ Most comprehensive engine suite (covers TypingEngine post-Path G; legacy filename retained)
│   ├── VniParityTest.cpp              # VNI ↔ Telex parity through unified TypingEngine
│   ├── CombinedEngineTest.cpp         # InputMethod::Combined routing
│   ├── PhonotacticsTest.cpp           # Path 2 wstring_view rule engine (T2 onset agreement + T3 N-group)
│   ├── PhonotacticsValidatorTest.cpp  # Path 1 CharState validator (T1 rename from SpellCheckerTest)
│   ├── FeatureOptionsTest.cpp         # Feature flag combinations
│   ├── CodeTableConverterTest.cpp     # Charset conversion correctness
│   ├── TelexDictionaryTest.cpp        # Telex dictionary lookups
│   ├── CustomKeyMapTest.cpp           # Path G G-4 custom keymap
│   ├── TypingActionTest.cpp           # Path G G-3 TypingAction enum
│   ├── MacroCaseTest.cpp              # H5 macro case-mapping (CaseMapper DI)
│   ├── MacroPrefixTest.cpp            # Macro prefix expansion
│   ├── MainThreadWorkerTests.cpp      # Sprint 1 D8-D10 worker queue
│   ├── HookContextAnchorTest.cpp      # Hook seqlock anchor read/write
│   ├── HookEngineAtomicTests.cpp      # Sprint 1 D5 atomic flag migration
│   ├── TypingConfigRCUTests.cpp       # Sprint 1 D6 shared_ptr<TypingConfig> RCU
│   ├── EngineBenchmarkTest.cpp        # Hot-path latency budget
│   ├── EngineFactoryTest.cpp          # Engine construction by method enum
│   ├── ConfigManagerTest.cpp          # Win32 only — TOML round-trip
│   ├── SharedStateTest.cpp            # Win32 only — IPC seqlock + ABI gate
│   ├── ConfigEventTest.cpp            # Win32 only — named-event sync
│   ├── UpdateSecurityTest.cpp         # UpdateChecker signature verification
│   ├── output/                        # Win32-only — output injector tests (Sprint 2 T3)
│   │   ├── InjectorTestBase.h
│   │   ├── InjectorTraitsTest.cpp     # ChannelTraits virtual-method coverage
│   │   ├── OutputInjectorFactoryTest.cpp  # WindowClassification → injector mapping
│   │   ├── Win32SendInputInjectorTest.cpp
│   │   ├── RichEditEmReplaceSelInjectorTest.cpp
│   │   └── SplitDispatchInjectorTest.cpp
│   ├── pipeline/                      # W1-W5 feature pipeline tests (HookEngine layer)
│   │   ├── EnumsTest.cpp / IntentTest.cpp / IntentSinkTest.cpp
│   │   ├── CoordinatorRegistryTest.cpp / CoordinatorDispatchTest.cpp
│   │   ├── CoordinatorStageDispatchTest.cpp / CoordinatorGateFilterTest.cpp
│   │   ├── OutputChannelTest.cpp / HookCompositionSessionTest.cpp
│   │   ├── EnglishBiasGateTest.cpp / SpellCheckGateTest.cpp / ToneEscapeGateTest.cpp
│   │   ├── BackwardEditFeatureTest.cpp / CommitUndoFeatureTest.cpp
│   │   └── MacroFeatureTest.cpp / EscRestoreRawFeatureTest.cpp
│   ├── engine/                        # W7 engine-rule tests
│   │   ├── EngineRuleRegistryTest.cpp     # Framework dispatch + gate filter
│   │   ├── ToneRuleTest.cpp / ModifierRuleTest.cpp
│   │   └── QuickStartConsonantRuleTest.cpp / QuickEndConsonantRuleTest.cpp
│   └── TestHelper.h                   # Shared test utilities
│
├── docs/                              # Documentation (see docs/index.md)
│   ├── Architecture/                  # System architecture (sharded, 12 files)
│   ├── CODING_RULES/                  # Coding standards (sharded, 10 files)
│   ├── telex-test-specification/      # Telex test cases (sharded, 21 files)
│   ├── vietnamese-phonology-spec-distillate.md  # Typing rules (distilled)
│   ├── SECURITY_FIXES-distillate.md   # Security fixes (distilled)
│   ├── tray-icon-sync-analysis-v2-distillate.md # Tray icon sync (distilled)
│   ├── Planning.md                    # Feature roadmap (24K)
│   ├── tsf-hook-coordination.md       # TSF/Hook interplay (4K)
│   ├── subdialog-checklist.md         # Subdialog creation guide (4K)
│   ├── plans/                         # Implementation plans
│   └── diagrams/                      # Visual diagrams
│
├── extern/                            # Vendored dependencies
│   ├── googletest/                    # Google Test v1.14.0
│   ├── tomlplusplus/                  # TOML parser
│   └── sciter/                        # Sciter SDK (JS engine + UI)
│       └── bin/packfolder.exe         # Pack UI → resources.cpp
│
├── CMakeLists.txt                     # Build system (single file)
├── CLAUDE.md                          # AI assistant context
├── BUILD.md                           # Build instructions
└── README.md                          # Project overview
```

---

## Quick Reference: Where To Look

| Task | Go to |
|---|---|
| Fix typing/diacritics bug | `src/core/engine/TypingEngine.cpp` (unified Telex + VNI post Path G) |
| Fix syllable validity / tone gating | `src/core/engine/PhonotacticsValidator.cpp` (Path 1, hot, gates by `spellCheckEnabled`) |
| Fix rendered-text rule engine (interface contract) | `src/core/engine/Phonotactics.cpp` (Path 2 / wstring_view) |
| Adjust shared phonology rule data | `src/core/engine/VietnamesePhonologyData.h` (T2.1 consolidation in progress) |
| Fix English bias / V+C+V detection | `src/core/engine/EnglishProtection.h` |
| Change encoding conversion | `src/core/engine/CodeTableConverter.cpp` |
| Add/change config option | `src/core/config/TypingConfig.h` → `ConfigManager.cpp` |
| Fix EXE↔DLL sync | `src/core/ipc/SharedState.h` → `SharedStateManager.cpp` |
| Fix TSF composition | `src/tsf/EngineController.cpp` → `CompositionManager.cpp` |
| Fix TSF key handling | `src/tsf/KeyEventSink.cpp` |
| Fix language bar icon | `src/tsf/LanguageBarButton.cpp` |
| Fix hook-based input | `src/app/system/HookEngine.cpp` (coordinator + dispatch chain — see Wave 3 carve table below) |
| Fix focus tracking / WinEvent / smart-switch / CJK layout | `src/app/system/FocusOwner.cpp` (Wave 3 PR 3.2) |
| Fix output dispatch / RichEdit retry / clipboard fallback / synth-event counter | `src/app/system/OutputDispatcher.cpp` (Wave 3 PR 3.3) |
| Fix commit-undo FSM / backspace-into-committed-word / commit stack | `src/app/system/CommitState.h` + `HookEngine.cpp::HandleCommitUndoFsm` (Wave 3 PR 3.4 — orchestration stays in HookEngine) |
| Fix LL hook lifecycle / dedicated hook thread / hook reinstall | `src/app/system/HookLifecycle.cpp` (Wave 3 PR 3.1) |
| Add/change output channel (Electron/Console/RichEdit/Win32) | `src/app/output/` — extend `IOutputInjector`, register in `OutputInjectorFactory` |
| Change settings UI | `src/app/dialogs/SettingsDialog.cpp` + `src/app/ui/settings/` |
| Add new subdialog | See `docs/subdialog-checklist.md` |
| Fix tray icon/menu | `src/app/system/TrayIcon.cpp` |
| Fix quick consonant | `src/app/system/QuickConvert.cpp` |
| Change Classic/Lite UI | `src/app/classic/ClassicSettingsDialog.cpp` + `ClassicTheme.cpp` |
| Change themes/colors | `src/app/ui/shared/theme.css` (Sciter) or `ClassicTheme.cpp` (Classic) |
| Add/change i18n strings | `src/app/ui/shared/strings.js` + `i18n.js` |
| Fix auto-update | `src/app/system/UpdateChecker.cpp` → `UpdateInstaller.cpp` |
| Fix hotkey handling | `src/app/system/HotkeyManager.cpp` |
| Add tests | `tests/` — follow existing `*Test.cpp` pattern |

---

## Data Flow

```
┌──────────────────── VKeyApp (EXE) ────────────────────┐
│  main.cpp → TrayIcon → SettingsDialog → ConfigManager    │
│                ↕              ↕                           │
│         HookEngine    SciterSubDialogs                   │
│         (fallback)    (subprocess UI)                    │
└────────────┬─────────────────────────────────────────────┘
             │  SharedState (memory-mapped file)
             │  ConfigEvent (named event)
┌────────────┴─────────────────────────────────────────────┐
│  VKeyTSF (DLL) — loaded per-process by Windows        │
│  TextService → KeyEventSink → EngineController           │
│     ↕              ↕               ↕                     │
│  Register   CompositionManager  TelexEngine/VniEngine    │
│             LanguageBarButton   SpellChecker             │
└──────────────────────────────────────────────────────────┘
```

---

## TypingEngine Internal Architecture

> Read this section before debugging any diacritics/tone bug in `TypingEngine.cpp`. Telex + VNI both flow through this single engine post Path G unification (2026-04-16); the legacy `TelexEngine`/`VniEngine` files no longer exist.

### PushChar() Processing Pipeline (post W7 framework)

Since W7.4 (2026-05-23), `PushChar` is a ~73 LOC skeleton that delegates all sub-steps to engine rules via `EngineRuleRegistry`. Rules live in `src/core/engine/rule/`. **Read `docs/plans/2026-05-23-feature-pipeline-w7-retro.md` before touching this code** — it captures the 10 architecture decisions (ADs) that shape the framework.

```
PushChar(c)
  ├─ rawInput_.push_back + escRawHistory_.push_back + qc_.onlyQC = false
  ├─ Build EngineRuleContext (keyChar, lower, isUpper, gate inputs, live state refs)
  ├─ ruleRegistry_.DispatchAtPhase(PreClassify)
  │     └─ QuickStartConsonantRule (prio 5, Requires=0)
  │           ├─ 0a:      f→ph, j→gi, w→qu (only at word start)        ↦ Veto on match
  │           ├─ 0a-cont: undo quick start if next char ≠ vowel        ↦ Pass (fall through)
  │           └─ 0b:      cc→ch family + uu→ươ                          ↦ Veto on match
  │                 (cc→ch path: ProcessChar(newKey) + FinalizeRegularChar)
  ├─ Detect VNI digit sequence (literal protection)
  ├─ ClassifyKey(lower, mode flags) → TypingAction (Tone* / Modifier* / Insert* / None)
  ├─ ruleRegistry_.DispatchAtPhase(PostClassify)
  │     ├─ ToneRule (prio 10, Requires=ToneEscape)
  │     │     └─ HandleToneFsm: ClearTone, ToneAcute/Grave/Hook/Tilde/Dot
  │     │           ├─ Spell-check disabled path (literal-or-recover)
  │     │           ├─ English protection (HardEnglish bias, V+C+V structural check)
  │     │           ├─ Stop-final coda guard (block grave/hook/tilde on c/ch/p/t coda)
  │     │           └─ ProcessTone(requestedTone, cachedTarget) → FindToneTarget()
  │     ├─ ModifierRule (prio 20, Requires=0)
  │     │     └─ HandleModifierAction → ProcessModifier()
  │     │           ├─ Telex: aa→â, ee→ê, oo→ô, w→ư/ơ/ă (8 priority levels), dd→đ
  │     │           ├─ Brackets: [ → ơ, ] → ư
  │     │           ├─ VNI: 6→circumflex, 7→horn, 8→breve, 9→đ
  │     │           └─ UserDefined: HornOrInsertU(NoStart) / UndoAllMarks / Insert*
  │     └─ QuickEndConsonantRule (prio 30, Requires=0)
  │           └─ 2c: g→ng, h→nh, k→ch (after vowel)                     ↦ Veto on match
  └─ Step 3 (regular character — no rule consumed the key):
        ProcessChar(c) + FinalizeRegularChar()
              └─ RelocateToneToTarget + ApplyAutoUO + UpdateSpellState
                  + CheckEnglishBias + P8 w→ư revert (Telex) + CheckZwjfInitialBias
```

**Veto** semantics: rule mutated engine state and the keystroke is fully consumed → `PushChar` returns early. **Pass**: rule did not act (or 0a-cont fall-through); next rule in phase runs; if all pass → step 3.

**State (qc_, quickStartKey_, escape_, engProt_) lives on TypingEngine.** Rules access via `engine.` calls inside executor bodies. Backspace + Reset + IsRestoreCandidate still own these fields (AD-7 in retro doc).

### Tone Placement Priority (FindToneTargetImpl)

```
FindToneTarget()
  ├─ P1: Horn vowel (ư, ơ) — last one wins (for ươ pair)
  ├─ P2: Modified vowel (â, ê, ô, ă) — first one found
  ├─ P3: Diphthong/triphthong table lookup
  │       ├─ Triphthong (modern only): tone on MIDDLE vowel (oai, uyu, ...)
  │       └─ Diphthong: kDiphthongClassic/Modern in VietnameseTables.h
  │           rule=1 → first vowel
  │           rule=2 → second vowel
  │           rule=3 → coda-aware (SECOND if coda exists, ELSE FIRST)
  └─ P4: Default → rightmost vowel
  Special: "gi" cluster ('i' skipped), "qu" cluster ('u' skipped)
```

### Modifier Application Priority (ProcessWModifier)

```
ProcessWModifier()
  P1: "ua" → horn on 'u' (mưa)     P5: standalone 'u' → horn
  P2: "uo" → horn on 'o' (ươ)      P6: standalone 'o' → horn
  P3: "oa" → breve on 'a' (hoặc)   P7: standalone 'a' → breve
  P4: Escape (clear horn/breve)     P8: no target → insert 'ư'
```

### Key Interactions Between Subsystems

| Action | Triggers | Why it matters |
|---|---|---|
| Circumflex applied | → `RelocateToneToTarget()` | Tone may need to move to newly-modified vowel |
| Regular char added | → `RelocateToneToTarget()` | Tone relocates when a new vowel forms a diphthong/coda (e.g. hofa -> hoà) |
| Horn applied | → `RelocateToneToHornVowel()` | Horn vowels have highest tone priority |
| Any char after ơ | → `ApplyAutoUO()` | Auto-horns preceding 'u' (u+ơ → ư+ơ) |
| Every PushChar/Backspace | → `UpdateSpellState()` | Sets `spellCheckDisabled_` if invalid syllable |
| `spellCheckDisabled_` = true | Tone keys become literal chars | Free marking blocked, tone blocked |
| Free marking crosses vowels | → `Phonology::ValidateSyllableState()` | Tentative apply + validate, undo if invalid (Path 1 hot-path validator in `PhonotacticsValidator.cpp`) |

### State Model (CharState)

Each character in the buffer is a `CharState` with: `base` (lowercase letter), `mod` (None/Circumflex/Breve/Horn), `tone` (None/Acute/Grave/Hook/Tilde/Dot), `isUpper`, `rawIdx`, `toneRawIdx`. Composition (`Compose()`) combines base+mod+tone into a single Unicode character via flat array lookups in `VietnameseTables.h`.

---

## Largest Files (by LOC, snapshot 2026-05-24 — feat/architecture-review-v3.1 post Wave 3 PR 3.5)

| File | LOC | Notes |
|---|---|---|
| `app/system/HookEngine.cpp` | 3393 | Coordinator + dispatch chain. Wave 3 carved focus/output/commit-state/lifecycle into owner classes (~2116 LOC migrated). Pre-Wave-3: 4431. Still hosts: ProcessKeyDown orchestrator, HandleCommitUndoFsm body (289 LOC — kept for engine_ glue locality), HandlePreDispatch/DispatchKeyAction chain, hook-command drain handlers. |
| `core/engine/TypingEngine.cpp` | 2000 | Unified Telex + VNI engine (Path G); W7 framework moved tone / modifier / quick-consonant sub-blocks into `src/core/engine/rule/` plugins. `PushChar` is now a 73-LOC skeleton (was ~210 inline). |
| `app/dialogs/SettingsDialog.cpp` | 1506 | Main settings UI logic (Sciter) |
| `core/config/ConfigManager.cpp` | 1222 | TOML config load/save (incl. Wave 2 RCU snapshot rebuild) |
| `app/main.cpp` | 1020 | App initialization |
| `core/engine/PhonotacticsValidator.cpp` | 813 | Path 1 hot-path syllable validator (T1 rename) |
| `app/system/QuickConvert.cpp` | 714 | Quick consonant shortcuts |
| `app/main_lite.cpp` | 703 | WinMain for VKeyLite (Classic Win32 UI build) |
| `core/engine/CodeTableConverter.cpp` | 662 | Charset conversion tables |
| `app/system/StartupHelper.h` | 646 | Windows startup / autorun helpers (header-heavy) |
| `app/system/FocusOwner.cpp` | 640 | Wave 3 PR 3.2 — focus/CJK/smart-switch/AppProfile cache/WebView2 detection |
| `app/system/HookEngine.h` | 625 | Public surface + private declarations (Wave 3 trimmed 6 LOC; was 631) |
| `app/system/TrayIcon.cpp` | 623 | Tray icon + menu |
| `tsf/EngineController.cpp` | 608 | TSF engine orchestrator |
| `app/system/OutputDispatcher.cpp` | 551 | Wave 3 PR 3.3 — output dispatch orchestrator (RichEdit retry / clipboard fallback / SendInput) |
| `app/dialogs/ConvertToolDialog.cpp` | 529 | Charset converter subdialog |
| `core/engine/Phonotactics.cpp` | 492 | Path 2 wstring_view rule engine (T2 + T3) |

### Wave 3 carve table (snapshot 2026-05-24 — `feat/architecture-review-v3.1`)

| PR | Owner extracted | HookEngine.cpp Δ | Owner LOC | Commit |
|---|---|---|---|---|
| 3.1 | `HookLifecycle` (thread + LL hooks + mailbox + reinstall pump) | baseline | 215 cpp + 112 h | `21c6afc` |
| 3.2 | `FocusOwner` (WinEvent + classify + smart-switch + CJK + WebView2) | -563 → 3868 | 640 cpp + 226 h | `cb2bcb7` |
| 3.3 | `OutputDispatcher` (injector RCU + ReplaceUnicode + clipboard + synth counter) | -467 → 3401 | 551 cpp + 204 h | `a109994` |
| 3.4 | `CommitState` (Idle/Ready/Primed FSM + LIFO stack — header-only) | -6 → 3395 | 168 h | `5323850` |
| 3.5 | Cleanup (dead aliases + header diet) | -2 → 3393 | — | `6e8bf67` |

Net: ~2116 LOC migrated from HookEngine into 4 single-concern owners. Zero behavior change (byte-identical chaos 54/55 across all 4 verification runs — chrome `1.3-escape-bs-aa-b` environmental flake stable).
