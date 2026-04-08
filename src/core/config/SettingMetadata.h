// NexusKey - Setting Metadata Table
// Shared mapping for dual-UI (Sciter + Classic Win32) settings binding
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

#include "core/config/TypingConfig.h"
#include "core/SystemConfig.h"

namespace NextKey {

/// What kind of control this setting maps to
enum class SettingType : uint8_t {
    Toggle,    // Checkbox (bool)
    Dropdown,  // Combo box (enum)
    Action     // Button (no backing field)
};

/// Which config struct owns this field
enum class SettingOwner : uint8_t {
    Typing,  // TypingConfig
    Hotkey,  // HotkeyConfig
    System,  // SystemConfig
    UI       // UI-only (no persistent field)
};

/// Metadata for one user-configurable setting
struct SettingMeta {
    const wchar_t* id;       // Sciter DOM id (e.g. L"spell-check")
    SettingType    type;      // Toggle, Dropdown, Action
    SettingOwner   owner;     // Typing, Hotkey, System, UI
    ptrdiff_t      offset;    // offsetof(OwnerStruct, field)
    const wchar_t* label;     // Vietnamese label
    const wchar_t* labelEn;   // English label
    uint16_t       win32Id;   // IDC_ control ID (0 = no control)
    uint8_t        tab;       // 0=Cơ bản, 1=Phím tắt, 2=Hệ thống
    uint8_t        column;    // 0=left, 1=right
};

// ── Helper macros to reduce verbosity ──────────────────────────────
#define NK_TYPING(id, field, vi, en, idc, tab, col)                          \
    { L##id, SettingType::Toggle, SettingOwner::Typing,                      \
      static_cast<ptrdiff_t>(offsetof(TypingConfig, field)),                 \
      L##vi, L##en, idc, tab, col }

#define NK_HOTKEY(id, field, vi, en, idc, tab, col)                          \
    { L##id, SettingType::Toggle, SettingOwner::Hotkey,                      \
      static_cast<ptrdiff_t>(offsetof(HotkeyConfig, field)),                 \
      L##vi, L##en, idc, tab, col }

#define NK_SYSTEM(id, field, vi, en, idc, tab, col)                          \
    { L##id, SettingType::Toggle, SettingOwner::System,                      \
      static_cast<ptrdiff_t>(offsetof(SystemConfig, field)),                 \
      L##vi, L##en, idc, tab, col }

/// All toggle settings, ordered by tab → column → visual position
inline constexpr SettingMeta kSettings[] = {

    // ── Tab 0: Cơ bản — Left column (col 0) ──
    NK_TYPING("spell-check",          spellCheckEnabled,
              "Kiểm tra chính tả",    "Spell check",                   2201, 0, 0),
    NK_TYPING("modern-ortho",         modernOrtho,
              "Bỏ dấu kiểu mới",     "Modern tone placement",         2202, 0, 0),
    NK_TYPING("auto-caps",            autoCaps,
              "Tự động viết hoa",     "Auto capitalize",               2203, 0, 0),
    NK_TYPING("allow-zwjf",           allowZwjf,
              "Cho phép z, w, j, f",  "Allow z, w, j, f keys",        2204, 0, 0),
    NK_TYPING("restore-key",          autoRestoreEnabled,
              "Phục hồi phím",        "Restore key on invalid",        2205, 0, 0),
    NK_TYPING("smart-switch",         smartSwitch,
              "Tự động chuyển mã",    "Smart input switch",            2206, 0, 0),
    NK_TYPING("exclude-apps",         excludeApps,
              "Loại trừ ứng dụng",    "Exclude apps",                  2207, 0, 0),
    NK_TYPING("allow-english-bypass", allowEnglishBypass,
              "Gõ dấu tự do",         "Bypass English blocking",       2208, 0, 0),

    // ── Tab 0: Cơ bản — Right column (col 1) ──
    NK_TYPING("beep-sound",           beepOnSwitch,
              "Tiếng bíp khi chuyển", "Beep on switch",                2211, 0, 1),
    NK_TYPING("use-macro",            macroEnabled,
              "Gõ tắt",               "Enable macros",                 2212, 0, 1),
    NK_TYPING("macro-english",        macroInEnglish,
              "Gõ tắt cả tiếng Anh",  "Macros in English mode",       2213, 0, 1),
    NK_TYPING("quick-telex",          quickConsonant,
              "Gõ nhanh phụ âm",      "Quick consonant",               2214, 0, 1),
    NK_TYPING("quick-start",          quickStartConsonant,
              "Gõ nhanh phụ âm đầu",  "Quick start consonant",         2215, 0, 1),
    NK_TYPING("quick-end",            quickEndConsonant,
              "Gõ nhanh phụ âm cuối", "Quick end consonant",           2216, 0, 1),
    NK_TYPING("temp-off-spell",       tempOffSpellByCtrl,
              "Ctrl tắt tạm chính tả","Ctrl temp off spell check",     2217, 0, 1),
    NK_TYPING("temp-off-openkey",     tempOffByAlt,
              "Alt tắt tạm bộ gõ",    "Alt temp off Vietnamese",       2218, 0, 1),

    // ── Tab 1: Phím tắt (col 0) ──
    NK_HOTKEY("key-ctrl",             ctrl,
              "Ctrl",                  "Ctrl",                          2301, 1, 0),
    NK_HOTKEY("key-shift",            shift,
              "Shift",                 "Shift",                         2302, 1, 0),
    NK_HOTKEY("key-alt",              alt,
              "Alt",                   "Alt",                           2303, 1, 0),
    NK_HOTKEY("key-win",              win,
              "Win",                   "Win",                           2304, 1, 0),

    // ── Tab 2: Hệ thống — Left column (col 0) ──
    NK_SYSTEM("run-startup",          runAtStartup,
              "Chạy cùng Windows",     "Run at startup",               2401, 2, 0),
    NK_SYSTEM("run-admin",            runAsAdmin,
              "Chạy với quyền admin",  "Run as admin",                 2402, 2, 0),
    NK_SYSTEM("show-on-startup",      showOnStartup,
              "Hiện cửa sổ khi khởi động", "Show window on startup",  2403, 2, 0),
    NK_SYSTEM("desktop-shortcut",     desktopShortcut,
              "Tạo lối tắt desktop",   "Desktop shortcut",             2404, 2, 0),

    // ── Tab 2: Hệ thống — Right column (col 1) ──
    NK_SYSTEM("floating-icon",        showFloatingIcon,
              "Biểu tượng nổi",        "Floating icon",                2405, 2, 1),
    NK_SYSTEM("check-update",         autoCheckUpdate,
              "Tự động cập nhật",       "Auto check update",           2406, 2, 1),
};

#undef NK_TYPING
#undef NK_HOTKEY
#undef NK_SYSTEM

/// Total number of entries
inline constexpr size_t kSettingsCount = sizeof(kSettings) / sizeof(kSettings[0]);

// ── Lookup functions ───────────────────────────────────────────────

/// Find setting by Sciter DOM id string. Returns nullptr if not found.
[[nodiscard]] inline constexpr const SettingMeta* FindSetting(const wchar_t* id) noexcept {
    for (size_t i = 0; i < kSettingsCount; ++i) {
        // constexpr-friendly wchar_t comparison
        const wchar_t* a = kSettings[i].id;
        const wchar_t* b = id;
        bool match = true;
        while (*a || *b) {
            if (*a != *b) { match = false; break; }
            ++a; ++b;
        }
        if (match) return &kSettings[i];
    }
    return nullptr;
}

/// Find setting by Win32 control ID. Returns nullptr if not found.
[[nodiscard]] inline constexpr const SettingMeta* FindSettingByControlId(uint16_t controlId) noexcept {
    if (controlId == 0) return nullptr;
    for (size_t i = 0; i < kSettingsCount; ++i) {
        if (kSettings[i].win32Id == controlId) return &kSettings[i];
    }
    return nullptr;
}

}  // namespace NextKey
