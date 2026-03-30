// NexusKey - SharedState Header
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <cstdint>
#include "core/config/TypingConfig.h"

namespace NextKey {

// Flag bit definitions (must be before SharedState)
namespace SharedFlags {
    constexpr uint32_t VIETNAMESE_MODE = 0x0001;
    constexpr uint32_t ENGINE_ENABLED  = 0x0002;
    constexpr uint32_t SPELL_CHECK     = 0x0004;
    constexpr uint32_t TSF_ACTIVE      = 0x0008;  // Foreground app uses TSF engine (hook sets, DLL reads)
}

// Feature flag bit definitions (uint32_t packed into 3 bytes: featureFlags[2] + extFeatureFlags)
namespace FeatureFlags {
    // Byte 0 (bits 0-7)
    constexpr uint16_t MODERN_ORTHO         = 0x0001;
    constexpr uint16_t AUTO_CAPS            = 0x0002;
    constexpr uint16_t ALLOW_ZWJF           = 0x0004;
    constexpr uint16_t AUTO_RESTORE         = 0x0008;
    constexpr uint16_t TEMP_OFF_SPELL_CTRL  = 0x0010;
    constexpr uint16_t TEMP_OFF_BY_ALT      = 0x0040;
    constexpr uint16_t BEEP_ON_SWITCH      = 0x0080;
    // Byte 1 (bits 8-15)
    constexpr uint16_t MACRO_ENABLED        = 0x0100;
    constexpr uint16_t MACRO_IN_ENGLISH     = 0x0200;
    constexpr uint16_t QUICK_CONSONANT      = 0x0400;
    constexpr uint16_t QUICK_START_CONSONANT = 0x0800;
    constexpr uint16_t QUICK_END_CONSONANT   = 0x1000;
    constexpr uint16_t TEMP_OFF_MACRO_ESC    = 0x2000;
    constexpr uint16_t SMART_SWITCH          = 0x4000;
    constexpr uint16_t EXCLUDE_APPS          = 0x8000;
    // Extended flags (byte 2, bits 16-23) — stored in extFeatureFlags
    constexpr uint32_t AUTO_CAPS_MACRO       = 0x00010000;
    constexpr uint32_t ALLOW_ENGLISH_BYPASS  = 0x00020000;
}

/// SharedState struct for IPC between Core and Engine
/// Layout is versioned for forward compatibility (Phase 3+ expansion).
/// Magic: 0x59454B4E ('NKEY')
///
/// Seqlock protocol using epoch field:
///   Writer: epoch++ (now odd = writing), copy data, epoch++ (now even = done)
///   Reader: read epoch, copy data, verify epoch unchanged AND even → retry if not
struct SharedState {
    // ── Header (12 bytes) ──
    uint32_t magic;           // Magic identifier: 'NKEY' = 0x59454B4E
    uint32_t structVersion;   // Struct layout version (increment on layout change)
    uint32_t structSize;      // sizeof(SharedState) for forward compat

    // ── Synchronization (4 bytes) ──
    uint32_t epoch;           // Seqlock counter (even = stable, odd = write in progress)

    // ── Runtime flags (4 bytes) ──
    uint32_t flags;           // Runtime flags (Vietnamese mode, engine enabled, etc.)

    // ── Config data (3 bytes) ──
    uint8_t  inputMethod;     // 0=Telex, 1=VNI, 2=SimpleTelex
    uint8_t  spellCheck;      // Spell check enabled
    uint8_t  optimizeLevel;   // Optimization level

    // ── Feature flags (3 bytes, little-endian) ──
    uint8_t  featureFlags[2]; // Bitmask for optional features (bits 0-15)
    uint8_t  extFeatureFlags; // Extended feature flags (bits 16-23)

    // ── Extended config (1 byte) ──
    uint8_t  codeTable;       // CodeTable enum value (0=Unicode, 1=TCVN3, etc.)

