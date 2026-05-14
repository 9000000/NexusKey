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
│   │   ├── engine/                    # Pure C++ input engines
│   │   │   ├── IInputEngine.h         # Interface: ProcessKey, GetResult, Reset
│   │   │   ├── TypingEngine.cpp/h     # Unified typing engine — Telex + VNI both routed through here (1723 LOC, post Path G unification 2026-04-16)
│   │   │   ├── TypingAction.h         # TypingAction enum — engine output commands (Path G G-3)
│   │   │   ├── EnglishProtection.h    # Heuristics: HardEnglishStart/End, IsInvalidVietnameseCoda, V+C+V context
│   │   │   ├── PhonotacticsValidator.cpp/h  # Path 1 / hot-path syllable validator on CharState. Owns kVCPairRules per-nucleus allowed-coda bitmasks (T1 rename from SpellChecker, 813 LOC)
│   │   │   ├── IPhonotactics.h        # Interface for the rendered-text rule engine (Path G G-1)
│   │   │   ├── Phonotactics.cpp/h     # Path 2 / wstring_view rule engine — IsValidSyllable, TonePosition, CanComplete (492 LOC, T2 onset agreement + T3 N-group)
│   │   │   ├── VietnamesePhonologyData.h  # Shared phonology data — IsFrontBaseVowel today; VCPair/onset rules will land here (T2.1 phonology consolidation)
│   │   │   ├── CodeTableConverter.cpp/h  # Charset conversion TCVN3/VNI/Unicode (662 LOC)
│   │   │   ├── EngineFactory.cpp/h    # Creates engine by TypingMethod enum (Combined routes to TypingEngine)
│   │   │   ├── EngineHelpers.h        # Shared helper utilities (vowel/consonant scans, modifier targeting)
│   │   │   └── VietnameseTables.h     # Static data: kDiphthongClassic/Modern, IsTriphthong, DiphthongVowelIndex, tone-position tables
│   │   ├── config/                    # Configuration management
│   │   │   ├── TypingConfig.h         # Config struct: method, spellCheck, macros etc.
│   │   │   ├── ConfigManager.cpp/h    # TOML load/save (Win32-only, 24K cpp)
│   │   │   ├── ConfigEvent.cpp/h      # Named-event sync (Win32-only)
│   │   │   └── SettingMetadata.h      # Setting definitions: keys, types, defaults, UI labels
│   │   ├── ipc/                       # EXE ↔ DLL communication
│   │   │   ├── SharedState.h          # Memory-mapped struct: flags, config snapshot
│   │   │   ├── SharedConstants.h      # Shared memory names, sizes
│   │   │   └── SharedStateManager.cpp/h  # CreateFileMapping/MapViewOfFile (Win32)
│   │   ├── SmartSwitchManager.cpp/h   # Auto V/E switch by context
│   │   ├── SmartSwitchState.h         # State tracking for smart switch
│   │   ├── Strings.cpp/h             # Localized string dictionary (S() lookup)
│   │   ├── WinStrings.h              # Wstring ↔ UTF8 conversion (Win32-only)
│   │   ├── Debug.h                    # Debug logging macros
│   │   ├── SystemConfig.h            # System-level constants
│   │   ├── UIConfig.h                # UI-related constants
│   │   └── Version.h                 # Version string
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
│       │   ├── HookEngine.cpp/h       # Keyboard hook + dispatch (3563 LOC — largest; H1 decomposed `ProcessKeyDown` 561→79 LOC)
│       │   ├── TrayIcon.cpp/h         # System tray icon + menu (18K cpp)
│       │   ├── QuickConvert.cpp/h     # Quick consonant shortcuts (15K cpp)
│       │   ├── FloatingIcon.cpp/h     # Floating V/E indicator overlay
│       │   ├── DarkModeHelper.cpp/h   # Win32 dark mode detection + DWM attributes
│       │   ├── HotkeyManager.cpp/h    # Global hotkey registration
│       │   ├── UpdateChecker.cpp/h    # GitHub release checker
│       │   ├── UpdateInstaller.cpp/h  # Auto-update installer
│       │   ├── UpdateSecurity.cpp/h   # Update signature verification
│       │   ├── TsfRegistration.cpp/h  # Register/unregister TSF DLL
│       │   ├── ToastPopup.cpp/h       # Toast notification popup
│       │   ├── SubprocessRunners.cpp/h  # Launch subdialogs as child processes
│       │   ├── SubprocessHelper.h     # Subprocess utilities
│       │   └── StartupHelper.h        # Windows startup registration
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
├── tests/                             # Google Test (1551 tests / 62 suites as of 2026-05-08)
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
| Fix hook-based input | `src/app/system/HookEngine.cpp` |
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

### PushChar() Processing Pipeline

```
PushChar(c)
  ├─ 0a. Quick start consonant (f→ph, j→gi, w→qu) — word start only
  ├─ 0b. Quick consonant (cc→ch, gg→gi, nn→ng, ...) — after consonant
  ├─ 1a. 'z' key → clear existing tone
  ├─ 1b. Tone keys (s,f,r,x,j) → ProcessTone() → FindToneTarget()
  ├─ 2a. Modifier keys → ProcessModifier()
  │       ├─ Brackets: [ → ơ, ] → ư
  │       ├─ 'w' → ProcessWModifier() (horn/breve, 8 priority levels)
  │       ├─ Double vowel → circumflex (aa→â, ee→ê, oo→ô)
  │       │       ├─ Direct: last char matches (e.g., "a" + 'a' → "â")
  │       │       └─ Free marking: backward scan across intervening chars
  │       │               (e.g., "tieng" + 'e' → "tiêng")
  │       │               Crosses consonants freely; crosses vowels only
  │       │               when spell check validates the result
  │       └─ dd → đ (ProcessDModifier)
  ├─ 2b. Quick end consonant (g→ng, h→nh, k→ch) — after vowel
  └─ 3.  Regular character → ProcessChar()
  Then: ApplyAutoUO() + UpdateSpellState()
```

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

## Largest Files (by LOC, snapshot 2026-05-08)

| File | LOC | Notes |
|---|---|---|
| `app/system/HookEngine.cpp` | 3563 | Keyboard hook + dispatch — most complex; H1 decomposed `ProcessKeyDown` 561→79 LOC orchestrator |
| `core/engine/TypingEngine.cpp` | 1723 | Unified Telex + VNI engine (Path G) |
| `app/dialogs/SettingsDialog.cpp` | 1430 | Main settings UI logic (Sciter) |
| `app/main.cpp` | 973 | App initialization |
| `core/config/ConfigManager.cpp` | 821 | TOML config load/save |
| `core/engine/PhonotacticsValidator.cpp` | 813 | Path 1 hot-path syllable validator (T1 rename) |
| `app/system/QuickConvert.cpp` | 698 | Quick consonant shortcuts |
| `core/engine/CodeTableConverter.cpp` | 662 | Charset conversion tables |
| `app/system/TrayIcon.cpp` | 631 | Tray icon + menu |
| `tsf/EngineController.cpp` | 608 | TSF engine orchestrator |
| `core/engine/Phonotactics.cpp` | 492 | Path 2 wstring_view rule engine (T2 + T3) |
