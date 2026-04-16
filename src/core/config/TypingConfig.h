// NexusKey - Typing Configuration
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace NextKey {

/// Input method types
enum class InputMethod : uint8_t {
    Telex = 0,
    VNI = 1,
    SimpleTelex = 2,  // w/[/] as literal when standalone (no vowel context)
    Combined = 3      // Telex + VNI triggers simultaneously
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
    bool tsfApps = false;      // Use TSF engine for listed apps (skip hook)
    uint8_t optimizeLevel = 0;  // 0 = off, 1 = basic, 2 = aggressive
    bool modernOrtho = false;   // Modern tone placement (oà, uý)
    bool autoCaps = false;      // Auto-capitalize first letter of sentence
    bool allowZwjf = false;     // z/w/j/f act as tone/modifier keys (normal Vietnamese)
    bool autoRestoreEnabled = false;  // Restore raw keys when word is invalid
    bool tempOffByAlt = false;        // Double-Alt tap temporarily disables Vietnamese for current word
    bool macroEnabled = false;         // Allow macro/shorthand expansion
    bool macroInEnglish = false;       // Allow macros even when Vietnamese mode is off
    bool quickConsonant = false;       // Quick typing: cc→ch, gg→gi, nn→ng
    bool quickStartConsonant = false;  // Quick start consonant: f→ph, j→gi, w→qu
    bool quickEndConsonant = false;    // Quick end consonant: g→ng, h→nh, k→ch
    bool tempOffMacroByEsc = false;    // Esc temporarily disables macro for next word
    bool autoCapsMacro = false;        // Auto-capitalize expansion to match typed case
    bool allowEnglishBypass = false;   // Cho phép gõ dấu tự do / Bypass English blocking (e.g. yes -> ýe)
    std::vector<std::wstring> spellExclusions;  // Spell check exclusion prefixes (e.g. "hđ", "đp")

    // Default constructor for compiled defaults (FR8 - engine autonomy)
    TypingConfig() = default;
};

/// Configuration for quick-convert hotkey feature
struct ConvertConfig {
    // Toggle options (which conversions are enabled)
    bool allCaps = false;         // Chuyển sang chữ HOA
    bool allLower = false;        // Chuyển sang chữ thường
    bool capsFirst = false;       // Đặt chữ Hoa đầu câu
    bool capsEach = false;        // Đặt chữ Hoa Sau Mỗi Từ
    bool removeMark = false;      // Loại bỏ dấu câu
    bool alertDone = false;       // Thông báo khi chuyển xong
    bool autoPaste = false;       // Tự động dán + bôi đen
    bool sequential = false;      // Convert tuần tự
    bool enableLog = false;       // Bật/tắt log debug

    // Encoding conversion
    uint8_t sourceEncoding = 0;   // CodeTable enum value
    uint8_t destEncoding = 0;     // CodeTable enum value

    // Hotkey for quick-convert
    HotkeyConfig hotkey{};
};

}  // namespace NextKey