    // ── Hotkey config (6 bytes, packed: 1 byte modifiers + 2 bytes key each) ──
    uint8_t  hotkeyMods;      // bits: [0]=ctrl [1]=shift [2]=alt [3]=win
    uint8_t  hotkeyKeyLo;     // wchar_t key, low byte
    uint8_t  hotkeyKeyHi;     // wchar_t key, high byte
    uint8_t  convertMods;     // convert hotkey modifiers (same encoding)
    uint8_t  convertKeyLo;    // convert hotkey key, low byte
    uint8_t  convertKeyHi;    // convert hotkey key, high byte

    // ── Reserved for future expansion (23 bytes) ──
    uint8_t  reserved[23];

    static constexpr uint32_t MAGIC_VALUE = 0x59454B4E;    // 'NKEY'
    static constexpr uint32_t CURRENT_VERSION = 2;          // v2: added structVersion, structSize, reserved

    [[nodiscard]] bool IsValid() const noexcept {
        return magic == MAGIC_VALUE
            && structVersion <= CURRENT_VERSION
            && structSize >= 24;  // Minimum: header + epoch + flags + config
    }

    [[nodiscard]] uint32_t GetFeatureFlags() const noexcept {
        return featureFlags[0]
             | (static_cast<uint32_t>(featureFlags[1]) << 8)
             | (static_cast<uint32_t>(extFeatureFlags) << 16);
    }

    void SetFeatureFlags(uint32_t ff) noexcept {
        featureFlags[0] = static_cast<uint8_t>(ff);
        featureFlags[1] = static_cast<uint8_t>(ff >> 8);
        extFeatureFlags = static_cast<uint8_t>(ff >> 16);
    }

    // ── Hotkey encode/decode helpers ──
    void SetHotkey(const HotkeyConfig& hk) noexcept {
        hotkeyMods = (hk.ctrl ? 1 : 0) | (hk.shift ? 2 : 0) | (hk.alt ? 4 : 0) | (hk.win ? 8 : 0);
        hotkeyKeyLo = static_cast<uint8_t>(hk.key);
        hotkeyKeyHi = static_cast<uint8_t>(hk.key >> 8);
    }
    [[nodiscard]] HotkeyConfig GetHotkey() const noexcept {
        HotkeyConfig hk;
        hk.ctrl  = (hotkeyMods & 1) != 0;
        hk.shift = (hotkeyMods & 2) != 0;
        hk.alt   = (hotkeyMods & 4) != 0;
        hk.win   = (hotkeyMods & 8) != 0;
        hk.key   = static_cast<wchar_t>(hotkeyKeyLo | (static_cast<uint16_t>(hotkeyKeyHi) << 8));
        return hk;
    }
    // Convert hotkey fields are reserved for future migration.
    // Currently convert hotkey is managed by ConvertToolDialog (separate subprocess)
    // and read from TOML only. Wire here when ConvertToolDialog gets SharedState access.

