// NexusKey - Typing Configuration
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <cstdint>

namespace NextKey {

/// Input method types
enum class InputMethod : uint8_t {
    Telex = 0,
    VNI = 1,
    SimpleTelex = 2  // w/[/] as literal when standalone (no vowel context)
};

/// Output encoding (bảng mã)
enum class CodeTable : uint8_t {
    Unicode = 0,
    TCVN3 = 1,
    VNIWindows = 2,
    UnicodeCompound = 3,
    VietnameseLocale = 4
};

/// Hotkey configuration for V/E toggle (internal, separate from Windows KL switching)
struct HotkeyConfig {
    bool ctrl = false;
    bool shift = false;
    bool alt = false;
    bool win = false;
    wchar_t key = 0;  // e.g. 'Z' for Alt+Z. Default: none (user must configure)
};

/// Typing configuration loaded from TOML, used by engine
struct TypingConfig {
    InputMethod inputMethod = InputMethod::Telex;
    CodeTable codeTable = CodeTable::Unicode;
    bool spellCheckEnabled = false;
    bool beepOnSwitch = false;
    bool smartSwitch = false;
    bool excludeApps = false;  // Exclude apps feature toggle
    uint8_t optimizeLevel = 0;  // 0 = off, 1 = basic, 2 = aggressive
    bool modernOrtho = false;   // Modern tone placement (oà, uý)
    bool autoCaps = false;      // Auto-capitalize first letter of sentence
    bool allowZwjf = true;      // z/w/j/f act as tone/modifier keys (normal Vietnamese)

    // Default constructor for compiled defaults (FR8 - engine autonomy)
    TypingConfig() = default;
};

}  // namespace NextKey
