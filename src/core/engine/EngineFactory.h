// NexusKey - Input Engine Factory
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "IInputEngine.h"
#include "core/config/TypingConfig.h"
#include <memory>

namespace NextKey {

/// Factory for creating input method engines
class EngineFactory {
public:
    /// Create an engine based on configuration
    static std::unique_ptr<IInputEngine> Create(const TypingConfig& config);
    
    /// Create an engine for a specific input method
    static std::unique_ptr<IInputEngine> Create(InputMethod method);
};

}  // namespace NextKey
