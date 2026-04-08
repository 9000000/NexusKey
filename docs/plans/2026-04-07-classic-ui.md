# NexusKey Classic UI Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a Win32 native "Classic" UI build target alongside the existing Sciter "Modern" UI, with Material Design 3 visual styling (Indigo #5C6BC0 seed).

**Architecture:** New CMake target `NextKeyLite` builds a separate EXE using the same `NextKeyCore`/`NextKeyEngine` libraries but with Win32 native dialogs instead of Sciter. A `SettingMetadata.h` mapping table eliminates duplicated if-else chains for settings binding. `ClassicTheme` class handles M3 colors, owner-draw controls, and dark/light mode switching.

**Tech Stack:** C++20, Win32 API (CreateWindowEx, WM_DRAWITEM, DwmSetWindowAttribute), GDI, CMake compile-time flag `NEXUSKEY_LITE_MODE`

---

## File Structure

### New files to create:

```
src/core/config/SettingMetadata.h       -- Mapping table: setting ID → config field offset + UI metadata
src/app/classic/                        -- New folder for Classic UI
src/app/classic/ClassicTheme.h          -- M3 color tokens, font set, brush cache
src/app/classic/ClassicTheme.cpp        -- Theme init, dark mode detect, WM_CTLCOLOR handlers, owner-draw
src/app/classic/ClassicSettingsDialog.h -- Win32 settings dialog (compact + advanced)
src/app/classic/ClassicSettingsDialog.cpp -- Dialog proc, control creation, settings binding
src/app/classic/ClassicAboutDialog.h    -- Simple about dialog
src/app/classic/ClassicAboutDialog.cpp  -- About dialog implementation
src/app/classic/resource.h              -- Control IDs (IDC_*, IDD_*)
src/app/classic/NexusKeyLite.rc         -- Dialog templates + manifest
src/app/main_lite.cpp                   -- WinMain entry for lite build
```

### Files to modify:

```
CMakeLists.txt                          -- Add NextKeyLite target + NEXUSKEY_LITE_MODE option
src/core/ipc/SharedState.h              -- No changes (EncodeFeatureFlags already exists)
src/core/config/TypingConfig.h          -- No changes (struct already defined)
```

---

## Task 1: CMake — Add NextKeyLite Target

**Files:**
- Modify: `CMakeLists.txt`
- Create: `src/app/main_lite.cpp` (minimal skeleton)
- Create: `src/app/classic/resource.h` (control IDs)
- Create: `src/app/classic/NexusKeyLite.rc` (manifest + version)

### Steps:

- [ ] **Step 1: Create resource.h with control IDs**

```cpp
// src/app/classic/resource.h
// NexusKey Classic UI — Control IDs
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

// Dialog IDs
#define IDD_SETTINGS_CLASSIC    1001

// ── Compact section: dropdowns ──
#define IDC_COMBO_METHOD        2001  // Kiểu gõ (Telex/VNI/SimpleTelex)
#define IDC_COMBO_ENCODING      2002  // Bảng mã (Unicode/TCVN3/VNI/...)
#define IDC_COMBO_SWITCHKEY     2003  // Phím chuyển

// ── Compact section: buttons ──
#define IDC_BTN_CLOSE           2050  // Đóng
#define IDC_BTN_EXIT            2051  // Kết thúc NexusKey
#define IDC_CHECK_EXPAND        2052  // Mở rộng (toggle compact↔advanced)

// ── Advanced: Tab control ──
#define IDC_TAB_ADVANCED        2100

// ── Tab 0: Cơ bản — Checkboxes (left column) ──
#define IDC_CHECK_SPELL         2201  // Kiểm tra chính tả
#define IDC_CHECK_MODERN_ORTHO  2202  // Kiểu dấu mới (oà/uý)
#define IDC_CHECK_AUTO_CAPS     2203  // Tự động viết hoa
#define IDC_CHECK_ALLOW_ZWJF    2204  // Cho phép z/w/j/f
#define IDC_CHECK_AUTO_RESTORE  2205  // Khôi phục phím sai
#define IDC_CHECK_SMART_SWITCH  2206  // Smart switch V/E
#define IDC_CHECK_EXCLUDE_APPS  2207  // Loại trừ ứng dụng
#define IDC_CHECK_ENGLISH_BYPASS 2208 // Cho phép gõ dấu tự do

// ── Tab 0: Cơ bản — Checkboxes (right column) ──
#define IDC_CHECK_BEEP          2211  // Âm thanh chuyển V/E
#define IDC_CHECK_MACRO         2212  // Gõ tắt (macro)
#define IDC_CHECK_MACRO_EN      2213  // Macro khi Tiếng Anh
#define IDC_CHECK_QUICK_TELEX   2214  // cc→ch, gg→gi
#define IDC_CHECK_QUICK_START   2215  // f→ph, j→gi
#define IDC_CHECK_QUICK_END     2216  // g→ng, h→nh
#define IDC_CHECK_TEMP_OFF_SPELL 2217 // Ctrl tắt spell tạm
#define IDC_CHECK_TEMP_OFF_ALT  2218  // Alt tắt TV tạm

// ── Tab 1: Phím tắt ──
#define IDC_CHECK_KEY_CTRL      2301  // Ctrl modifier
#define IDC_CHECK_KEY_SHIFT     2302  // Shift modifier
#define IDC_CHECK_KEY_ALT       2303  // Alt modifier
#define IDC_CHECK_KEY_WIN       2304  // Win modifier
#define IDC_EDIT_SWITCH_KEY     2305  // Key char input

// ── Tab 2: Hệ thống ──
#define IDC_CHECK_RUN_STARTUP   2401  // Khởi động cùng Windows
#define IDC_CHECK_RUN_ADMIN     2402  // Chạy với quyền Admin
#define IDC_CHECK_SHOW_STARTUP  2403  // Hiện dialog khi khởi động
#define IDC_CHECK_DESKTOP_SC    2404  // Shortcut desktop
#define IDC_CHECK_FLOATING_ICON 2405  // Floating icon
#define IDC_CHECK_AUTO_UPDATE   2406  // Tự động kiểm tra cập nhật
#define IDC_CHECK_ENGLISH_UI    2407  // Giao diện Tiếng Anh

// ── Footer buttons ──
#define IDC_BTN_DEFAULTS        2501  // Thiết lập mặc định
#define IDC_BTN_SAVE            2502  // Lưu thay đổi

// ── Static labels (for owner-draw sections) ──
#define IDC_STATIC_METHOD       2601
#define IDC_STATIC_ENCODING     2602
#define IDC_STATIC_SWITCHKEY    2603
```

- [ ] **Step 2: Create minimal .rc file with manifest**

```rc
// src/app/classic/NexusKeyLite.rc
#include "resource.h"
#include <windows.h>
#include <commctrl.h>

// Version info (matches main app)
VS_VERSION_INFO VERSIONINFO
 FILEVERSION 2,1,8,0
 PRODUCTVERSION 2,1,8,0
BEGIN
    BLOCK "StringFileInfo"
    BEGIN
        BLOCK "040904B0"
        BEGIN
            VALUE "FileDescription", "NexusKey Classic - Vietnamese IME"
            VALUE "ProductName", "NexusKey Classic"
            VALUE "FileVersion", "2.1.8"
            VALUE "ProductVersion", "2.1.8"
            VALUE "LegalCopyright", "Copyright (c) 2026 PhatMT"
        END
    END
END

// App icon (reuse from main app)
IDI_APPLICATION ICON "../../app/resources/nexuskey.ico"

// Visual styles manifest (Common Controls v6)
CREATEPROCESS_MANIFEST_RESOURCE_ID RT_MANIFEST "NexusKeyLite.exe.manifest"
```

Create manifest file:
```xml
<!-- src/app/classic/NexusKeyLite.exe.manifest -->
<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<assembly xmlns="urn:schemas-microsoft-com:asm.v1" manifestVersion="1.0">
  <assemblyIdentity type="win32" name="NexusKey.Classic" version="2.1.8.0" processorArchitecture="*"/>
  <dependency>
    <dependentAssembly>
      <assemblyIdentity type="win32" name="Microsoft.Windows.Common-Controls"
        version="6.0.0.0" processorArchitecture="*" publicKeyToken="6595b64144ccf1df" language="*"/>
    </dependentAssembly>
  </dependency>
  <application xmlns="urn:schemas-microsoft-com:asm.v3">
    <windowsSettings>
      <dpiAware xmlns="http://schemas.microsoft.com/SMI/2005/WindowsSettings">true/pm</dpiAware>
      <dpiAwareness xmlns="http://schemas.microsoft.com/SMI/2016/WindowsSettings">PerMonitorV2</dpiAwareness>
    </windowsSettings>
  </application>
</assembly>
```

- [ ] **Step 3: Create main_lite.cpp skeleton**

