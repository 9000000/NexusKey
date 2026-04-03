// NexusKey - Localized String Dictionary
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <cstdint>

namespace NextKey {

/// UI display language
enum class Language : uint8_t { Vietnamese = 0, English = 1 };

/// String table identifiers
enum class StringId : uint16_t {
    // Tray menu
    MENU_TOGGLE_VIET,
    MENU_SPELL_CHECK,
    MENU_SMART_SWITCH,
    MENU_MACRO_TOGGLE,
    MENU_MACRO_CONFIG,
    MENU_CONVERT_TOOL,
    MENU_QUICK_CONVERT,
    MENU_INPUT_METHOD,
    MENU_CODE_TABLE,
    MENU_UNICODE_COMPOUND,
    MENU_SETTINGS,
    MENU_ABOUT,
    MENU_EXIT,

    // Tooltips
    TIP_VIETNAMESE,
    TIP_ENGLISH,

    // About dialog
    ABOUT_TITLE,
    ABOUT_BODY,

    // Update
    UPDATE_TITLE,
    UPDATE_AVAILABLE_TITLE,
    UPDATE_AVAILABLE_BODY,
    UPDATE_NOW,
    UPDATE_SKIP,
    UPDATE_LATEST,
    UPDATE_FAILED,
    UPDATE_DOWNLOADING,
    UPDATE_CHANGELOG,
    UPDATE_SUCCESS,
    UPDATE_INSTALL_FAILED,

    // Convert tool
    CONVERT_NO_SOURCE_FILE,
    CONVERT_READ_ERROR,
    CONVERT_CLIPBOARD_EMPTY,
    CONVERT_NO_DEST_FILE,
    CONVERT_WRITE_ERROR,
    CONVERT_CLIPBOARD_WRITE_ERROR,
    CONVERT_SUCCESS,

    _COUNT
};

/// Set the active UI language (thread-safe)
void SetLanguage(Language lang) noexcept;

/// Get the active UI language (thread-safe)
[[nodiscard]] Language GetLanguage() noexcept;

/// Look up a localized string by ID (zero-overhead array lookup)
[[nodiscard]] const wchar_t* S(StringId id) noexcept;

}  // namespace NextKey