    /// Initialize with defaults
    void InitDefaults() noexcept {
        magic = MAGIC_VALUE;
        structVersion = CURRENT_VERSION;
        structSize = sizeof(SharedState);
        epoch = 0;
        flags = SharedFlags::VIETNAMESE_MODE | SharedFlags::ENGINE_ENABLED;
        inputMethod = 0;  // Telex
        spellCheck = 0;
        optimizeLevel = 0;
        SetFeatureFlags(FeatureFlags::ALLOW_ZWJF);  // Default: tone keys enabled
        codeTable = 0;  // Unicode
        hotkeyMods = 0; hotkeyKeyLo = 0; hotkeyKeyHi = 0;
        convertMods = 0; convertKeyLo = 0; convertKeyHi = 0;
        for (auto& b : reserved) b = 0;
    }
};

// Ensure SharedState layout is stable across EXE and DLL builds
static_assert(sizeof(SharedState) == 56, "SharedState size changed — update structVersion");

/// Encode TypingConfig feature bools → uint32_t bitmask (3 bytes used)
[[nodiscard]] inline uint32_t EncodeFeatureFlags(const TypingConfig& config) noexcept {
    uint32_t flags = 0;
    if (config.modernOrtho)        flags |= FeatureFlags::MODERN_ORTHO;
    if (config.autoCaps)           flags |= FeatureFlags::AUTO_CAPS;
    if (config.allowZwjf)          flags |= FeatureFlags::ALLOW_ZWJF;
    if (config.autoRestoreEnabled) flags |= FeatureFlags::AUTO_RESTORE;
    if (config.tempOffSpellByCtrl) flags |= FeatureFlags::TEMP_OFF_SPELL_CTRL;
    if (config.tempOffByAlt)       flags |= FeatureFlags::TEMP_OFF_BY_ALT;
    if (config.beepOnSwitch)       flags |= FeatureFlags::BEEP_ON_SWITCH;
    if (config.macroEnabled)       flags |= FeatureFlags::MACRO_ENABLED;
    if (config.macroInEnglish)     flags |= FeatureFlags::MACRO_IN_ENGLISH;
    if (config.quickConsonant)     flags |= FeatureFlags::QUICK_CONSONANT;
    if (config.quickStartConsonant) flags |= FeatureFlags::QUICK_START_CONSONANT;
    if (config.quickEndConsonant)   flags |= FeatureFlags::QUICK_END_CONSONANT;
    if (config.tempOffMacroByEsc)   flags |= FeatureFlags::TEMP_OFF_MACRO_ESC;
    if (config.smartSwitch)         flags |= FeatureFlags::SMART_SWITCH;
    if (config.excludeApps)         flags |= FeatureFlags::EXCLUDE_APPS;
    if (config.autoCapsMacro)       flags |= FeatureFlags::AUTO_CAPS_MACRO;
    if (config.allowEnglishBypass)  flags |= FeatureFlags::ALLOW_ENGLISH_BYPASS;
    return flags;
}

/// Decode uint32_t bitmask → TypingConfig feature bools
inline void DecodeFeatureFlags(uint32_t flags, TypingConfig& config) noexcept {
    config.modernOrtho        = (flags & FeatureFlags::MODERN_ORTHO) != 0;
    config.autoCaps           = (flags & FeatureFlags::AUTO_CAPS) != 0;
    config.allowZwjf          = (flags & FeatureFlags::ALLOW_ZWJF) != 0;
    config.autoRestoreEnabled = (flags & FeatureFlags::AUTO_RESTORE) != 0;
    config.tempOffSpellByCtrl = (flags & FeatureFlags::TEMP_OFF_SPELL_CTRL) != 0;
    config.tempOffByAlt       = (flags & FeatureFlags::TEMP_OFF_BY_ALT) != 0;
    config.beepOnSwitch       = (flags & FeatureFlags::BEEP_ON_SWITCH) != 0;
    config.macroEnabled       = (flags & FeatureFlags::MACRO_ENABLED) != 0;
    config.macroInEnglish     = (flags & FeatureFlags::MACRO_IN_ENGLISH) != 0;
    config.quickConsonant     = (flags & FeatureFlags::QUICK_CONSONANT) != 0;
    config.quickStartConsonant = (flags & FeatureFlags::QUICK_START_CONSONANT) != 0;
    config.quickEndConsonant   = (flags & FeatureFlags::QUICK_END_CONSONANT) != 0;
    config.tempOffMacroByEsc   = (flags & FeatureFlags::TEMP_OFF_MACRO_ESC) != 0;
    config.smartSwitch         = (flags & FeatureFlags::SMART_SWITCH) != 0;
    config.excludeApps         = (flags & FeatureFlags::EXCLUDE_APPS) != 0;
    config.autoCapsMacro       = (flags & FeatureFlags::AUTO_CAPS_MACRO) != 0;
    config.allowEnglishBypass  = (flags & FeatureFlags::ALLOW_ENGLISH_BYPASS) != 0;
}

}  // namespace NextKey