```cpp
// src/app/main_lite.cpp
// NexusKey Classic — Lite build entry point
// SPDX-License-Identifier: GPL-3.0-only

#include "core/Version.h"
#include "core/config/ConfigManager.h"
#include "core/ipc/SharedState.h"
#include "core/ipc/SharedStateManager.h"
#include "core/ipc/SecurityHelpers.h"
#include "core/config/ConfigEvent.h"
#include "core/Strings.h"
#include "core/Debug.h"

#include "system/HookEngine.h"
#include "system/QuickConvert.h"
#include "system/TrayIcon.h"
#include "system/TsfRegistration.h"
#include "system/StartupHelper.h"
#include "system/UpdateChecker.h"

#include <Windows.h>
#include <commctrl.h>
#include <ole2.h>
#include <memory>
#include <atomic>

#pragma comment(lib, "comctl32.lib")

using namespace NextKey;

static std::atomic<bool> g_running{true};
static TrayIcon g_trayIcon;
static HookEngine g_hookEngine;
static SharedStateManager g_sharedState;
static std::unique_ptr<QuickConvert> g_quickConvert;

// TODO: Task 5 will add ClassicSettingsDialog integration here

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, LPWSTR, int) {
    // Prevent multiple instances
    HANDLE hMutex = CreateMutexW(nullptr, TRUE, L"NexusKeyLiteMutex");
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        CloseHandle(hMutex);
        return 0;
    }

    OleInitialize(nullptr);
    INITCOMMONCONTROLSEX icc = { sizeof(icc), ICC_TAB_CLASSES | ICC_STANDARD_CLASSES };
    InitCommonControlsEx(&icc);

    // Placeholder: message loop
    MSG msg;
    while (GetMessage(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    OleUninitialize();
    CloseHandle(hMutex);
    return 0;
}
```

- [ ] **Step 4: Add NextKeyLite target to CMakeLists.txt**

Add after the `NextKeyApp` target (around line 177):

```cmake
# ============================================================================
# Classic UI (Lite build) — Win32 native, no Sciter dependency
# Build with: cmake -B build -DNEXUSKEY_LITE_MODE=ON
# ============================================================================
option(NEXUSKEY_LITE_MODE "Build Classic (Win32 native) UI instead of Sciter" OFF)

if(NEXUSKEY_LITE_MODE AND WIN32)
    enable_language(RC)

    add_executable(NextKeyLite WIN32
        src/app/main_lite.cpp
        src/app/classic/resource.h
        src/app/classic/NexusKeyLite.rc
        # system/ (shared with main app)
        src/app/system/TrayIcon.h
        src/app/system/TrayIcon.cpp
        src/app/system/FloatingIcon.h
        src/app/system/FloatingIcon.cpp
        src/app/system/HookEngine.h
        src/app/system/HookEngine.cpp
        src/app/system/HotkeyManager.h
        src/app/system/HotkeyManager.cpp
        src/app/system/QuickConvert.h
        src/app/system/QuickConvert.cpp
        src/app/system/ToastPopup.h
        src/app/system/ToastPopup.cpp
        src/app/system/UpdateChecker.h
        src/app/system/UpdateChecker.cpp
        src/app/system/UpdateInstaller.h
        src/app/system/UpdateInstaller.cpp
        src/app/system/UpdateSecurity.h
        src/app/system/UpdateSecurity.cpp
        src/app/system/StartupHelper.h
        src/app/system/SubprocessHelper.h
        src/app/system/TsfRegistration.h
        src/app/system/TsfRegistration.cpp
        # classic/ (new)
        src/app/classic/ClassicTheme.h
        src/app/classic/ClassicTheme.cpp
        src/app/classic/ClassicSettingsDialog.h
        src/app/classic/ClassicSettingsDialog.cpp
        # version
        src/core/Version.h
    )
    target_include_directories(NextKeyLite PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src/app
        ${CMAKE_CURRENT_SOURCE_DIR}/src/app/classic
        ${CMAKE_CURRENT_SOURCE_DIR}/src/app/system
    )
    target_compile_definitions(NextKeyLite PRIVATE
        NEXUSKEY_HOOK_ENGINE
        NEXUSKEY_LITE_MODE
        $<$<CONFIG:Debug>:NEXTKEY_DEBUG>
    )
    target_link_libraries(NextKeyLite PRIVATE
        NextKeyCore
        advapi32 user32 ole32 shell32 dwmapi uuid urlmon version comctl32 winmm bcrypt gdiplus
    )
    set_target_properties(NextKeyLite PROPERTIES OUTPUT_NAME "NexusKeyClassic")
endif()
```

- [ ] **Step 5: Create stub files so CMake configures**

Create empty stubs for files referenced in CMake:

`src/app/classic/ClassicTheme.h`:
```cpp
// NexusKey Classic — Theme (stub)
#pragma once
namespace NextKey::Classic {}
```

`src/app/classic/ClassicTheme.cpp`:
```cpp
// NexusKey Classic — Theme (stub)
#include "ClassicTheme.h"
```

`src/app/classic/ClassicSettingsDialog.h`:
```cpp
// NexusKey Classic — Settings Dialog (stub)
#pragma once
namespace NextKey::Classic {}
```

`src/app/classic/ClassicSettingsDialog.cpp`:
```cpp
// NexusKey Classic — Settings Dialog (stub)
#include "ClassicSettingsDialog.h"
```

- [ ] **Step 6: Verify CMake configures on Linux (test build sanity)**

Run:
```bash
cd /home/phatmt/code/NexusKey
cmake -B build-lite-check -G "Unix Makefiles" -DCMAKE_BUILD_TYPE=Debug 2>&1 | tail -5
```

Expected: CMake configures without errors (NextKeyLite target only builds on WIN32, so Linux skips it — but verifies the CMakeLists.txt syntax is valid).

- [ ] **Step 7: Commit**

```bash
git add src/app/classic/ src/app/main_lite.cpp CMakeLists.txt
git commit -m "feat(classic): add NextKeyLite CMake target with Win32 native UI skeleton"
```

---

## Task 2: SettingMetadata.h — Shared Mapping Table

**Files:**
- Create: `src/core/config/SettingMetadata.h`

This replaces the 40+ if-else chain in `SettingsDialog.cpp:566` with a data-driven lookup. Both Sciter and Classic UIs can consume it.

- [ ] **Step 1: Create SettingMetadata.h**

