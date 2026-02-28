// NexusKey - SharedState Header
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <cstdint>
#include "TypingConfig.h"

namespace NextKey {

// Flag bit definitions (must be before SharedState)
namespace SharedFlags {
    constexpr uint32_t VIETNAMESE_MODE = 0x0001;
    constexpr uint32_t ENGINE_ENABLED  = 0x0002;
    constexpr uint32_t SPELL_CHECK     = 0x0004;
}

// Feature flag bit definitions for SharedState.featureFlags
namespace FeatureFlags {
    constexpr uint8_t MODERN_ORTHO = 0x01;
    constexpr uint8_t AUTO_CAPS    = 0x02;
    constexpr uint8_t ALLOW_ZWJF   = 0x04;
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

    // ── Config data (4 bytes) ──
    uint8_t  inputMethod;     // 0=Telex, 1=VNI, 2=SimpleTelex
    uint8_t  spellCheck;      // Spell check enabled
    uint8_t  optimizeLevel;   // Optimization level
    uint8_t  featureFlags;    // Phase 3: bitmask for optional features

    // ── Reserved for future expansion (32 bytes) ──
    uint8_t  reserved[32];

    static constexpr uint32_t MAGIC_VALUE = 0x59454B4E;    // 'NKEY'
    static constexpr uint32_t CURRENT_VERSION = 2;          // v2: added structVersion, structSize, reserved

    [[nodiscard]] bool IsValid() const noexcept {
        return magic == MAGIC_VALUE
            && structVersion <= CURRENT_VERSION
            && structSize >= 24;  // Minimum: header + epoch + flags + config
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
        featureFlags = FeatureFlags::ALLOW_ZWJF;  // Default: tone keys enabled
        for (auto& b : reserved) b = 0;
    }
};

/// Encode TypingConfig feature bools → SharedState.featureFlags bitmask
[[nodiscard]] inline uint8_t EncodeFeatureFlags(const TypingConfig& config) noexcept {
    uint8_t flags = 0;
    if (config.modernOrtho) flags |= FeatureFlags::MODERN_ORTHO;
    if (config.autoCaps)    flags |= FeatureFlags::AUTO_CAPS;
    if (config.allowZwjf)   flags |= FeatureFlags::ALLOW_ZWJF;
    return flags;
}

/// Decode SharedState.featureFlags bitmask → TypingConfig feature bools
inline void DecodeFeatureFlags(uint8_t flags, TypingConfig& config) noexcept {
    config.modernOrtho = (flags & FeatureFlags::MODERN_ORTHO) != 0;
    config.autoCaps    = (flags & FeatureFlags::AUTO_CAPS) != 0;
    config.allowZwjf   = (flags & FeatureFlags::ALLOW_ZWJF) != 0;
}

}  // namespace NextKey
