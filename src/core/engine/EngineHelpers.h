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

/// Check if the consonant onset before 'u' at uIdx forms an edge case prefix
/// where uơ (not ươ) is a valid Vietnamese word: h (huơ), th (thuở), kh (khuơ).
/// Used by the uo horn cycle to determine 3-state vs 2-state toggle.
template<typename CharStateT>
[[nodiscard]] inline bool IsUOEdgeCasePrefix(
        const CharStateT* states, size_t /*count*/, size_t uIdx) noexcept {
    if (uIdx == 1 && towlower(states[0].base) == L'h') return true;       // h + uo
    if (uIdx == 2) {
        wchar_t c0 = towlower(states[0].base);
        wchar_t c1 = towlower(states[1].base);
        if (c1 == L'h' && (c0 == L't' || c0 == L'k')) return true;        // th/kh + uo
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

/// Find the target 'd' for stroke modifier (dd→đ / d9→đ).
/// Returns index of the 'd' to modify, or SIZE_MAX if none found or blocked.
/// Scans backward for last 'd', then checks that the contiguous 'd' cluster
/// is NOT preceded by a vowel (blocks "added"→"ađed", allows "vdd"→"vđ").
/// Works with both Telex::CharState and Vni::CharState.
template<typename CharStateT>
[[nodiscard]] inline size_t FindStrokeDTarget(
        const CharStateT* states, size_t count) noexcept {
    if (count == 0) return SIZE_MAX;

    size_t dIdx = SIZE_MAX;
    for (size_t i = count; i-- > 0;) {
        if (states[i].IsD()) { dIdx = i; break; }
    }
    if (dIdx == SIZE_MAX) return SIZE_MAX;

    // For non-initial 'd', scan past contiguous 'd' cluster to find real predecessor.
    // Block if preceded by vowel (prevents "added"→"ađed", "oddly"→"ođly").
    if (dIdx > 0) {
        size_t checkIdx = dIdx;
        while (checkIdx > 0 && states[checkIdx - 1].IsD()) --checkIdx;
        if (checkIdx > 0 && states[checkIdx - 1].IsVowel()) return SIZE_MAX;
    }
    return dIdx;
}

/// Returns true if the buffer ends with a stop-final consonant (c, ch, k, p, t)
/// preceded by at least one vowel. Scans backward from end.
/// Stop finals only accept Acute and Dot tones in Vietnamese phonology.
/// Used by pre-tone check to block Grave/Hook/Tilde before ProcessTone().
template<typename CharStateT>
[[nodiscard]] inline bool HasStopFinalCoda(
        const CharStateT* states, size_t count) noexcept {
    if (count < 2) return false;

    // Scan backward from end to find coda start (stop at first vowel)
    size_t codaEnd = count;
    size_t codaStart = codaEnd;
    while (codaStart > 0 && !states[codaStart - 1].IsVowel()) {
        --codaStart;
    }
    size_t codaLen = codaEnd - codaStart;

    // Must be preceded by a vowel (loop exit guarantees this when codaStart > 0)
    if (codaStart == 0) return false;

    if (codaLen == 1) {
        wchar_t c = towlower(states[codaStart].base);
        return c == L'c' || c == L'k' || c == L'p' || c == L't';
    }
    if (codaLen == 2) {
        wchar_t c0 = towlower(states[codaStart].base);
        wchar_t c1 = towlower(states[codaStart + 1].base);
        return (c0 == L'c' && c1 == L'h');  // ch is the only stop digraph
    }
    return false;
}

}  // namespace NextKey