```cpp
// src/core/config/SettingMetadata.h
// NexusKey — Setting metadata for data-driven UI binding
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <cstddef>
#include <cstdint>

namespace NextKey {

/// Type of UI control for a setting
enum class SettingType : uint8_t {
    Toggle,     // Boolean checkbox/toggle
    Dropdown,   // Enum dropdown (int value)
    Action      // Button (no persistent value)
};

/// Which config object owns this setting
enum class SettingOwner : uint8_t {
    Typing,     // TypingConfig — syncs to SharedState + TOML
    Hotkey,     // HotkeyConfig — syncs to SharedState + TOML
    System,     // SystemConfig — TOML only (no SharedState)
    UI          // UIConfig — TOML only (no SharedState)
};

/// Metadata for a single setting (constexpr, zero-allocation)
struct SettingMeta {
    const wchar_t* id;         // Sciter DOM id / logical name (e.g. L"spell-check")
    SettingType    type;        // Toggle, Dropdown, Action
    SettingOwner   owner;       // Which config struct owns this field
    ptrdiff_t      offset;      // offsetof(OwnerStruct, field) — for Toggle type
    const wchar_t* label;       // Vietnamese display label
    const wchar_t* labelEn;    // English display label (for i18n)
    uint16_t       win32Id;     // IDC_ control ID for Classic UI (0 = no control)
    uint8_t        tab;         // Tab index in advanced mode (0=Cơ bản, 1=Phím tắt, 2=Hệ thống)
    uint8_t        column;      // Column in 2-col layout (0=left, 1=right)
};

// Forward-declare config structs for offsetof
struct TypingConfig;
struct HotkeyConfig;
struct SystemConfig;

} // namespace NextKey

// Include actual structs for offsetof to work
#include "core/config/TypingConfig.h"
#include "core/SystemConfig.h"
#include "core/UIConfig.h"
#include "classic/resource.h"   // IDC_ constants (only when NEXUSKEY_LITE_MODE)

namespace NextKey {

// ──────────────────────────────────────────────────────────
// Master settings table — single source of truth
// Add new settings here; both UIs pick them up automatically.
// ──────────────────────────────────────────────────────────

// Helper macro to reduce verbosity
#define TYPING_TOGGLE(id, field, label, labelEn, idc, tab, col) \
    { L##id, SettingType::Toggle, SettingOwner::Typing, \
      offsetof(TypingConfig, field), L##label, L##labelEn, idc, tab, col }

#define HOTKEY_TOGGLE(id, field, label, labelEn, idc, tab, col) \
    { L##id, SettingType::Toggle, SettingOwner::Hotkey, \
      offsetof(HotkeyConfig, field), L##label, L##labelEn, idc, tab, col }

#define SYSTEM_TOGGLE(id, field, label, labelEn, idc, tab, col) \
    { L##id, SettingType::Toggle, SettingOwner::System, \
      offsetof(SystemConfig, field), L##label, L##labelEn, idc, tab, col }

#ifdef NEXUSKEY_LITE_MODE
// Classic UI uses IDC_ constants from resource.h
#else
// Sciter UI: set IDC to 0 (unused)
#undef IDC_CHECK_SPELL
#define IDC_CHECK_SPELL 0
// ... (Sciter build doesn't need IDC values, just the id strings)
#endif

inline constexpr SettingMeta kSettingsTable[] = {
    // ── Tab 0: Cơ bản — Left column ──
    TYPING_TOGGLE("spell-check",     spellCheckEnabled,  "Kiểm tra chính tả",       "Spell check",           IDC_CHECK_SPELL,         0, 0),
    TYPING_TOGGLE("modern-ortho",    modernOrtho,        "Kiểu dấu mới (oà/uý)",   "Modern ortho (oà/uý)",  IDC_CHECK_MODERN_ORTHO,  0, 0),
    TYPING_TOGGLE("auto-caps",       autoCaps,           "Tự động viết hoa",        "Auto capitalize",       IDC_CHECK_AUTO_CAPS,     0, 0),
    TYPING_TOGGLE("allow-zwjf",      allowZwjf,          "Cho phép z/w/j/f",        "Allow z/w/j/f keys",    IDC_CHECK_ALLOW_ZWJF,    0, 0),
    TYPING_TOGGLE("restore-key",     autoRestoreEnabled, "Khôi phục phím sai",      "Auto restore keys",     IDC_CHECK_AUTO_RESTORE,  0, 0),
    TYPING_TOGGLE("smart-switch",    smartSwitch,        "Chuyển V/E thông minh",   "Smart V/E switch",      IDC_CHECK_SMART_SWITCH,  0, 0),
    TYPING_TOGGLE("exclude-apps",    excludeApps,        "Loại trừ ứng dụng",       "Exclude apps",          IDC_CHECK_EXCLUDE_APPS,  0, 0),
    TYPING_TOGGLE("allow-english-bypass", allowEnglishBypass, "Gõ dấu tự do",       "Free tone marking",     IDC_CHECK_ENGLISH_BYPASS,0, 0),

    // ── Tab 0: Cơ bản — Right column ──
    TYPING_TOGGLE("beep-sound",      beepOnSwitch,       "Âm thanh chuyển V/E",     "Beep on switch",        IDC_CHECK_BEEP,          0, 1),
    TYPING_TOGGLE("use-macro",       macroEnabled,       "Gõ tắt (macro)",          "Macros",                IDC_CHECK_MACRO,         0, 1),
    TYPING_TOGGLE("macro-english",   macroInEnglish,     "Macro khi Tiếng Anh",     "Macros in English",     IDC_CHECK_MACRO_EN,      0, 1),
    TYPING_TOGGLE("quick-telex",     quickConsonant,     "cc→ch, gg→gi, nn→ng",    "Quick consonant",       IDC_CHECK_QUICK_TELEX,   0, 1),
    TYPING_TOGGLE("quick-start",     quickStartConsonant,"f→ph, j→gi, w→qu",       "Quick start consonant", IDC_CHECK_QUICK_START,   0, 1),
    TYPING_TOGGLE("quick-end",       quickEndConsonant,  "g→ng, h→nh, k→ch",       "Quick end consonant",   IDC_CHECK_QUICK_END,     0, 1),
    TYPING_TOGGLE("temp-off-spell",  tempOffSpellByCtrl, "Ctrl tắt spell tạm",     "Ctrl disables spell",   IDC_CHECK_TEMP_OFF_SPELL,0, 1),
    TYPING_TOGGLE("temp-off-openkey",tempOffByAlt,       "Alt tắt TV tạm",         "Alt disables Vn",       IDC_CHECK_TEMP_OFF_ALT,  0, 1),

    // ── Tab 1: Phím tắt ──
    HOTKEY_TOGGLE("key-ctrl",  ctrl,  "Ctrl",  "Ctrl",  IDC_CHECK_KEY_CTRL,  1, 0),
    HOTKEY_TOGGLE("key-shift", shift, "Shift", "Shift", IDC_CHECK_KEY_SHIFT, 1, 0),
    HOTKEY_TOGGLE("key-alt",   alt,   "Alt",   "Alt",   IDC_CHECK_KEY_ALT,   1, 0),
    HOTKEY_TOGGLE("key-win",   win,   "Win",   "Win",   IDC_CHECK_KEY_WIN,   1, 0),

    // ── Tab 2: Hệ thống ──
    SYSTEM_TOGGLE("run-startup",     runAtStartup,     "Khởi động cùng Windows",   "Run at startup",      IDC_CHECK_RUN_STARTUP,   2, 0),
    SYSTEM_TOGGLE("run-admin",       runAsAdmin,        "Chạy quyền Admin",         "Run as admin",        IDC_CHECK_RUN_ADMIN,     2, 0),
    SYSTEM_TOGGLE("show-on-startup", showOnStartup,     "Hiện khi khởi động",       "Show on startup",     IDC_CHECK_SHOW_STARTUP,  2, 0),
    SYSTEM_TOGGLE("desktop-shortcut",desktopShortcut,   "Shortcut desktop",          "Desktop shortcut",    IDC_CHECK_DESKTOP_SC,    2, 0),
    SYSTEM_TOGGLE("floating-icon",   showFloatingIcon,  "Biểu tượng nổi",           "Floating icon",       IDC_CHECK_FLOATING_ICON, 2, 1),
    SYSTEM_TOGGLE("check-update",    autoCheckUpdate,   "Tự động cập nhật",          "Auto update check",   IDC_CHECK_AUTO_UPDATE,   2, 1),
};

#undef TYPING_TOGGLE
#undef HOTKEY_TOGGLE
#undef SYSTEM_TOGGLE

/// Number of entries in the settings table
inline constexpr size_t kSettingsCount = sizeof(kSettingsTable) / sizeof(kSettingsTable[0]);

/// Find a setting by its string ID. Returns nullptr if not found.
[[nodiscard]] inline const SettingMeta* FindSetting(const wchar_t* id) noexcept {
    for (size_t i = 0; i < kSettingsCount; ++i) {
        if (wcscmp(kSettingsTable[i].id, id) == 0)
            return &kSettingsTable[i];
    }
    return nullptr;
}

/// Find a setting by its Win32 control ID. Returns nullptr if not found.
[[nodiscard]] inline const SettingMeta* FindSettingByControlId(uint16_t controlId) noexcept {
    for (size_t i = 0; i < kSettingsCount; ++i) {
        if (kSettingsTable[i].win32Id == controlId)
            return &kSettingsTable[i];
    }
    return nullptr;
}

} // namespace NextKey
```

- [ ] **Step 2: Verify it compiles (Linux test build)**

Run:
```bash
cd /home/phatmt/code/NexusKey
cmake -B build-linux -G "Unix Makefiles" -DCMAKE_BUILD_TYPE=Debug 2>&1 | tail -3
cmake --build build-linux --target NextKeyTests 2>&1 | tail -5
```

Expected: Builds and tests still pass (SettingMetadata.h is header-only, included by nothing yet).

- [ ] **Step 3: Commit**

```bash
git add src/core/config/SettingMetadata.h
git commit -m "feat(config): add SettingMetadata.h — shared mapping table for dual-UI settings binding"
```

---

## Task 3: ClassicTheme — M3 Color System + Fonts

**Files:**
- Modify: `src/app/classic/ClassicTheme.h` (replace stub)
- Modify: `src/app/classic/ClassicTheme.cpp` (replace stub)

- [ ] **Step 1: Implement ClassicTheme.h**

```cpp
// src/app/classic/ClassicTheme.h
// NexusKey Classic — Material Design 3 Theme Engine
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <Windows.h>
#include <dwmapi.h>
#include <cstdint>

namespace NextKey::Classic {

/// M3 color tokens (light and dark schemes)
struct ThemeColors {
    COLORREF background;        // Window background
    COLORREF surface;           // Card/panel surface
    COLORREF surfaceVariant;    // Input background, list bg
    COLORREF surfaceTonal1;     // Titlebar, elevated surfaces
    COLORREF primary;           // Accent: active state, buttons
    COLORREF onPrimary;         // Text on primary fill
    COLORREF primaryContainer;  // Chip bg, selected state
    COLORREF onSurface;         // Primary text
    COLORREF onSurfaceVariant;  // Secondary text, icons
    COLORREF outline;           // Input border, control border
    COLORREF outlineVariant;    // Subtle divider
    COLORREF error;             // Error state
    COLORREF onError;           // Text on error
};

/// Font handles (created once per DPI change)
struct FontSet {
    HFONT header;     // Segoe UI Variable Display, 11pt, SemiBold
    HFONT body;       // Segoe UI Variable Text, 9pt, Regular
    HFONT caption;    // Segoe UI Variable Text, 8pt, Regular
    HFONT label;      // Segoe UI Variable Text, 9pt, Medium (buttons)
};

/// Material Design 3 theme manager for Win32
class ClassicTheme {
public:
    ClassicTheme() = default;
    ~ClassicTheme();

    // Non-copyable
    ClassicTheme(const ClassicTheme&) = delete;
    ClassicTheme& operator=(const ClassicTheme&) = delete;

    /// Initialize theme (detect dark mode, create fonts/brushes)
    void Init(HWND hwnd);

    /// Clean up GDI resources
    void Destroy();

    /// Handle WM_SETTINGCHANGE to detect theme switch
    /// Returns true if theme actually changed (caller should InvalidateRect)
    bool OnSettingChange(LPARAM lParam);

    // ── WM_CTLCOLOR handlers (return HBRUSH) ──
    HBRUSH OnCtlColorDlg(HDC hdc);
    HBRUSH OnCtlColorStatic(HDC hdc, HWND hCtrl);
    HBRUSH OnCtlColorBtn(HDC hdc, HWND hCtrl);
    HBRUSH OnCtlColorEdit(HDC hdc, HWND hCtrl);
    HBRUSH OnCtlColorListBox(HDC hdc, HWND hCtrl);

    /// Owner-draw button (WM_DRAWITEM for BS_OWNERDRAW buttons)
    void DrawButton(DRAWITEMSTRUCT* dis, bool isPrimary);

    /// Owner-draw checkbox (WM_DRAWITEM for BS_OWNERDRAW checkboxes)
    void DrawCheckbox(DRAWITEMSTRUCT* dis);

    /// Draw tab control item (TCS_OWNERDRAWFIXED)
    void DrawTabItem(DRAWITEMSTRUCT* dis);

    /// Draw 1px horizontal divider
    void DrawDivider(HDC hdc, int x, int y, int width);

    /// Apply dark mode to window caption (DWM)
    void ApplyWindowAttributes(HWND hwnd);

    // ── State layer helpers ──
    static COLORREF BlendColors(COLORREF base, COLORREF overlay, BYTE alpha);

    // ── Accessors ──
    [[nodiscard]] bool IsDark() const noexcept { return isDark_; }
    [[nodiscard]] const ThemeColors& Colors() const noexcept { return colors_; }
    [[nodiscard]] const FontSet& Fonts() const noexcept { return fonts_; }
    [[nodiscard]] HBRUSH BrushBackground() const noexcept { return brBackground_; }
    [[nodiscard]] HBRUSH BrushSurface() const noexcept { return brSurface_; }

private:
    void DetectDarkMode();
    void CreateBrushes();
    void DestroyBrushes();
    void CreateFonts(UINT dpi);
    void DestroyFonts();

    bool isDark_ = false;
    ThemeColors colors_{};
    FontSet fonts_{};

    // Cached GDI brushes
    HBRUSH brBackground_ = nullptr;
    HBRUSH brSurface_ = nullptr;
    HBRUSH brSurfaceVariant_ = nullptr;
    HBRUSH brPrimary_ = nullptr;
    HBRUSH brOutlineVariant_ = nullptr;

    HWND hwnd_ = nullptr;
};

} // namespace NextKey::Classic
```

