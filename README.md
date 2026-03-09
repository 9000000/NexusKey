# NexusKey

[![Build](https://github.com/phatmt/NexusKey/actions/workflows/build.yml/badge.svg)](https://github.com/phatmt/NexusKey/actions/workflows/build.yml)
[![License: GPL v3](https://img.shields.io/badge/License-GPLv3-blue.svg)](https://www.gnu.org/licenses/gpl-3.0)

**Modern Vietnamese Input Method for Windows**

NexusKey is a Vietnamese keyboard input application for Windows, inspired by [OpenKey](https://github.com/nickeldev/unikey-source) by Mai Vũ Tuyên. It features a completely rewritten engine, modern Sciter.JS-based UI, and deep integration with Windows via the Text Services Framework (TSF).

## Features

- **Input methods** — Telex, VNI, Simple Telex
- **Code tables** — Unicode, TCVN3, VNI Windows, Unicode Compound, Vietnamese Locale
- **Spell checker** — Vietnamese spelling validation with configurable strictness
- **Smart switch** — Auto-detect and switch between Vietnamese/English per application
- **Macros** — Custom text expansion (e.g., `addr` → full address)
- **Auto-capitalization** — Capitalize first letter after sentence-ending punctuation
- **Quick consonants** — Double-tap consonants for common Vietnamese pairs
- **Exclude apps** — Disable Vietnamese input in specific applications
- **TSF engine per app** — Use TSF input method instead of keyboard hook for selected apps
- **Per-app code table** — Different code tables for different applications
- **Auto-update** — Built-in update checker and installer
- **Tone placement** — Modern (new-style) and classic (old-style) options
- **Modern UI** — Sciter.JS-powered settings with dark/light theme support

## Screenshots

*Coming soon*

## Building

### Prerequisites

- **Windows 10/11**
- **Visual Studio 2022** (with "Desktop development with C++" workload and ATL support)
- **CMake 3.20+**

All other dependencies (Sciter SDK, Google Test, toml++) are vendored in `extern/`.

### Build

```powershell
cmake -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release --target NextKeyApp
```

### Run Tests

```powershell
cmake --build build --config Release --target NextKeyTests
ctest --test-dir build --build-config Release --output-on-failure
```

## Architecture

NexusKey is composed of three layers:

| Layer | Target | Description |
|-------|--------|-------------|
| **NextKeyEngine** | Static lib | Pure C++20 Vietnamese input engines (Telex, VNI), spell checker, code table converter. Zero platform dependencies. |
| **NextKeyCore** | Static lib | Platform layer — shared memory IPC, configuration management, smart switch. |
| **NextKeyApp** | Win32 EXE | GUI application with Sciter.JS UI, hook engine, tray icon, auto-update. |
| **NextKeyTSF** | DLL | Text Services Framework integration — registers as a Windows input method. |

## Credits

- Inspired by [OpenKey](https://github.com/nickeldev/unikey-source) by Mai Vũ Tuyên
- UI powered by [Sciter.JS](https://sciter.com/)
- Configuration parsing by [toml++](https://github.com/marzer/tomlplusplus)
- Testing with [Google Test](https://github.com/google/googletest)

### Top Testers 🏆
A special thanks to the following community members for their extensive testing and feedback:
- Zenfas
- huntersun
- haihv8x
- os-hoanghv

## License

This project is licensed under the [GNU General Public License v3.0](https://www.gnu.org/licenses/gpl-3.0.en.html) — see the [LICENSE](LICENSE) file for details.
