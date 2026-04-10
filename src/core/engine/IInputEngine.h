// NexusKey - Input Engine Interface
// Copyright (c) 2024-2026 PhatMT. All rights reserved.
// SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-NexusKey-Commercial
// Dual-licensed: GPL-3.0 for open-source use, commercial license for proprietary use.
// See LICENSE and LICENSE-COMMERCIAL in the project root.

#pragma once

#include <string>
#include <cstdint>

namespace NextKey {

/// Abstract interface for input method engines (Telex, VNI, etc.)
/// NFR7: No global state - all state is instance-based
class IInputEngine {
public:
    virtual ~IInputEngine() = default;

    /// Push a character to the engine for processing
    virtual void PushChar(wchar_t c) = 0;

    /// Handle backspace - remove last character
    virtual void Backspace() = 0;

    /// Get current composition (without committing)
    [[nodiscard]] virtual std::wstring Peek() const = 0;

    /// Commit composition and get final text, then reset state
    [[nodiscard]] virtual std::wstring Commit() = 0;

    /// Reset engine state (clear composition)
    virtual void Reset() = 0;

    /// Get number of characters in current composition
    [[nodiscard]] virtual size_t Count() const = 0;

    /// Check if a quick consonant expansion (e.g., nn->ng, cc->ch) is currently active
    [[nodiscard]] virtual bool HasActiveQuickConsonant() const = 0;
};

}  // namespace NextKey