- [ ] **Step 2: Implement ClassicTheme.cpp**

```cpp
// src/app/classic/ClassicTheme.cpp
// NexusKey Classic — Material Design 3 Theme Engine
// SPDX-License-Identifier: GPL-3.0-only

#include "ClassicTheme.h"
#include <VersionHelpers.h>

#pragma comment(lib, "dwmapi.lib")

namespace NextKey::Classic {

// ── M3 Color Palettes (Indigo #5C6BC0 seed) ──

static constexpr ThemeColors kLightColors = {
    .background       = RGB(255, 251, 254),  // #FFFBFE
    .surface          = RGB(255, 251, 254),
    .surfaceVariant   = RGB(228, 225, 236),  // #E4E1EC
    .surfaceTonal1    = RGB(242, 239, 254),  // #F2EFFE
    .primary          = RGB( 92, 107, 192),  // #5C6BC0
    .onPrimary        = RGB(255, 255, 255),
    .primaryContainer = RGB(225, 226, 255),  // #E1E2FF
    .onSurface        = RGB( 27,  27,  31),  // #1B1B1F
    .onSurfaceVariant = RGB( 70,  70,  79),  // #46464F
    .outline          = RGB(119, 118, 128),  // #777680
    .outlineVariant   = RGB(200, 198, 207),  // #C8C6CF
    .error            = RGB(186,  26,  26),  // #BA1A1A
    .onError          = RGB(255, 255, 255),
};

static constexpr ThemeColors kDarkColors = {
    .background       = RGB( 27,  27,  31),  // #1B1B1F
    .surface          = RGB( 27,  27,  31),
    .surfaceVariant   = RGB( 70,  70,  79),  // #46464F
    .surfaceTonal1    = RGB( 33,  33,  47),  // #21212F
    .primary          = RGB(190, 194, 255),  // #BEC2FF
    .onPrimary        = RGB( 38,  42, 112),  // #262A70
    .primaryContainer = RGB( 62,  67, 147),  // #3E4393
    .onSurface        = RGB(229, 225, 230),  // #E5E1E6
    .onSurfaceVariant = RGB(200, 198, 207),  // #C8C6CF
    .outline          = RGB(145, 143, 154),  // #918F9A
    .outlineVariant   = RGB( 70,  70,  79),  // #46464F
    .error            = RGB(255, 180, 171),  // #FFB4AB
    .onError          = RGB(105,   0,   5),  // #690005
};

// ── Lifecycle ──

ClassicTheme::~ClassicTheme() {
    Destroy();
}

void ClassicTheme::Init(HWND hwnd) {
    hwnd_ = hwnd;
    DetectDarkMode();
    colors_ = isDark_ ? kDarkColors : kLightColors;

    UINT dpi = 96;
    // GetDpiForWindow requires Win10 1607+
    auto pfn = (UINT(WINAPI*)(HWND))GetProcAddress(GetModuleHandleW(L"user32.dll"), "GetDpiForWindow");
    if (pfn) dpi = pfn(hwnd);

    CreateFonts(dpi);
    CreateBrushes();
    ApplyWindowAttributes(hwnd);
}

void ClassicTheme::Destroy() {
    DestroyBrushes();
    DestroyFonts();
}

void ClassicTheme::DetectDarkMode() {
    DWORD value = 1;  // default light
    DWORD size = sizeof(value);
    RegGetValueW(HKEY_CURRENT_USER,
        L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
        L"AppsUseLightTheme", RRF_RT_REG_DWORD, nullptr, &value, &size);
    isDark_ = (value == 0);
}

bool ClassicTheme::OnSettingChange(LPARAM lParam) {
    if (lParam && wcscmp(reinterpret_cast<LPCWSTR>(lParam), L"ImmersiveColorSet") == 0) {
        bool wasDark = isDark_;
        DetectDarkMode();
        if (wasDark != isDark_) {
            colors_ = isDark_ ? kDarkColors : kLightColors;
            DestroyBrushes();
            CreateBrushes();
            ApplyWindowAttributes(hwnd_);
            return true;
        }
    }
    return false;
}

void ClassicTheme::ApplyWindowAttributes(HWND hwnd) {
    // Dark mode caption bar (Win10 build 18985+)
    BOOL darkBool = isDark_ ? TRUE : FALSE;
    DwmSetWindowAttribute(hwnd, 20 /*DWMWA_USE_IMMERSIVE_DARK_MODE*/, &darkBool, sizeof(darkBool));

    // Win11: rounded corners + caption color
    if (IsWindows11OrGreater()) {
        auto corner = 2; // DWMWCP_ROUND
        DwmSetWindowAttribute(hwnd, 33 /*DWMWA_WINDOW_CORNER_PREFERENCE*/, &corner, sizeof(corner));

        COLORREF captionColor = isDark_ ? RGB(33, 33, 47) : RGB(242, 239, 254);
        DwmSetWindowAttribute(hwnd, 35 /*DWMWA_CAPTION_COLOR*/, &captionColor, sizeof(captionColor));
    }
}

// ── Brushes ──

void ClassicTheme::CreateBrushes() {
    brBackground_     = CreateSolidBrush(colors_.background);
    brSurface_        = CreateSolidBrush(colors_.surface);
    brSurfaceVariant_ = CreateSolidBrush(colors_.surfaceVariant);
    brPrimary_        = CreateSolidBrush(colors_.primary);
    brOutlineVariant_ = CreateSolidBrush(colors_.outlineVariant);
}

void ClassicTheme::DestroyBrushes() {
    auto del = [](HBRUSH& br) { if (br) { DeleteObject(br); br = nullptr; } };
    del(brBackground_); del(brSurface_); del(brSurfaceVariant_);
    del(brPrimary_); del(brOutlineVariant_);
}

// ── Fonts ──

void ClassicTheme::CreateFonts(UINT dpi) {
    auto make = [&](int pt, int weight, const wchar_t* face) -> HFONT {
        int height = -MulDiv(pt, static_cast<int>(dpi), 72);
        return CreateFontW(height, 0, 0, 0, weight, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, face);
    };
    fonts_.header  = make(11, FW_SEMIBOLD, L"Segoe UI Variable Display");
    fonts_.body    = make(9,  FW_NORMAL,   L"Segoe UI Variable Text");
    fonts_.caption = make(8,  FW_NORMAL,   L"Segoe UI Variable Text");
    fonts_.label   = make(9,  FW_MEDIUM,   L"Segoe UI Variable Text");

    // Fallback: if Segoe UI Variable not available (Win10 pre-21H2)
    if (!fonts_.body) {
        fonts_.header  = make(11, FW_SEMIBOLD, L"Segoe UI");
        fonts_.body    = make(9,  FW_NORMAL,   L"Segoe UI");
        fonts_.caption = make(8,  FW_NORMAL,   L"Segoe UI");
        fonts_.label   = make(9,  FW_MEDIUM,   L"Segoe UI");
    }
}

void ClassicTheme::DestroyFonts() {
    auto del = [](HFONT& f) { if (f) { DeleteObject(f); f = nullptr; } };
    del(fonts_.header); del(fonts_.body); del(fonts_.caption); del(fonts_.label);
}

// ── WM_CTLCOLOR Handlers ──

HBRUSH ClassicTheme::OnCtlColorDlg(HDC) {
    return brBackground_;
}

HBRUSH ClassicTheme::OnCtlColorStatic(HDC hdc, HWND) {
    SetTextColor(hdc, colors_.onSurface);
    SetBkMode(hdc, TRANSPARENT);
    SelectObject(hdc, fonts_.body);
    return brBackground_;
}

HBRUSH ClassicTheme::OnCtlColorBtn(HDC hdc, HWND) {
    SetTextColor(hdc, colors_.onSurface);
    SetBkMode(hdc, TRANSPARENT);
    return brBackground_;
}

HBRUSH ClassicTheme::OnCtlColorEdit(HDC hdc, HWND) {
    SetTextColor(hdc, colors_.onSurface);
    SetBkColor(hdc, colors_.surfaceVariant);
    return brSurfaceVariant_;
}

HBRUSH ClassicTheme::OnCtlColorListBox(HDC hdc, HWND) {
    SetTextColor(hdc, colors_.onSurface);
    SetBkColor(hdc, colors_.surface);
    return brSurface_;
}

// ── Owner-Draw: Button ──

void ClassicTheme::DrawButton(DRAWITEMSTRUCT* dis, bool isPrimary) {
    HDC hdc = dis->hDC;
    RECT rc = dis->rcItem;
    bool pressed = (dis->itemState & ODS_SELECTED) != 0;
    bool disabled = (dis->itemState & ODS_DISABLED) != 0;
    bool focused = (dis->itemState & ODS_FOCUS) != 0;

    // Background
    COLORREF bg;
    if (isPrimary) {
        bg = disabled ? BlendColors(colors_.onSurface, colors_.background, 31)
                      : pressed ? BlendColors(colors_.primary, colors_.onPrimary, 31)
                                : colors_.primary;
    } else {
        bg = pressed ? BlendColors(colors_.surface, colors_.onSurface, 31)
                     : colors_.surface;
    }

    HBRUSH br = CreateSolidBrush(bg);
    FillRect(hdc, &rc, br);
    DeleteObject(br);

    // Border (outlined button only)
    if (!isPrimary) {
        HPEN pen = CreatePen(PS_SOLID, 1, focused ? colors_.primary : colors_.outline);
        HPEN old = static_cast<HPEN>(SelectObject(hdc, pen));
        HBRUSH null = static_cast<HBRUSH>(GetStockObject(NULL_BRUSH));
        HBRUSH oldBr = static_cast<HBRUSH>(SelectObject(hdc, null));
        Rectangle(hdc, rc.left, rc.top, rc.right, rc.bottom);
        SelectObject(hdc, old);
        SelectObject(hdc, oldBr);
        DeleteObject(pen);
    }

    // Text
    wchar_t text[128]{};
    GetWindowTextW(dis->hwndItem, text, 128);
    SetTextColor(hdc, disabled ? BlendColors(colors_.onSurface, colors_.background, 97)
                               : isPrimary ? colors_.onPrimary : colors_.primary);
    SetBkMode(hdc, TRANSPARENT);
    SelectObject(hdc, fonts_.label);
    DrawTextW(hdc, text, -1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
}

// ── Owner-Draw: Checkbox ──

void ClassicTheme::DrawCheckbox(DRAWITEMSTRUCT* dis) {
    HDC hdc = dis->hDC;
    RECT rc = dis->rcItem;
    bool checked = (dis->itemState & ODS_CHECKED) != 0;
    // For owner-draw checkboxes, state is managed manually via GetWindowLongPtr
    // Check GWLP_USERDATA for checked state
    checked = (GetWindowLongPtrW(dis->hwndItem, GWLP_USERDATA) != 0);

    // Fill background
    FillRect(hdc, &rc, brBackground_);

    // Checkbox box: 16x16, vertically centered
    constexpr int boxSize = 16;
    int y = rc.top + (rc.bottom - rc.top - boxSize) / 2;
    RECT box = { rc.left, y, rc.left + boxSize, y + boxSize };

    // Box fill
    COLORREF fillColor = checked ? colors_.primary : colors_.surface;
    HBRUSH fillBr = CreateSolidBrush(fillColor);
    FillRect(hdc, &box, fillBr);
    DeleteObject(fillBr);

    // Box border
    COLORREF borderColor = checked ? colors_.primary : colors_.outline;
    HPEN pen = CreatePen(PS_SOLID, 1, borderColor);
    HPEN oldPen = static_cast<HPEN>(SelectObject(hdc, pen));
    HBRUSH nullBr = static_cast<HBRUSH>(SelectObject(hdc, GetStockObject(NULL_BRUSH)));
    Rectangle(hdc, box.left, box.top, box.right, box.bottom);
    SelectObject(hdc, oldPen);
    SelectObject(hdc, nullBr);
    DeleteObject(pen);

    // Checkmark (when checked)
    if (checked) {
        HPEN ckPen = CreatePen(PS_SOLID, 2, colors_.onPrimary);
        HPEN oldCk = static_cast<HPEN>(SelectObject(hdc, ckPen));
        POINT pts[] = {
            { box.left + 3, box.top + 8 },
            { box.left + 6, box.top + 11 },
            { box.left + 12, box.top + 4 },
        };
        Polyline(hdc, pts, 3);
        SelectObject(hdc, oldCk);
        DeleteObject(ckPen);
    }

    // Label text (to the right of checkbox)
    RECT textRc = { rc.left + boxSize + 8, rc.top, rc.right, rc.bottom };
    wchar_t label[256]{};
    GetWindowTextW(dis->hwndItem, label, 256);
    SetTextColor(hdc, colors_.onSurface);
    SetBkMode(hdc, TRANSPARENT);
    SelectObject(hdc, fonts_.body);
    DrawTextW(hdc, label, -1, &textRc, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
}

// ── Owner-Draw: Tab Item ──

void ClassicTheme::DrawTabItem(DRAWITEMSTRUCT* dis) {
    HDC hdc = dis->hDC;
    RECT rc = dis->rcItem;
    bool selected = (dis->itemState & ODS_SELECTED) != 0;

    // Background
    COLORREF bg = selected ? colors_.surface : colors_.background;
    HBRUSH br = CreateSolidBrush(bg);
    FillRect(hdc, &rc, br);
    DeleteObject(br);

    // Active indicator: 3px line at bottom
    if (selected) {
        RECT indicator = { rc.left + 8, rc.bottom - 3, rc.right - 8, rc.bottom };
        HBRUSH indBr = CreateSolidBrush(colors_.primary);
        FillRect(hdc, &indicator, indBr);
        DeleteObject(indBr);
    }

    // Tab text
    wchar_t text[64]{};
    TCITEMW tci = {};
    tci.mask = TCIF_TEXT;
    tci.pszText = text;
    tci.cchTextMax = 64;
    SendMessageW(dis->hwndItem, TCM_GETITEMW, dis->itemID, reinterpret_cast<LPARAM>(&tci));

    SetTextColor(hdc, selected ? colors_.primary : colors_.onSurfaceVariant);
    SetBkMode(hdc, TRANSPARENT);
    SelectObject(hdc, selected ? fonts_.label : fonts_.body);
    DrawTextW(hdc, text, -1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
}

// ── Divider ──

void ClassicTheme::DrawDivider(HDC hdc, int x, int y, int width) {
    RECT line = { x, y, x + width, y + 1 };
    HBRUSH br = CreateSolidBrush(colors_.outlineVariant);
    FillRect(hdc, &line, br);
    DeleteObject(br);
}

// ── Utility ──

COLORREF ClassicTheme::BlendColors(COLORREF base, COLORREF overlay, BYTE alpha) {
    auto blend = [](BYTE b, BYTE o, BYTE a) -> BYTE {
        return static_cast<BYTE>((b * (255 - a) + o * a) / 255);
    };
    return RGB(
        blend(GetRValue(base), GetRValue(overlay), alpha),
        blend(GetGValue(base), GetGValue(overlay), alpha),
        blend(GetBValue(base), GetBValue(overlay), alpha)
    );
}

// ── IsWindows11OrGreater helper ──
// (Also used by Sciter UI in SciterHelper.cpp)
static bool IsWindows11OrGreater() {
    // Windows 11 = build >= 22000
    OSVERSIONINFOEXW osvi = { sizeof(osvi) };
    osvi.dwBuildNumber = 22000;
    DWORDLONG mask = 0;
    VER_SET_CONDITION(mask, VER_BUILDNUMBER, VER_GREATER_EQUAL);
    return VerifyVersionInfoW(&osvi, VER_BUILDNUMBER, mask) != FALSE;
}

} // namespace NextKey::Classic
```

