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

/// Check if any state contains đ (d with modifier).
/// When user typed dd→đ, they clearly intended the stroke — skip auto-restore
/// so abbreviations like "đt" are not reverted to "ddt".
template<typename CharStateT>
inline bool HasStrokeD(const CharStateT* states, size_t count) noexcept {
    for (size_t i = 0; i < count; ++i) {
        if (states[i].IsD() && states[i].HasModifier()) {
            return true;
        }
    }
    return false;
}

/// Undo ươ pair: if 'o' at oIndex has a modifier and preceding 'u' also has one,
/// clear the u's modifier. Handles: ươ→uô (horn undo), uu→ươ backspace, w escape.
/// Works with both Telex and Vni CharState.
template<typename CharStateT>
inline void UndoHornU(CharStateT* states, size_t oIndex) noexcept {
    if (oIndex > 0 && states[oIndex].base == L'o' && states[oIndex].HasModifier() &&
        states[oIndex - 1].base == L'u' && states[oIndex - 1].HasModifier()) {
        states[oIndex - 1].mod = {};  // Clear to None (value-initialized enum = 0)
    }
}

}  // namespace NextKey
