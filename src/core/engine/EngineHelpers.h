// NexusKey - Shared Engine Helper Functions
// Copyright (c) 2024-2026 PhatMT. All rights reserved.
// SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-NexusKey-Commercial
// Dual-licensed: GPL-3.0 for open-source use, commercial license for proprietary use.
// See LICENSE and LICENSE-COMMERCIAL in the project root.
//
// Template helpers shared between TelexEngine and VniEngine.
// Eliminates logic duplication for spell check and auto-restore.

#pragma once

#include "SpellChecker.h"
#include "EnglishProtection.h"
#include "core/config/TypingConfig.h"
#include <string>

namespace NextKey {

/// Check if the composed buffer matches any spell exclusion prefix (case-insensitive).
/// Exclusion entries must be >= 2 chars. Match is prefix-based: "hđ" covers "hđt", "hđqt".
template<typename CharStateT, typename ComposeFunc>
inline bool IsSpellExcluded(const CharStateT* states, size_t count,
                            const std::vector<std::wstring>& exclusions,
                            ComposeFunc compose) {
    if (exclusions.empty() || count == 0) return false;
    // Build composed buffer for matching
    std::wstring buf;
    buf.reserve(count);
    for (size_t i = 0; i < count; ++i) {
        wchar_t ch = compose(states[i]);
        if (ch != 0) buf += towlower(ch);
    }
    for (const auto& pat : exclusions) {
        if (pat.size() < 2) continue;
        if (buf.size() < pat.size()) continue;
        // Case-insensitive prefix match
        bool match = true;
        for (size_t i = 0; i < pat.size(); ++i) {
            if (towlower(pat[i]) != buf[i]) { match = false; break; }
        }
        if (match) return true;
    }
    return false;
}

/// Update spell check state — works with both Telex::CharState and Vni::CharState.
/// Call after every PushChar/Backspace to revalidate the syllable.
template<typename CharStateT, typename ComposeFunc>
inline void UpdateSpellCheck(const CharStateT* states, size_t count,
                             const TypingConfig& config,
                             bool& spellCheckDisabled,
                             ComposeFunc compose) {
    if (!config.spellCheckEnabled || count == 0) {
        spellCheckDisabled = false;
        return;
    }
    if (IsSpellExcluded(states, count, config.spellExclusions, compose)) {
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

/// Check if the user intentionally typed đ and the result looks like an abbreviation.
/// Returns true (keep composed) when ALL of:
///   1. Raw input has consecutive "dd"/"DD" or "d9" (Telex/VNI stroke pattern)
///   2. Composed output actually contains đ/Đ (the stroke fired, not blocked)
///   3. No plain ASCII vowels (a,e,i,o,u,y) appear after the last đ in composed
///      (abbreviations are đ + consonants like "đt"; typing attempts have unmodified
///       vowels like "đwa" with plain 'a')
/// This protects abbreviations (đt, đh) while letting failed typing attempts
/// (đwa, đp+vowel) auto-restore. For full control, use the spell exclusion list.
template<typename RawT>
inline bool HasIntentionalStrokeD(const RawT& rawInput,
                                  const std::wstring& composed) noexcept {
    // Step 1: raw input must have consecutive dd or d9
    bool hasRawDD = false;
    for (size_t i = 0; i + 1 < rawInput.size(); ++i) {
        wchar_t ch = rawInput[i];
        if (ch == L'd' || ch == L'D') {
            wchar_t next = rawInput[i + 1];
            if (next == L'd' || next == L'D' || next == L'9') { hasRawDD = true; break; }
        }
    }
    if (!hasRawDD) return false;

    // Step 2: composed must contain đ/Đ — if the stroke was blocked (e.g.,
    // spellCheckDisabled_ prevented dd→đ), there's nothing to protect.
    size_t lastStrokeD = std::wstring::npos;
    for (size_t i = 0; i < composed.size(); ++i) {
        if (composed[i] == L'\u0111' || composed[i] == L'\u0110') lastStrokeD = i;
    }
    if (lastStrokeD == std::wstring::npos) return false;

    // Step 3: after the last đ, only consonants (no plain vowels).
    // Abbreviations: đt, đh, hđ, vđề (ề is > 0x7F, not plain ASCII vowel).
    // Typing attempts: đwa (plain 'a'), đia (plain 'i','a').
    for (size_t i = lastStrokeD + 1; i < composed.size(); ++i) {
        wchar_t ch = towlower(composed[i]);
        if (ch == L'a' || ch == L'e' || ch == L'i' || ch == L'o' || ch == L'u' || ch == L'y') {
            return false;
        }
    }
    return true;
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

/// When allowZwjf is disabled AND spell check is on, treat w/z/j/f as initial
/// consonant → HardEnglish.  When spell check is off, z/w/j/f are always
/// allowed (the toggle has no effect).
/// Call after CheckEnglishBias() or RecalcEnglishBias() to layer this check.
template<typename CharStateT>
inline void CheckZwjfInitialBias(const CharStateT* states, size_t count,
                                  const TypingConfig& config,
                                  EnglishProtectionState& engProt) noexcept {
    if (config.allowZwjf || !config.spellCheckEnabled || count == 0) return;
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

/// Pre-check for stroke-D modifier: returns true if applying đ would create an
/// invalid consonant cluster (e.g., "drop" + d → "đrop" with coda "p" already present).
/// When onset 'd' (position 0) is immediately followed by a vowel (e.g. "doc"),
/// applying stroke gives [đ + vowel + coda] — perfectly valid Vietnamese structure.
/// Works with both Telex::CharState and Vni::CharState.
template<typename CharStateT>
[[nodiscard]] inline bool IsStrokeDBlockedByCoda(
        const CharStateT* states, size_t count, size_t dTarget) noexcept {
    bool isSimpleOnsetVowelCoda = (dTarget == 0 && count > 1 && states[1].IsVowel());
    if (isSimpleOnsetVowelCoda) return false;

    size_t codaLen = 0;
    for (size_t j = count; j-- > 0;) {
        if (states[j].IsVowel() || states[j].IsD()) break;
        ++codaLen;
    }
    return codaLen >= 1;
}

/// Typing-time check: would applying dd→đ produce a word matching a spell exclusion?
/// Simulates stroke on the d-target, builds composed buffer with 'd' replaced by 'đ',
/// then does bidirectional prefix match (buffer may be shorter than exclusion while typing).
template<typename CharStateT, typename ComposeFunc>
[[nodiscard]] inline bool WouldStrokeDMatchExclusion(
        const CharStateT* states, size_t count,
        const std::vector<std::wstring>& exclusions,
        ComposeFunc compose) {
    if (exclusions.empty() || count == 0) return false;

    size_t dIdx = FindStrokeDTarget(states, count);
    if (dIdx == SIZE_MAX) return false;

    wchar_t buf[16];
    size_t bufLen = 0;
    for (size_t i = 0; i < count && bufLen < 16; ++i) {
        wchar_t ch = compose(states[i]);
        if (ch == 0) continue;
        buf[bufLen++] = (i == dIdx) ? L'\u0111' : towlower(ch);
    }

    for (const auto& pat : exclusions) {
        if (pat.size() < 2) continue;
        size_t cmpLen = std::min(bufLen, pat.size());
        if (cmpLen == 0) continue;
        bool match = true;
        for (size_t i = 0; i < cmpLen; ++i) {
            if (towlower(pat[i]) != buf[i]) { match = false; break; }
        }
        if (match) return true;
    }
    return false;
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

/// Check if states_ contains a modifier that the given key could escape.
/// Used by both TelexEngine and VniEngine to bypass the spellCheckDisabled_ gate
/// for modifier escape (ww undoes horn, dd undoes stroke, etc.).
/// @param mod  The modifier type the key would apply/escape.
/// @param isStroke  True if checking for stroke-d (searches IsD() instead of IsVowel()).
template<typename CharStateT, typename ModifierT>
[[nodiscard]] inline bool HasEscapableModifier(
        const CharStateT* states, size_t count, ModifierT mod,
        bool isStroke = false) noexcept {
    for (size_t i = 0; i < count; ++i) {
        if (isStroke) {
            if (states[i].IsD() && states[i].mod == mod) return true;
        } else {
            if (states[i].IsVowel() && states[i].mod == mod) return true;
        }
    }
    return false;
}

/// Shared FindToneTarget algorithm — returns the index of the vowel that should
/// receive the tone mark, using priority: P1 horn > P2 modified > P3 diphthong > P4 rightmost.
/// Returns SIZE_MAX if no vowel found. Used by both TelexEngine and VniEngine.
template<typename CharStateT>
[[nodiscard]] inline size_t FindToneTargetImpl(
        const CharStateT* states, size_t count,
        const uint8_t table[6][6], bool checkTriphthongs) noexcept {
    size_t lastHornIdx = SIZE_MAX;
    size_t firstModifiedIdx = SIZE_MAX;
    size_t v3rd = SIZE_MAX;
    size_t v2nd = SIZE_MAX;
    size_t vLast = SIZE_MAX;
    size_t vowelCount = 0;

    for (size_t i = 0; i < count; ++i) {
        if (!states[i].IsVowel()) continue;
        if (IsClusterConsonant(states, count, i)) continue;

        v3rd = v2nd;
        v2nd = vLast;
        vLast = i;
        ++vowelCount;

        if (states[i].IsHorn()) lastHornIdx = i;
        if (firstModifiedIdx == SIZE_MAX && states[i].HasModifier())
            firstModifiedIdx = i;
    }

    if (vowelCount == 0) return SIZE_MAX;

    // Priority 1: Horn vowels (last one for ươ)
    if (lastHornIdx != SIZE_MAX) return lastHornIdx;

    // Priority 2: Other modified vowels (â, ê, ô, ă)
    if (firstModifiedIdx != SIZE_MAX) return firstModifiedIdx;

    // Priority 3: Diphthong/triphthong rules
    if (vowelCount >= 2 && vLast == v2nd + 1) {
        // Triphthongs (Modern only): tone on MIDDLE vowel
        if (checkTriphthongs && vowelCount >= 3 && v3rd != SIZE_MAX &&
            v2nd == v3rd + 1 && vLast == v2nd + 1) {
            if (IsTriphthong(states[v3rd].base, states[v2nd].base, states[vLast].base))
                return v2nd;
        }

        // Diphthong table lookup
        int fi = DiphthongVowelIndex(states[v2nd].base);
        int li = DiphthongVowelIndex(states[vLast].base);
        if (fi >= 0 && li >= 0) {
            uint8_t rule = table[fi][li];

            // Rule 3: Rising diphthongs (oa, oe) - SECOND with coda, FIRST without
            if (rule == 3) {
                rule = (vLast + 1 < count) ? 2 : 1;
            }

            if (rule == 1) return v2nd;    // tone on FIRST
            if (rule == 2) return vLast;   // tone on SECOND
        }
    }

    // Default: rightmost vowel
    return vLast;
}

/// P4 guard for tone relocation: returns true if the target vowel was selected
/// by FindToneTarget's P4 default (rightmost vowel, no diphthong rule for the
/// last two vowels). Prevents tone sliding when repeated vowels are typed,
/// e.g., "Kìaaaa" — tone stays on 'i', doesn't drift to the last 'a'.
/// Legitimate P3 relocations (coda changing a rule-3 target) have rule != 0.
template<typename CharStateT>
[[nodiscard]] inline bool IsToneRelocBlockedByP4(
        const CharStateT* states, size_t count,
        size_t targetIdx, bool modernOrtho) noexcept {
    // P1/P2 targets (horn, circumflex, breve) always attract the tone.
    if (states[targetIdx].HasModifier()) return false;

    // Find last two non-cluster vowels
    size_t vLast = SIZE_MAX, v2nd = SIZE_MAX;
    for (size_t i = count; i-- > 0;) {
        if (!states[i].IsVowel()) continue;
        if (IsClusterConsonant(states, count, i)) continue;
        if (vLast == SIZE_MAX) { vLast = i; continue; }
        v2nd = i;
        break;
    }
    if (targetIdx != vLast || v2nd == SIZE_MAX || vLast != v2nd + 1) return false;

    int fi = DiphthongVowelIndex(states[v2nd].base);
    int li = DiphthongVowelIndex(states[vLast].base);
    if (fi < 0 || li < 0) return false;

    const auto& table = modernOrtho ? kDiphthongModern : kDiphthongClassic;
    return table[fi][li] == 0;  // No diphthong rule → P4 default → block
}

}  // namespace NextKey