- [ ] **Step 3: Commit**

```bash
git add src/app/classic/ClassicTheme.h src/app/classic/ClassicTheme.cpp
git commit -m "feat(classic): implement ClassicTheme with M3 Indigo palette, owner-draw, dark mode"
```

---

## Task 4: ClassicSettingsDialog — Compact + Advanced Modes

**Files:**
- Modify: `src/app/classic/ClassicSettingsDialog.h` (replace stub)
- Modify: `src/app/classic/ClassicSettingsDialog.cpp` (replace stub)

This is the largest task. The dialog creates all controls programmatically (no .rc dialog template — gives full control over M3 styling).

- [ ] **Step 1: Implement ClassicSettingsDialog.h**

```cpp
// src/app/classic/ClassicSettingsDialog.h
// NexusKey Classic — Win32 Native Settings Dialog
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "ClassicTheme.h"
#include "resource.h"
#include "core/config/TypingConfig.h"
#include "core/SystemConfig.h"
#include "core/UIConfig.h"
#include "core/ipc/SharedStateManager.h"
#include "core/config/ConfigEvent.h"
#include <Windows.h>
#include <commctrl.h>
#include <string>

namespace NextKey::Classic {

class ClassicSettingsDialog {
public:
    ClassicSettingsDialog() = default;
    ~ClassicSettingsDialog();

    /// Show the dialog (blocks until closed). Returns true if settings were changed.
    bool Show(HINSTANCE hInstance, HWND parent = nullptr);

    /// Get the HWND (valid while Show() is running)
    [[nodiscard]] HWND GetHwnd() const noexcept { return hwnd_; }

private:
    // Window creation
    bool RegisterWindowClass(HINSTANCE hInstance);
    void CreateCompactControls();
    void CreateAdvancedControls();
    void ToggleAdvancedMode(bool expand);
    void DestroyWindow();

    // Settings I/O
    void LoadSettings();
    void SaveSettings();
    void SyncToSharedState();
    void SaveToToml();
    void PopulateControls();
    void ReadControlValues();

    // Event handlers
    void OnCommand(WPARAM wParam, LPARAM lParam);
    void OnCheckboxClick(uint16_t controlId);
    void OnDropdownChange(uint16_t controlId);
    void OnTabChange();

    // Tab page management
    void ShowTabPage(int tabIndex);

    // Dialog proc
    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

    // Layout constants (pixels at 96 DPI)
    static constexpr int kCompactWidth = 320;
    static constexpr int kCompactHeight = 260;
    static constexpr int kAdvancedWidth = 500;
    static constexpr int kAdvancedHeight = 480;
    static constexpr int kPadding = 16;
    static constexpr int kControlHeight = 24;
    static constexpr int kComboHeight = 28;
    static constexpr int kButtonHeight = 32;
    static constexpr int kRowGap = 6;

    // Window
    HWND hwnd_ = nullptr;
    HINSTANCE hInstance_ = nullptr;
    ClassicTheme theme_;

    // Compact controls
    HWND comboMethod_ = nullptr;
    HWND comboEncoding_ = nullptr;
    HWND comboSwitchKey_ = nullptr;
    HWND checkExpand_ = nullptr;
    HWND btnClose_ = nullptr;
    HWND btnExit_ = nullptr;

    // Advanced controls
    HWND tabControl_ = nullptr;
    HWND btnDefaults_ = nullptr;
    HWND btnSave_ = nullptr;
    bool isAdvanced_ = false;

    // Checkbox handles (indexed by tab page)
    // Tab 0: up to 16 checkboxes, Tab 1: up to 8, Tab 2: up to 8
    static constexpr int kMaxCheckboxes = 32;
    HWND checkboxes_[kMaxCheckboxes]{};

    // Settings state
    TypingConfig config_;
    HotkeyConfig hotkeyConfig_;
    SystemConfig systemConfig_;

    // IPC
    SharedStateManager sharedState_;
    ConfigEvent configEvent_;

    bool settingsChanged_ = false;

    // Timer for deferred TOML save
    static constexpr UINT_PTR kTimerDeferredSave = 1001;
    static constexpr DWORD kDeferredSaveDelayMs = 30000;
    bool configDirty_ = false;
};

} // namespace NextKey::Classic
```

