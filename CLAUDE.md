# NexusKey

Vietnamese IME (Input Method Editor) for Windows. Hybrid TSF + Hook fallback, C++20, Sciter.JS UI.

## Build & Test

```bash
# Linux — tests only (no Windows APIs)
cmake -B build-linux -G "Unix Makefiles" -DCMAKE_BUILD_TYPE=Debug
cmake --build build-linux --target NextKeyTests
./build-linux/tests/NextKeyTests

# Run specific test suite
./build-linux/tests/NextKeyTests --gtest_filter="TelexEngineTest.*"

# Windows — full build (from WSL)
powershell.exe -Command "cd '\\wsl.localhost\Ubuntu-24.04\home\phatmt\code\NexusKey\build'; cmake --build . --target NexusKey --config Debug 2>&1"

# Pack Sciter UI resources (Release builds)
extern/sciter/bin/packfolder.exe src/app/ui src/app/resources.cpp -v resources
```

## Project Structure

```
src/
  core/engine/     # Pure C++ engines: TelexEngine, VniEngine, SpellChecker, IInputEngine interface
  core/config/     # TypingConfig, ConfigManager (Win32), ConfigEvent
  core/ipc/        # SharedState, SharedStateManager — EXE<->DLL communication
  tsf/             # TSF DLL (TextService, KeyEventSink, CompositionManager, LanguageBarButton)
  app/             # EXE entry point, Win32 GUI
    dialogs/       # Sciter-based dialogs (Settings, About, MacroTable, etc.)
    system/        # TrayIcon, HookEngine, HotkeyManager, UpdateChecker, TsfRegistration
    sciter/        # SciterHelper, SciterArchive, ScaleHelper
    ui/            # HTML/CSS/JS for Sciter dialogs
tests/             # Google Test — TelexEngineTest, VniEngineTest, SpellCheckerTest, etc.
extern/            # Vendored: googletest, tomlplusplus, sciter SDK
docs/              # Architecture.md, CODING_RULES.md, Planning.md, specs
```

## CMake Targets

| Target | Type | Description |
|---|---|---|
| `NextKeyEngine` | Static lib | Pure C++ engines, no platform deps |
| `NextKeyCore` | Static lib | Platform layer: SharedState, Config, links NextKeyEngine |
| `NextKeyTSF` | Shared lib (DLL) | TSF Text Input Processor, links NextKeyCore |
| `NextKeyApp` | Executable | Main GUI app (Sciter UI + tray), links NextKeyCore |
| `NextKeyTests` | Executable | Google Test suite, `EXCLUDE_FROM_ALL` |

## Architecture Summary

- **EXE** (NextKeyApp): Settings UI, tray icon, config management, hotkey V/E toggle
- **DLL** (NextKeyTSF): Loaded per-process by Windows TSF, handles keystroke->Vietnamese transformation
- **Communication**: SharedState (memory-mapped file) for flags, config.toml for settings
- **Resilience**: DLL works independently if EXE crashes. Config snapshot at word boundary. Never block typing.

## Key Coding Rules

Follow `docs/CODING_RULES.md` in full. Critical rules:

1. **Namespace**: All code under `NextKey::`. No `using namespace` in headers.
2. **Naming**: PascalCase classes/methods, camelCase locals, trailing_ members, UPPER_SNAKE constants.
3. **Memory**: Smart pointers for ownership, CComPtr for COM, RAII for handles.
4. **Errors**: Never block user. Async notify + fallback to defaults. Log but don't crash.
5. **TSF**: All text changes via edit sessions. Clean up composition on deactivate.
6. **Modern C++**: `[[nodiscard]]`, `noexcept`, `constexpr` where appropriate. `std::atomic` for cross-thread flags.

## Common Pitfalls

- **Sciter script tags**: No `type="module"`. Use plain `<script src="..."></script>`.
- **Sciter CSS @import**: Unreliable. Use `<link>` tags instead.
- **Sciter init timing**: `load()` is async. Don't use `call_function()` for init — set DOM attributes directly.
- **Sciter events**: Use `handle_event()` not `on_event()` in `sciter::window` subclasses.
- **Sciter resource loading**: Always use `this://app/` URLs + `on_load_data()` override.
- **TIP vs HKL**: `GetKeyboardLayout()` returns same HKL for TIPs and plain keyboards. Don't use it to detect TIP state.
- **SharedState toggle**: Use `InterlockedXor` for atomic flag toggle. Both EXE and DLL can toggle.
- **DLL icon**: TSF LanguageBarButton owns the icon. No cross-process PostMessage needed.

## Sciter SDK Reference

SDK location: `/mnt/c/Users/Admin/Downloads/Compressed/sciter-js-sdk-6.0.3.5/sciter-js-sdk-6.0.3.5`
Always check SDK `samples/` and `docs/` when implementing Sciter features.

## Screenshots

When user says "see the picture", check `/mnt/c/Users/Admin/Pictures/claude/`
