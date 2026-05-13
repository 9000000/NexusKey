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

    /// Get current composition (without committing).
    /// Returns const ref to internal buffer — valid until next mutation (PushChar/Backspace/Reset).
    [[nodiscard]] virtual const std::wstring& Peek() const = 0;

    /// Commit composition and get final text, then reset state
    [[nodiscard]] virtual std::wstring Commit() = 0;

    /// Reset engine state (clear composition)
    virtual void Reset() = 0;

    /// Get number of characters in current composition
    [[nodiscard]] virtual size_t Count() const = 0;

    /// Check if a quick consonant expansion (e.g., nn->ng, cc->ch) is currently active
    [[nodiscard]] virtual bool HasActiveQuickConsonant() const = 0;

    /// Seed engine state from existing Vietnamese text (e.g., when user BS back into
    /// committed text). Decomposes each char into base + modifier + tone and rebuilds
    /// CharState array. State is fully reset before seeding.
    ///
    /// Returns false if any char isn't a recognized Vietnamese letter — caller should
    /// pass a single syllable with no whitespace/punctuation/digits. On failure the
    /// engine is left in Reset() state.
    [[nodiscard]] virtual bool SeedFromText(const std::wstring& text) = 0;

    /// Whether current buffer is flagged as a hard-English word (e.g., "hello",
    /// "approved", "system") by the engine's English-protection heuristics.
    /// Call AFTER SeedFromText — caller decides whether to revive composition.
    [[nodiscard]] virtual bool IsEnglishWord() const = 0;

    /// Raw keys typed by the user (case-preserved), independent of any Vietnamese
    /// transformation in the composed buffer. Used by the Esc-restore feature to
    /// recover the original keystrokes (e.g., composed "víu" ← raw "virus").
    /// Default no-op for engines that don't track raw input.
    [[nodiscard]] virtual std::wstring PeekRaw() const { return {}; }
};

}  // namespace NextKey