- [ ] **Step 2: Implement ClassicSettingsDialog.cpp — Window creation + compact mode**

This file is large (~500 lines). Core structure:

```cpp
// src/app/classic/ClassicSettingsDialog.cpp
// NexusKey Classic — Win32 Native Settings Dialog
// SPDX-License-Identifier: GPL-3.0-only

#include "ClassicSettingsDialog.h"
#include "core/config/ConfigManager.h"
#include "core/config/SettingMetadata.h"
#include "core/ipc/SharedState.h"
#include "core/Strings.h"
#include "system/StartupHelper.h"
#include "system/TsfRegistration.h"

namespace NextKey::Classic {

static constexpr wchar_t kWindowClass[] = L"NexusKeyClassicSettings";

ClassicSettingsDialog::~ClassicSettingsDialog() {
    DestroyWindow();
}

bool ClassicSettingsDialog::Show(HINSTANCE hInstance, HWND parent) {
    hInstance_ = hInstance;
    if (!RegisterWindowClass(hInstance)) return false;

    LoadSettings();
    isAdvanced_ = false;  // Start compact

    int width = kCompactWidth;
    int height = kCompactHeight;

    // Center on screen
    int screenW = GetSystemMetrics(SM_CXSCREEN);
    int screenH = GetSystemMetrics(SM_CYSCREEN);
    int x = (screenW - width) / 2;
    int y = (screenH - height) / 2;

    hwnd_ = CreateWindowExW(
        0, kWindowClass, L"NexusKey Settings",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
        x, y, width, height,
        parent, nullptr, hInstance, this);

    if (!hwnd_) return false;

    theme_.Init(hwnd_);
    CreateCompactControls();
    PopulateControls();

    // Set font on all child controls
    EnumChildWindows(hwnd_, [](HWND child, LPARAM lp) -> BOOL {
        auto* self = reinterpret_cast<ClassicSettingsDialog*>(lp);
        SendMessageW(child, WM_SETFONT, reinterpret_cast<WPARAM>(self->theme_.Fonts().body), TRUE);
        return TRUE;
    }, reinterpret_cast<LPARAM>(this));

    ShowWindow(hwnd_, SW_SHOW);
    UpdateWindow(hwnd_);

    // Modal message loop
    MSG msg;
    while (GetMessage(&msg, nullptr, 0, 0)) {
        if (!IsWindow(hwnd_)) break;
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    return settingsChanged_;
}

bool ClassicSettingsDialog::RegisterWindowClass(HINSTANCE hInstance) {
    WNDCLASSEXW wc = { sizeof(wc) };
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = nullptr;  // We handle WM_ERASEBKGND
    wc.lpszClassName = kWindowClass;
    wc.hIcon = LoadIconW(hInstance, MAKEINTRESOURCEW(IDI_APPLICATION));
    return RegisterClassExW(&wc) != 0 || GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
}

void ClassicSettingsDialog::CreateCompactControls() {
    int x = kPadding;
    int y = kPadding;
    int w = kCompactWidth - 2 * kPadding - 16;  // account for NC area

    // Label: Kiểu gõ
    CreateWindowExW(0, L"STATIC", L"Kiểu gõ", WS_CHILD | WS_VISIBLE | SS_LEFT,
        x, y, w, 16, hwnd_, reinterpret_cast<HMENU>(IDC_STATIC_METHOD), hInstance_, nullptr);
    y += 18;

    // Combo: Method
    comboMethod_ = CreateWindowExW(0, WC_COMBOBOXW, nullptr,
        WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_TABSTOP,
        x, y, w, 200, hwnd_, reinterpret_cast<HMENU>(IDC_COMBO_METHOD), hInstance_, nullptr);
    SendMessageW(comboMethod_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Telex"));
    SendMessageW(comboMethod_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"VNI"));
    SendMessageW(comboMethod_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Simple Telex"));
    y += kComboHeight + kRowGap;

    // Label: Bảng mã
    CreateWindowExW(0, L"STATIC", L"Bảng mã", WS_CHILD | WS_VISIBLE | SS_LEFT,
        x, y, w, 16, hwnd_, reinterpret_cast<HMENU>(IDC_STATIC_ENCODING), hInstance_, nullptr);
    y += 18;

    // Combo: Encoding
    comboEncoding_ = CreateWindowExW(0, WC_COMBOBOXW, nullptr,
        WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_TABSTOP,
        x, y, w, 200, hwnd_, reinterpret_cast<HMENU>(IDC_COMBO_ENCODING), hInstance_, nullptr);
    SendMessageW(comboEncoding_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Unicode"));
    SendMessageW(comboEncoding_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"TCVN3 (ABC)"));
    SendMessageW(comboEncoding_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"VNI Windows"));
    SendMessageW(comboEncoding_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Unicode Compound"));
    SendMessageW(comboEncoding_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Vietnamese Locale"));
    y += kComboHeight + kRowGap;

    // Checkbox: Mở rộng
    checkExpand_ = CreateWindowExW(0, WC_BUTTONW, L"Mở rộng",
        WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX | WS_TABSTOP,
        x, y, w, kControlHeight, hwnd_, reinterpret_cast<HMENU>(IDC_CHECK_EXPAND), hInstance_, nullptr);
    y += kControlHeight + kRowGap + 4;

    // Divider (drawn in WM_PAINT)

    // Buttons
    int btnW = (w - 8) / 2;
    btnClose_ = CreateWindowExW(0, WC_BUTTONW, L"Đóng",
        WS_CHILD | WS_VISIBLE | BS_OWNERDRAW | WS_TABSTOP,
        x, y + 4, btnW, kButtonHeight, hwnd_, reinterpret_cast<HMENU>(IDC_BTN_CLOSE), hInstance_, nullptr);

    btnExit_ = CreateWindowExW(0, WC_BUTTONW, L"Kết thúc",
        WS_CHILD | WS_VISIBLE | BS_OWNERDRAW | WS_TABSTOP,
        x + btnW + 8, y + 4, btnW, kButtonHeight, hwnd_, reinterpret_cast<HMENU>(IDC_BTN_EXIT), hInstance_, nullptr);
}

void ClassicSettingsDialog::CreateAdvancedControls() {
    int x = kPadding;
    int contentW = kAdvancedWidth - 2 * kPadding - 16;

    // Tab control below compact section
    int tabY = kCompactHeight - kButtonHeight - kPadding - 10;  // Above buttons
    tabControl_ = CreateWindowExW(0, WC_TABCONTROLW, nullptr,
        WS_CHILD | WS_VISIBLE | TCS_OWNERDRAWFIXED | WS_TABSTOP,
        x, tabY, contentW, kAdvancedHeight - tabY - kButtonHeight - kPadding * 2,
        hwnd_, reinterpret_cast<HMENU>(IDC_TAB_ADVANCED), hInstance_, nullptr);

    // Add tabs
    TCITEMW tci = {};
    tci.mask = TCIF_TEXT;
    tci.pszText = const_cast<wchar_t*>(L"Cơ bản");
    TabCtrl_InsertItem(tabControl_, 0, &tci);
    tci.pszText = const_cast<wchar_t*>(L"Phím tắt");
    TabCtrl_InsertItem(tabControl_, 1, &tci);
    tci.pszText = const_cast<wchar_t*>(L"Hệ thống");
    TabCtrl_InsertItem(tabControl_, 2, &tci);

    SendMessageW(tabControl_, WM_SETFONT, reinterpret_cast<WPARAM>(theme_.Fonts().body), TRUE);

    // Create checkboxes from SettingMetadata table
    RECT tabRect;
    GetClientRect(tabControl_, &tabRect);
    TabCtrl_AdjustRect(tabControl_, FALSE, &tabRect);
    MapWindowPoints(tabControl_, hwnd_, reinterpret_cast<POINT*>(&tabRect), 2);

    int colW = (tabRect.right - tabRect.left - 8) / 2;

    for (size_t i = 0; i < kSettingsCount; ++i) {
        const auto& s = kSettingsTable[i];
        if (s.type != SettingType::Toggle || s.win32Id == 0) continue;

        // Count items in this tab+column to determine Y position
        int itemIndex = 0;
        for (size_t j = 0; j < i; ++j) {
            if (kSettingsTable[j].tab == s.tab && kSettingsTable[j].column == s.column
                && kSettingsTable[j].type == SettingType::Toggle && kSettingsTable[j].win32Id != 0)
                ++itemIndex;
        }

        int cx = tabRect.left + 4 + s.column * (colW + 8);
        int cy = tabRect.top + 4 + itemIndex * (kControlHeight + kRowGap);

        HWND chk = CreateWindowExW(0, WC_BUTTONW, s.label,
            WS_CHILD | BS_AUTOCHECKBOX | WS_TABSTOP,
            cx, cy, colW, kControlHeight,
            hwnd_, reinterpret_cast<HMENU>(static_cast<UINT_PTR>(s.win32Id)), hInstance_, nullptr);

        SendMessageW(chk, WM_SETFONT, reinterpret_cast<WPARAM>(theme_.Fonts().body), TRUE);

        // Store handle for show/hide per tab
        if (s.win32Id < IDC_CHECK_SPELL + kMaxCheckboxes) {
            checkboxes_[s.win32Id - IDC_CHECK_SPELL] = chk;
        }
    }

    // Footer buttons
    int footerY = kAdvancedHeight - kButtonHeight - kPadding - 30;
    int btnW = 130;
    btnDefaults_ = CreateWindowExW(0, WC_BUTTONW, L"Thiết lập mặc định",
        WS_CHILD | WS_VISIBLE | BS_OWNERDRAW | WS_TABSTOP,
        x, footerY, btnW, kButtonHeight,
        hwnd_, reinterpret_cast<HMENU>(IDC_BTN_DEFAULTS), hInstance_, nullptr);

    btnSave_ = CreateWindowExW(0, WC_BUTTONW, L"Lưu thay đổi",
        WS_CHILD | WS_VISIBLE | BS_OWNERDRAW | WS_TABSTOP,
        x + contentW - btnW, footerY, btnW, kButtonHeight,
        hwnd_, reinterpret_cast<HMENU>(IDC_BTN_SAVE), hInstance_, nullptr);

    SendMessageW(btnDefaults_, WM_SETFONT, reinterpret_cast<WPARAM>(theme_.Fonts().label), TRUE);
    SendMessageW(btnSave_, WM_SETFONT, reinterpret_cast<WPARAM>(theme_.Fonts().label), TRUE);

    ShowTabPage(0);
}

void ClassicSettingsDialog::ToggleAdvancedMode(bool expand) {
    isAdvanced_ = expand;
    int width = expand ? kAdvancedWidth : kCompactWidth;
    int height = expand ? kAdvancedHeight : kCompactHeight;

    // Resize window
    RECT rc = { 0, 0, width, height };
    AdjustWindowRectEx(&rc, GetWindowLong(hwnd_, GWL_STYLE), FALSE, GetWindowLong(hwnd_, GWL_EXSTYLE));
    SetWindowPos(hwnd_, nullptr, 0, 0, rc.right - rc.left, rc.bottom - rc.top,
        SWP_NOMOVE | SWP_NOZORDER);

    if (expand && !tabControl_) {
        CreateAdvancedControls();
    }

    // Show/hide advanced controls
    if (tabControl_) ShowWindow(tabControl_, expand ? SW_SHOW : SW_HIDE);
    if (btnDefaults_) ShowWindow(btnDefaults_, expand ? SW_SHOW : SW_HIDE);
    if (btnSave_) ShowWindow(btnSave_, expand ? SW_SHOW : SW_HIDE);

    // Move close/exit buttons to bottom
    if (expand) {
        ShowWindow(btnClose_, SW_HIDE);
        ShowWindow(btnExit_, SW_HIDE);
    } else {
        ShowWindow(btnClose_, SW_SHOW);
        ShowWindow(btnExit_, SW_SHOW);
    }

    InvalidateRect(hwnd_, nullptr, TRUE);
}

void ClassicSettingsDialog::ShowTabPage(int tabIndex) {
    // Show/hide checkboxes based on tab
    for (size_t i = 0; i < kSettingsCount; ++i) {
        const auto& s = kSettingsTable[i];
        if (s.type != SettingType::Toggle || s.win32Id == 0) continue;
        HWND chk = GetDlgItem(hwnd_, s.win32Id);
        if (chk) ShowWindow(chk, s.tab == tabIndex ? SW_SHOW : SW_HIDE);
    }
}

// ── Settings I/O ──

void ClassicSettingsDialog::LoadSettings() {
    config_ = ConfigManager::LoadOrDefault();
    hotkeyConfig_ = ConfigManager::LoadHotkeyConfigOrDefault();
    systemConfig_ = ConfigManager::LoadSystemConfigOrDefault();
    sharedState_.Open();
    configEvent_.Open();
}

void ClassicSettingsDialog::PopulateControls() {
    // Dropdowns
    SendMessageW(comboMethod_, CB_SETCURSEL, static_cast<int>(config_.inputMethod), 0);
    SendMessageW(comboEncoding_, CB_SETCURSEL, static_cast<int>(config_.codeTable), 0);

    // Checkboxes from metadata table
    for (size_t i = 0; i < kSettingsCount; ++i) {
        const auto& s = kSettingsTable[i];
        if (s.type != SettingType::Toggle || s.win32Id == 0) continue;

        bool value = false;
        void* configBase = nullptr;
        switch (s.owner) {
            case SettingOwner::Typing: configBase = &config_; break;
            case SettingOwner::Hotkey: configBase = &hotkeyConfig_; break;
            case SettingOwner::System: configBase = &systemConfig_; break;
            default: continue;
        }
        value = *reinterpret_cast<bool*>(reinterpret_cast<char*>(configBase) + s.offset);

        HWND chk = GetDlgItem(hwnd_, s.win32Id);
        if (chk) {
            CheckDlgButton(hwnd_, s.win32Id, value ? BST_CHECKED : BST_UNCHECKED);
        }
    }
}

void ClassicSettingsDialog::ReadControlValues() {
    // Dropdowns
    config_.inputMethod = static_cast<InputMethod>(SendMessageW(comboMethod_, CB_GETCURSEL, 0, 0));
    config_.codeTable = static_cast<CodeTable>(SendMessageW(comboEncoding_, CB_GETCURSEL, 0, 0));

    // Checkboxes from metadata table
    for (size_t i = 0; i < kSettingsCount; ++i) {
        const auto& s = kSettingsTable[i];
        if (s.type != SettingType::Toggle || s.win32Id == 0) continue;

        bool checked = (IsDlgButtonChecked(hwnd_, s.win32Id) == BST_CHECKED);

        void* configBase = nullptr;
        switch (s.owner) {
            case SettingOwner::Typing: configBase = &config_; break;
            case SettingOwner::Hotkey: configBase = &hotkeyConfig_; break;
            case SettingOwner::System: configBase = &systemConfig_; break;
            default: continue;
        }
        *reinterpret_cast<bool*>(reinterpret_cast<char*>(configBase) + s.offset) = checked;
    }
}

void ClassicSettingsDialog::SyncToSharedState() {
    if (sharedState_.IsConnected()) {
        SharedState state = sharedState_.Read();
        if (state.IsValid()) {
            state.inputMethod = static_cast<uint8_t>(config_.inputMethod);
            state.spellCheck = config_.spellCheckEnabled ? 1 : 0;
            state.codeTable = static_cast<uint8_t>(config_.codeTable);
            state.SetFeatureFlags(EncodeFeatureFlags(config_));
            state.SetHotkey(hotkeyConfig_);
            state.configGeneration++;
            sharedState_.Write(state);
        }
    }
    if (configEvent_.IsValid()) configEvent_.Signal();
}

void ClassicSettingsDialog::SaveToToml() {
    std::wstring path = ConfigManager::GetConfigPath();
    ConfigManager::SaveToFile(path, config_);
    ConfigManager::SaveHotkeyConfig(path, hotkeyConfig_);
    ConfigManager::SaveSystemConfig(path, systemConfig_);
    configDirty_ = false;

    // Bump configGeneration again for TOML-only fields
    if (sharedState_.IsConnected()) {
        SharedState state = sharedState_.Read();
        if (state.IsValid()) {
            state.configGeneration++;
            sharedState_.Write(state);
        }
    }
    if (configEvent_.IsValid()) configEvent_.Signal();
}

void ClassicSettingsDialog::SaveSettings() {
    ReadControlValues();
    SyncToSharedState();
    configDirty_ = true;
    SetTimer(hwnd_, kTimerDeferredSave, kDeferredSaveDelayMs, nullptr);
    settingsChanged_ = true;
}

// ── Event Handlers ──

void ClassicSettingsDialog::OnCommand(WPARAM wParam, LPARAM lParam) {
    uint16_t id = LOWORD(wParam);
    uint16_t code = HIWORD(wParam);

    switch (id) {
    case IDC_CHECK_EXPAND:
        ToggleAdvancedMode(IsDlgButtonChecked(hwnd_, IDC_CHECK_EXPAND) == BST_CHECKED);
        break;

    case IDC_BTN_CLOSE:
        ::DestroyWindow(hwnd_);
        break;

    case IDC_BTN_EXIT:
        PostQuitMessage(0);
        break;

    case IDC_BTN_SAVE:
        SaveSettings();
        SaveToToml();  // Force immediate save
        break;

    case IDC_BTN_DEFAULTS:
        config_ = TypingConfig{};
        hotkeyConfig_ = HotkeyConfig{};
        PopulateControls();
        SaveSettings();
        break;

    case IDC_COMBO_METHOD:
    case IDC_COMBO_ENCODING:
        if (code == CBN_SELCHANGE) SaveSettings();
        break;

    default:
        // Check if it's a checkbox from the settings table
        if (code == BN_CLICKED) {
            if (FindSettingByControlId(id)) {
                SaveSettings();
            }
        }
        break;
    }
}

void ClassicSettingsDialog::OnTabChange() {
    int sel = TabCtrl_GetCurSel(tabControl_);
    ShowTabPage(sel);
}

// ── Window Procedure ──

LRESULT CALLBACK ClassicSettingsDialog::WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    ClassicSettingsDialog* self = nullptr;

    if (msg == WM_NCCREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
        self = static_cast<ClassicSettingsDialog*>(cs->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        self->hwnd_ = hwnd;
    } else {
        self = reinterpret_cast<ClassicSettingsDialog*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }

    if (!self) return DefWindowProcW(hwnd, msg, wParam, lParam);

    switch (msg) {
    case WM_COMMAND:
        self->OnCommand(wParam, lParam);
        return 0;

    case WM_NOTIFY: {
        auto* nmhdr = reinterpret_cast<NMHDR*>(lParam);
        if (nmhdr->idFrom == IDC_TAB_ADVANCED && nmhdr->code == TCN_SELCHANGE) {
            self->OnTabChange();
        }
        return 0;
    }

    case WM_CTLCOLORSTATIC:
        return reinterpret_cast<LRESULT>(self->theme_.OnCtlColorStatic(
            reinterpret_cast<HDC>(wParam), reinterpret_cast<HWND>(lParam)));

    case WM_CTLCOLORBTN:
        return reinterpret_cast<LRESULT>(self->theme_.OnCtlColorBtn(
            reinterpret_cast<HDC>(wParam), reinterpret_cast<HWND>(lParam)));

    case WM_CTLCOLOREDIT:
        return reinterpret_cast<LRESULT>(self->theme_.OnCtlColorEdit(
            reinterpret_cast<HDC>(wParam), reinterpret_cast<HWND>(lParam)));

    case WM_CTLCOLORLISTBOX:
        return reinterpret_cast<LRESULT>(self->theme_.OnCtlColorListBox(
            reinterpret_cast<HDC>(wParam), reinterpret_cast<HWND>(lParam)));

    case WM_CTLCOLORDLG:
        return reinterpret_cast<LRESULT>(self->theme_.OnCtlColorDlg(reinterpret_cast<HDC>(wParam)));

    case WM_ERASEBKGND: {
        HDC hdc = reinterpret_cast<HDC>(wParam);
        RECT rc;
        GetClientRect(hwnd, &rc);
        FillRect(hdc, &rc, self->theme_.BrushBackground());
        return 1;
    }

    case WM_DRAWITEM: {
        auto* dis = reinterpret_cast<DRAWITEMSTRUCT*>(lParam);
        uint16_t id = static_cast<uint16_t>(wParam);
        if (id == IDC_BTN_SAVE) {
            self->theme_.DrawButton(dis, true);   // Primary
            return TRUE;
        }
        if (id == IDC_BTN_CLOSE || id == IDC_BTN_EXIT || id == IDC_BTN_DEFAULTS) {
            self->theme_.DrawButton(dis, false);  // Outlined
            return TRUE;
        }
        if (id == IDC_TAB_ADVANCED) {
            self->theme_.DrawTabItem(dis);
            return TRUE;
        }
        return 0;
    }

    case WM_SETTINGCHANGE:
        if (self->theme_.OnSettingChange(lParam)) {
            InvalidateRect(hwnd, nullptr, TRUE);
            // Repaint all children
            EnumChildWindows(hwnd, [](HWND child, LPARAM) -> BOOL {
                InvalidateRect(child, nullptr, TRUE);
                return TRUE;
            }, 0);
        }
        return 0;

    case WM_TIMER:
        if (wParam == kTimerDeferredSave && self->configDirty_) {
            KillTimer(hwnd, kTimerDeferredSave);
            self->SaveToToml();
        }
        return 0;

    case WM_CLOSE:
        if (self->configDirty_) self->SaveToToml();
        ::DestroyWindow(hwnd);
        return 0;

    case WM_DESTROY:
        self->theme_.Destroy();
        PostQuitMessage(0);
        return 0;
    }

    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

void ClassicSettingsDialog::DestroyWindow() {
    if (hwnd_) {
        ::DestroyWindow(hwnd_);
        hwnd_ = nullptr;
    }
}

} // namespace NextKey::Classic
```

