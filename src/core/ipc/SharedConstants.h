// NexusKey - Shared Constants between EXE and TSF DLL
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#ifdef _WIN32

#include <Windows.h>

namespace NextKey {

// Cross-process constants shared between EXE and TSF DLL.
// Icon management moved to DLL's ITfLangBarItemButton — no cross-process messaging needed.

// Custom window messages for cross-process V/E mode sync (main ↔ settings subprocess)
constexpr UINT WM_NEXUSKEY_SET_MODE = WM_USER + 100;      // Settings → Main: set mode (wParam: 1=Vietnamese, 0=English)
constexpr UINT WM_NEXUSKEY_MODE_CHANGED = WM_USER + 101;  // Main → Settings: mode changed (wParam: 1=Vietnamese, 0=English)
constexpr UINT WM_NEXUSKEY_OPEN_EXCLUDED = WM_USER + 102;  // Deferred: open excluded apps dialog

}  // namespace NextKey

#endif  // _WIN32
