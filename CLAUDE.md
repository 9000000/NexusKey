# NexusKey

Vietnamese IME (Input Method Editor) for Windows. Hybrid TSF + Hook fallback, C++20, Sciter.JS UI.

> **⚡ Always read `PROJECT_MAP.md` first** before exploring the codebase. It contains directory tree, file descriptions, quick-reference lookup table, and data flow diagram — saving significant time and tokens.

## Build & Test

### Prerequisites
1. **Windows 10/11**
2. **Visual Studio 2022** (with "Desktop development with C++" and "ATL support")
3. **CMake 3.20+**
4. **toml++**: `toml.hpp` placed in `src/vendor/toml.hpp` (from marzer/tomlplusplus)

### Build Commands

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

### Verification & Installation

1. **Run NextKey Core**: `NextKeyApp.exe` must be running to initialize shared memory.
2. **Register TSF DLL** (Admin Command Prompt in build folder):
   ```cmd
   regsvr32 NextKeyTSF.dll
   ```
   *To uninstall:* `regsvr32 /u NextKeyTSF.dll`

## Project Structure, Targets & Architecture

→ See `PROJECT_MAP.md` for full directory tree, CMake targets, data flow diagram, and quick-reference lookup table.

## Key Coding Rules

Follow `docs/CODING_RULES/` (sharded) in full. Critical rules:

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
- **Win10 DWM blur artifact**: `AccentState::BlurBehind` on Win10 blurs the entire HWND including transparent `border-radius` pixels → visible halo at bottom edge. Fix: `SciterHelper::enableWindowBlur()` skips blur on Win10 (`!IsWindows11OrGreater()`). Do NOT remove this guard.
- **Win10 window border**: `DWMWCP_ROUND` and `DWMWA_BORDER_COLOR` are Win11-only — ignored on Win10. CSS `--shadow-card` includes a `0 0 0 1px` border ring to define window edges on Win10.
- **Sciter float kills ClearType**: `float: left/right` on transparent windows creates composite layers → ClearType disabled → text blurry. Use `flow: horizontal` (Sciter flex) wrapper instead.
- **Sciter light mode text faded**: Grayscale AA on translucent windows makes dark-on-light text look washed out. Fix: `--text-primary: #000000` + `Segoe UI Variable Text` font.
- **Sciter window resize**: `window-resizable="false"` alone not enough — must also block `HTLEFT..HTBOTTOMRIGHT` in `WM_NCHITTEST` (both SettingsDialog + SciterSubDialog).

## Skills (use proactively)

Use the appropriate skill BEFORE diving into code:

| Skill | When to use |
|-------|------------|
| `nexuskey-typing-bugs` | Vietnamese typing bugs: wrong tone, English mangled, dd/w modifier issues |
| `nexuskey-debug` | General bugs: TSF, SharedState, Sciter UI, hook engine |
| `nexuskey-review` | Code review before commit — naming, SharedState, modern C++ checklist |
| `nexuskey-build` | Build, test, pack resources |
| `nexuskey-security` | Security audit — update system, SharedState IPC, config, macro expansion |

**Rules:**
- Typing/tone/English detection bugs → use `nexuskey-typing-bugs` BEFORE reading code
- Code review / before commit → use `nexuskey-review`
- Security audit / touching UpdateChecker, UpdateInstaller, SharedState → use `nexuskey-security`
- Adding new toggle → see `docs/CODING_RULES/5-struct-versioning.md` (7-step checklist + step 7 Write() copy)
- Docs are sharded + distilled for token efficiency → see `docs/index.md` for full map

## Sciter SDK Reference

SDK location: `/mnt/c/Users/Admin/Downloads/Compressed/sciter-js-sdk-6.0.3.5/sciter-js-sdk-6.0.3.5`
Always check SDK `samples/` and `docs/` when implementing Sciter features.

Update Sciter SDK to latest: `bash tools/update_sciter.sh` (fetches from GitLab, copies includes + binaries to `extern/sciter/`)

## Screenshots

When user says "see the picture", check `/mnt/c/Users/Admin/Pictures/claude/`