- [ ] **Step 3: Commit**

```bash
git add src/app/classic/ClassicSettingsDialog.h src/app/classic/ClassicSettingsDialog.cpp
git commit -m "feat(classic): implement ClassicSettingsDialog with compact/advanced modes and M3 theme"
```

---

## Task 5: Wire main_lite.cpp — Full Entry Point

**Files:**
- Modify: `src/app/main_lite.cpp`

- [ ] **Step 1: Complete main_lite.cpp with tray icon + settings dialog integration**

Update `main_lite.cpp` to match the full `main.cpp` structure but using `ClassicSettingsDialog` instead of Sciter `SettingsDialog`. Copy the essential initialization from `main.cpp` (SharedState init, HookEngine setup, TrayIcon setup) but replace the Sciter dialog call with ClassicSettingsDialog.

Key differences from main.cpp:
- No Sciter includes or initialization
- No `extern/sciter/include/sciter-main.cpp`
- Uses `ClassicSettingsDialog` for settings
- No `resources.cpp` (Sciter packed UI)

- [ ] **Step 2: Commit**

```bash
git add src/app/main_lite.cpp
git commit -m "feat(classic): wire main_lite.cpp entry point with ClassicSettingsDialog"
```

---

## Task 6: Verify Windows Build

**Files:** None (build verification only)

