// NexusKey - Shared Engine Helper Functions
// SPDX-License-Identifier: GPL-3.0-only
//
// Template helpers shared between TelexEngine and VniEngine.
// Eliminates logic duplication for spell check and auto-restore.

#pragma once

#include "SpellChecker.h"
#include "EnglishProtection.h"
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

/// Check if the user intentionally typed đ by looking for the stroke pattern in raw input.
/// Telex: consecutive "dd" (e.g., "ddt"→"đt").  VNI: "d" followed by "9" (e.g., "d9t"→"đt").
/// This protects abbreviations from auto-restore, while allowing words like "download"
/// (non-consecutive d's) to be restored correctly.
template<typename RawT>
inline bool HasIntentionalStrokeD(const RawT& rawInput) noexcept {
    for (size_t i = 0; i + 1 < rawInput.size(); ++i) {
        wchar_t ch = rawInput[i];
        if (ch == L'd' || ch == L'D') {
            wchar_t next = rawInput[i + 1];
            if (next == L'd' || next == L'D' || next == L'9') return true;
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

/// Recalculate English protection bias after backspace.
/// Resets bias and re-checks from scratch with current states.
/// Call after Backspace() modifies the state buffer.
template<typename CharStateT>
inline void RecalcEnglishBias(const CharStateT* states, size_t count,
                              EnglishProtectionState& engProt) noexcept {
    engProt.Reset();
    if (count >= 2) {
        CheckEnglishBias(states, count, engProt);
    }
}

/// When allowZwjf is disabled, treat w/z/j/f as initial consonant → HardEnglish.
/// Works independently of spell check — uses English Protection bias.
/// Call after CheckEnglishBias() or RecalcEnglishBias() to layer this check.
template<typename CharStateT>
inline void CheckZwjfInitialBias(const CharStateT* states, size_t count,
                                  const TypingConfig& config,
                                  EnglishProtectionState& engProt) noexcept {
    if (config.allowZwjf || count == 0) return;
    if (engProt.bias == LanguageBias::Vietnamese) return;  // Respect confirmed VN intent
    if (states[0].IsVowel()) return;
    wchar_t initialChar = states[0].base;
    if (initialChar == L'w' || initialChar == L'z' || initialChar == L'j' || initialChar == L'f') {
        engProt.bias = LanguageBias::HardEnglish;
    }
}

}  // namespace NextKey
