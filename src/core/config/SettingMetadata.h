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
    { L##id, ::NextKey::SettingType::Toggle, ::NextKey::SettingOwner::Typing,                      \
      static_cast<ptrdiff_t>(offsetof(::NextKey::TypingConfig, field)),                 \
      L##vi, L##en, idc, tab, col }

#define NK_HOTKEY(id, field, vi, en, idc, tab, col)                          \
    { L##id, ::NextKey::SettingType::Toggle, ::NextKey::SettingOwner::Hotkey,                      \
      static_cast<ptrdiff_t>(offsetof(::NextKey::HotkeyConfig, field)),                 \
      L##vi, L##en, idc, tab, col }

#define NK_SYSTEM(id, field, vi, en, idc, tab, col)                          \
    { L##id, ::NextKey::SettingType::Toggle, ::NextKey::SettingOwner::System,                      \
      static_cast<ptrdiff_t>(offsetof(::NextKey::SystemConfig, field)),                 \
      L##vi, L##en, idc, tab, col }

#define NK_DROPDOWN(id, field, vi, en, idc, tab, col)                        \
    { L##id, ::NextKey::SettingType::Dropdown, ::NextKey::SettingOwner::System,                    \
      static_cast<ptrdiff_t>(offsetof(::NextKey::SystemConfig, field)),                 \
      L##vi, L##en, idc, tab, col }

#define NK_ACTION(id, vi, en, idc, tab, col)                                 \
    { L##id, ::NextKey::SettingType::Action, ::NextKey::SettingOwner::UI,                          \
      0,                                                                     \
      L##vi, L##en, idc, tab, col }

/// All toggle settings, ordered by tab → column → visual position
inline constexpr SettingMeta kSettings[] = {

    // ── Tab 0: Bảng gõ — Left column (col 0) ──
    NK_TYPING("modern-ortho",         modernOrtho,
              "Đặt dấu oà, uý",       "Modern tone placement",         2202, 0, 0),
    NK_TYPING("allow-english-bypass", allowEnglishBypass,
              "Gõ tự do",             "Bypass English blocking",       2208, 0, 0),
    NK_TYPING("auto-caps",            autoCaps,
              "Viết hoa chữ cái đầu", "Auto capitalize",               2203, 0, 0),
    NK_TYPING("allow-zwjf",           allowZwjf,
              "Cho phép z, w, j, f",  "Allow z, w, j, f",              2204, 0, 0),
    NK_TYPING("spell-check",          spellCheckEnabled,
              "Kiểm tra chính tả",    "Spell check",                   2201, 0, 0),
    NK_TYPING("restore-key",          autoRestoreEnabled,
              "Tự khôi phục phím sai","Restore key on invalid",        2205, 0, 0),

    // ── Tab 0: Bảng gõ — Right column (col 1) ──
    NK_TYPING("beep-sound",           beepOnSwitch,
              "Tiếng bíp khi chuyển",   "Beep on switch",               2211, 0, 1),
    NK_TYPING("temp-off-spell",       tempOffSpellByCtrl,
              "Tạm tắt chính tả bằng Ctrl","Ctrl temps off spell check",2217, 0, 1),
    NK_TYPING("temp-off-openkey",     tempOffByAlt,
              "Tạm tắt bộ gõ bằng Alt",    "Alt temps off Vietnamese",  2218, 0, 1),
    NK_TYPING("smart-switch",         smartSwitch,
              "Lưu chế độ gõ theo app",   "Smart input switch",         2206, 0, 1),
    NK_ACTION("btn-smart-switch",     "...", "",                      2501, 0, 1),
    NK_TYPING("exclude-apps",         excludeApps,
              "Tắt tiếng việt theo app",  "Exclude apps",               2207, 0, 1),
    NK_ACTION("btn-exclude-apps",     "...", "",                      2502, 0, 1),

    // ── Tab 1: Gõ tắt (col 0) ──
    NK_TYPING("use-macro",            macroEnabled,
              "Cho phép gõ tắt",      "Enable macros",                 2212, 1, 0),
    NK_TYPING("macro-english",        macroInEnglish,
              "Gõ tắt khi tắt tiếng việt", "Macros in English mode",  2213, 1, 0),
    NK_TYPING("auto-caps-macro",      autoCapsMacro,
              "Tự động viết hoa theo phím", "Auto capitalize macros",  2219, 1, 0),
    NK_TYPING("cancel-macro-esc",     tempOffMacroByEsc,
              "Tạm bỏ gõ tắt bằng Esc",     "Temp skip macro by Esc",  2220, 1, 0),
    NK_ACTION("btn-macro-table",      "Bảng gõ tắt", "Macro Table",   2503, 1, 0),

    // ── Hotkeys (rendered in Compact mode, logically attached to Settings) ──
    NK_HOTKEY("key-ctrl",             ctrl,
              "Ctrl",                  "Ctrl",                          2301, 1, 0),
    NK_HOTKEY("key-shift",            shift,
              "Shift",                 "Shift",                         2302, 1, 0),
    NK_HOTKEY("key-alt",              alt,
              "Alt",                   "Alt",                           2303, 1, 0),
    NK_HOTKEY("key-win",              win,
              "Win",                   "Win",                           2304, 1, 0),

    // ── Tab 1: Gõ tắt (col 1) ──
    NK_TYPING("quick-telex",          quickConsonant,
              "Gõ nhanh phụ âm kép",  "Quick double consonant",        2214, 1, 1),
    NK_TYPING("quick-start",          quickStartConsonant,
              "Gõ tắt phụ âm đầu",    "Quick start consonant",         2215, 1, 1),
    NK_TYPING("quick-end",            quickEndConsonant,
              "Gõ tắt phụ âm cuối",   "Quick end consonant",           2216, 1, 1),

    // ── Tab 2: Hệ thống — Left column (col 0) ──
    NK_SYSTEM("run-startup",          runAtStartup,
              "Khởi động cùng Windows", "Run at startup",              2401, 2, 0),
    NK_SYSTEM("show-on-startup",      showOnStartup,
              "Bật bảng này khi khởi động", "Show window on startup", 2403, 2, 0),
    NK_SYSTEM("run-admin",            runAsAdmin,
              "Chạy với quyền admin",  "Run as admin",                 2402, 2, 0),
    NK_SYSTEM("desktop-shortcut",     desktopShortcut,
              "Tạo biểu tượng desktop","Desktop shortcut",             2404, 2, 0),

    // ── Tab 2: Hệ thống — Right column (col 1) ──
    NK_SYSTEM("floating-icon",        showFloatingIcon,
              "Icon V/E nổi",          "Floating icon",                2405, 2, 1),
    NK_DROPDOWN("custom-icon-style",  iconStyle,
              "Tuỳ chỉnh icon",       "Icon Style",                    2408, 2, 1),
    NK_SYSTEM("check-update",         autoCheckUpdate,
              "Tự kiểm tra cập nhật", "Auto check update",             2406, 2, 1),
    NK_ACTION("btn-check-update",     "Kiểm tra", "Check",             2504, 2, 1),
};

#undef NK_TYPING
#undef NK_HOTKEY
#undef NK_SYSTEM
#undef NK_DROPDOWN
#undef NK_ACTION

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