- [ ] **Step 1: Build on Windows with NEXUSKEY_LITE_MODE=ON**

From WSL, run:
```bash
powershell.exe -Command "cd '\\wsl.localhost\Ubuntu-24.04\home\phatmt\code\NexusKey'; cmake -B build-classic -DNEXUSKEY_LITE_MODE=ON; cmake --build build-classic --target NextKeyLite --config Debug 2>&1"
```

Expected: Compiles and links successfully. `NexusKeyClassic.exe` appears in build output.

- [ ] **Step 2: Verify existing tests still pass**

```bash
cd /home/phatmt/code/NexusKey
cmake -B build-linux -G "Unix Makefiles" -DCMAKE_BUILD_TYPE=Debug
cmake --build build-linux --target NextKeyTests
./build-linux/tests/NextKeyTests
```

Expected: All existing tests pass (Classic UI is Windows-only, doesn't affect test build).

- [ ] **Step 3: Commit any fixes**

If build errors found, fix and commit:
```bash
git add -u
git commit -m "fix(classic): resolve build errors in NextKeyLite target"
```

---

## Future Tasks (not in this plan)

These are intentionally deferred — ship the functional MVP first:

1. **Owner-draw checkboxes** — Currently using BS_AUTOCHECKBOX (system-drawn). Upgrade to BS_OWNERDRAW + ClassicTheme::DrawCheckbox for M3 flat look.
2. **Owner-draw combobox** — Flat dropdown styling.
3. **Hotkey editor** — Tab 1 needs key capture UI (WM_KEYDOWN hook).
4. **App overrides dialog** — Separate modal for per-app settings.
5. **Excluded apps dialog** — Separate modal for app exclusion list.
6. **About dialog** — ClassicAboutDialog with version info.
7. **Animation** — Compact↔Advanced smooth resize transition.
8. **DPI scaling** — WM_DPICHANGED handler to rescale controls.
9. **Refactor Sciter SettingsDialog** — Use SettingMetadata.h to replace if-else chain.
