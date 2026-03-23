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

// Feature flag bit definitions (uint16_t stored as featureFlags[2] little-endian)
namespace FeatureFlags {
    // Byte 0 (bits 0-7)
    constexpr uint16_t MODERN_ORTHO         = 0x0001;
    constexpr uint16_t AUTO_CAPS            = 0x0002;
    constexpr uint16_t ALLOW_ZWJF           = 0x0004;
    constexpr uint16_t AUTO_RESTORE         = 0x0008;
    constexpr uint16_t TEMP_OFF_SPELL_CTRL  = 0x0010;
    constexpr uint16_t REMEMBER_CODE_TABLE  = 0x0020;
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

    // ── Feature flags (2 bytes, little-endian uint16_t) ──
    uint8_t  featureFlags[2]; // Bitmask for optional features (see FeatureFlags namespace)

    // ── Extended config (1 byte, carved from reserved) ──
    uint8_t  codeTable;       // CodeTable enum value (0=Unicode, 1=TCVN3, etc.)

    // ── Reserved for future expansion (30 bytes) ──
    uint8_t  reserved[30];

    static constexpr uint32_t MAGIC_VALUE = 0x59454B4E;    // 'NKEY'
    static constexpr uint32_t CURRENT_VERSION = 2;          // v2: added structVersion, structSize, reserved

    [[nodiscard]] bool IsValid() const noexcept {
        return magic == MAGIC_VALUE
            && structVersion <= CURRENT_VERSION
            && structSize >= 24;  // Minimum: header + epoch + flags + config
    }

    [[nodiscard]] uint16_t GetFeatureFlags() const noexcept {
        return featureFlags[0] | (static_cast<uint16_t>(featureFlags[1]) << 8);
    }

    void SetFeatureFlags(uint16_t ff) noexcept {
        featureFlags[0] = static_cast<uint8_t>(ff);
        featureFlags[1] = static_cast<uint8_t>(ff >> 8);
    }

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
        for (auto& b : reserved) b = 0;
    }
};

// Ensure SharedState layout is stable across EXE and DLL builds
static_assert(sizeof(SharedState) == 56, "SharedState size changed — update structVersion");

/// Encode TypingConfig feature bools → uint16_t bitmask
[[nodiscard]] inline uint16_t EncodeFeatureFlags(const TypingConfig& config) noexcept {
    uint16_t flags = 0;
    if (config.modernOrtho)        flags |= FeatureFlags::MODERN_ORTHO;
    if (config.autoCaps)           flags |= FeatureFlags::AUTO_CAPS;
    if (config.allowZwjf)          flags |= FeatureFlags::ALLOW_ZWJF;
    if (config.autoRestoreEnabled) flags |= FeatureFlags::AUTO_RESTORE;
    if (config.tempOffSpellByCtrl) flags |= FeatureFlags::TEMP_OFF_SPELL_CTRL;
    if (config.tempOffByAlt)       flags |= FeatureFlags::TEMP_OFF_BY_ALT;
    if (config.rememberCodeTable)  flags |= FeatureFlags::REMEMBER_CODE_TABLE;
    if (config.beepOnSwitch)       flags |= FeatureFlags::BEEP_ON_SWITCH;
    if (config.macroEnabled)       flags |= FeatureFlags::MACRO_ENABLED;
    if (config.macroInEnglish)     flags |= FeatureFlags::MACRO_IN_ENGLISH;
    if (config.quickConsonant)     flags |= FeatureFlags::QUICK_CONSONANT;
    if (config.quickStartConsonant) flags |= FeatureFlags::QUICK_START_CONSONANT;
    if (config.quickEndConsonant)   flags |= FeatureFlags::QUICK_END_CONSONANT;
    if (config.tempOffMacroByEsc)   flags |= FeatureFlags::TEMP_OFF_MACRO_ESC;
    if (config.smartSwitch)         flags |= FeatureFlags::SMART_SWITCH;
    if (config.excludeApps)         flags |= FeatureFlags::EXCLUDE_APPS;
    return flags;
}

/// Decode uint16_t bitmask → TypingConfig feature bools
inline void DecodeFeatureFlags(uint16_t flags, TypingConfig& config) noexcept {
    config.modernOrtho        = (flags & FeatureFlags::MODERN_ORTHO) != 0;
    config.autoCaps           = (flags & FeatureFlags::AUTO_CAPS) != 0;
    config.allowZwjf          = (flags & FeatureFlags::ALLOW_ZWJF) != 0;
    config.autoRestoreEnabled = (flags & FeatureFlags::AUTO_RESTORE) != 0;
    config.tempOffSpellByCtrl = (flags & FeatureFlags::TEMP_OFF_SPELL_CTRL) != 0;
    config.tempOffByAlt       = (flags & FeatureFlags::TEMP_OFF_BY_ALT) != 0;
    config.rememberCodeTable  = (flags & FeatureFlags::REMEMBER_CODE_TABLE) != 0;
    config.beepOnSwitch       = (flags & FeatureFlags::BEEP_ON_SWITCH) != 0;
    config.macroEnabled       = (flags & FeatureFlags::MACRO_ENABLED) != 0;
    config.macroInEnglish     = (flags & FeatureFlags::MACRO_IN_ENGLISH) != 0;
    config.quickConsonant     = (flags & FeatureFlags::QUICK_CONSONANT) != 0;
    config.quickStartConsonant = (flags & FeatureFlags::QUICK_START_CONSONANT) != 0;
    config.quickEndConsonant   = (flags & FeatureFlags::QUICK_END_CONSONANT) != 0;
    config.tempOffMacroByEsc   = (flags & FeatureFlags::TEMP_OFF_MACRO_ESC) != 0;
    config.smartSwitch         = (flags & FeatureFlags::SMART_SWITCH) != 0;
    config.excludeApps         = (flags & FeatureFlags::EXCLUDE_APPS) != 0;
}

}  // namespace NextKey
