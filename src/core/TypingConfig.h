// NexusKey - Typing Configuration
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <cstdint>

namespace NextKey {

/// Input method types
enum class InputMethod : uint8_t {
    Telex = 0,
    VNI = 1
};

/// Typing configuration loaded from TOML, used by engine
struct TypingConfig {
    InputMethod inputMethod = InputMethod::Telex;
    bool spellCheckEnabled = false;
    uint8_t optimizeLevel = 0;  // 0 = off, 1 = basic, 2 = aggressive
    
    // Default constructor for compiled defaults (FR8 - engine autonomy)
    TypingConfig() = default;
};

}  // namespace NextKey
