// NexusKey - Shared Engine Helper Functions
// SPDX-License-Identifier: GPL-3.0-only
//
// Template helpers shared between TelexEngine and VniEngine.
// Eliminates logic duplication for spell check and auto-restore.

#pragma once

#include "SpellChecker.h"
#include "core/config/TypingConfig.h"
#include <string>

namespace NextKey {

/// Update spell check state — works with both Telex::CharState and Vni::CharState.
/// Call after every PushChar/Backspace to revalidate the syllable.
template<typename CharStateT>
inline void UpdateSpellCheck(const CharStateT* states, size_t count,
                             const TypingConfig& config, bool tempSpellOff,
                             bool& spellCheckDisabled) noexcept {
    if (!config.spellCheckEnabled || count == 0) {
        spellCheckDisabled = false;
        return;
    }
    if (tempSpellOff) {
        spellCheckDisabled = false;
        return;
    }
    auto result = SpellCheck::Validate(states, count, config.allowZwjf);
    spellCheckDisabled = (result == SpellCheck::Result::Invalid);
}

/// Check if auto-restore should return raw input instead of composed text.
/// Restores when composed has diacritics or same/shorter length (meaning
/// modifiers changed letters without escape expanding the string).
inline bool ShouldAutoRestore(const std::wstring& raw, const std::wstring& composed) noexcept {
    if (raw == composed) return false;
    for (wchar_t ch : composed) {
        if (ch > 0x7F) return true;  // Has diacritics → restore raw
    }
    return raw.length() <= composed.length();
}

}  // namespace NextKey
