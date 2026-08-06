// VKey - Input Engine Factory
// Copyright (c) 2024-2026 PhatMT. All rights reserved.
// SPDX-License-Identifier: AGPL-3.0-only

#pragma once

#include "IInputEngine.h"
#include "core/config/TypingConfig.h"
#include <memory>

namespace NextKey {

/// Factory for creating input method engines
class EngineFactory {
public:
    /// Whether Create(config) will select the Rust-backed implementation.
    /// Kept beside Create so status/reporting code cannot drift from fallback
    /// behavior when the runtime library is unavailable.
    [[nodiscard]] static bool WillUseRustEngine(const TypingConfig& config);

    /// True when the config asks for the Rust engine and this build ships it,
    /// yet the library cannot be used (missing, untrusted, ABI mismatch) — so
    /// Create() silently falls back to TypingEngine and Advanced spell-check
    /// loses lexicon auto-correct. Drives the restart banner; without it the
    /// only trace is a log line. Always false in builds compiled without the
    /// Rust engine, where the C++ engine is the intended backend.
    [[nodiscard]] static bool RustEngineExpectedButUnavailable(const TypingConfig& config);

    /// Create an engine based on configuration
    [[nodiscard]] static std::unique_ptr<IInputEngine> Create(const TypingConfig& config);

    /// Create an engine for a specific input method
    [[nodiscard]] static std::unique_ptr<IInputEngine> Create(InputMethod method);
};

}  // namespace NextKey
