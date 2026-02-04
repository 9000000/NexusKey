// NexusKey - TelexEngine Adapter Header
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "TelexEngine.h"
#include "KeyTranslator.h"
#include <memory>

namespace NextKey {

/// Adapter that wraps TelexEngine for TSF integration
/// Handles virtual key to character translation
class TelexEngineAdapter {
public:
    explicit TelexEngineAdapter(const TypingConfig& config);
    ~TelexEngineAdapter();

    // Non-copyable
    TelexEngineAdapter(const TelexEngineAdapter&) = delete;
    TelexEngineAdapter& operator=(const TelexEngineAdapter&) = delete;

    /// Process a key down event
    /// Returns true if the key was handled
    bool ProcessKeyDown(uint32_t vkCode, bool shiftPressed);

    /// Process backspace key
    void ProcessBackspace();

    /// Get current composition text
    std::wstring GetComposition() const;

    /// Commit composition and get final text
    std::wstring Commit();

    /// Reset engine state
    void Reset();

    /// Check if there's an active composition
    bool HasComposition() const;

private:
    std::unique_ptr<Telex::TelexEngine> engine_;
    KeyTranslator keyTranslator_;
};

}  // namespace NextKey
