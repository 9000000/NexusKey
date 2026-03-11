// NexusKey - English Protection Module (Header-Only)
// SPDX-License-Identifier: GPL-3.0-only
//
// 3-Tier English Protection System:
//   TIER 1: Hard reject impossible patterns (cl, cr, ending x/r/z/f)
//   TIER 2: Soft bias for ambiguous patterns (y + vowel)
//   TIER 3: User override via same-key insistence
//
// Only active when spellCheckEnabled = true.
// All functions are constexpr/inline — zero runtime overhead.

#pragma once

#include <cstddef>
#include <cwctype>

namespace NextKey {

// =============================================================================
// Language Bias State
// =============================================================================

enum class LanguageBias : uint8_t {
    Unknown,      // Initial state, not yet determined
    HardEnglish,  // TIER 1: Definitely not Vietnamese (disable composition)
    SoftEnglish,  // TIER 2: Ambiguous, defer tone until user insists
    Vietnamese    // Confirmed Vietnamese pattern
};

// Shared state struct — embedded in both TelexEngine and VniEngine
struct EnglishProtectionState {
    LanguageBias bias = LanguageBias::Unknown;
    wchar_t lastToneKey = 0;
    int sameToneKeyCount = 0;

    constexpr void Reset() noexcept {
        bias = LanguageBias::Unknown;
        lastToneKey = 0;
        sameToneKeyCount = 0;
    }
};

// =============================================================================
// TIER 1: Hard English Pattern Detection (O(1))
// =============================================================================

/// Check if two starting consonants form an impossible Vietnamese cluster.
/// Vietnamese has: ch, gh, gi, kh, ng, ngh, nh, ph, qu, th, tr
/// All other 2-consonant starts are English-only.
[[nodiscard]] inline bool IsHardEnglishStart(wchar_t c0, wchar_t c1) noexcept {
    c0 = towlower(c0);
    c1 = towlower(c1);
    // Exhaustive list of impossible Vietnamese start clusters
    return (c0 == L'c' && c1 == L'l') ||
           (c0 == L'c' && c1 == L'r') ||
           (c0 == L'b' && c1 == L'r') ||
           (c0 == L'b' && c1 == L'l') ||
           (c0 == L'd' && c1 == L'r') ||
           (c0 == L'f' && c1 == L'r') ||
           (c0 == L'f' && c1 == L'l') ||
           (c0 == L'g' && c1 == L'r') ||
           (c0 == L'g' && c1 == L'l') ||
           (c0 == L'p' && c1 == L'r') ||
           (c0 == L'p' && c1 == L'l') ||
           (c0 == L's' && c1 == L'm') ||
           (c0 == L's' && c1 == L'n') ||
           (c0 == L's' && c1 == L'p') ||
           (c0 == L's' && c1 == L'w') ||
           (c0 == L's' && c1 == L't') ||
           (c0 == L's' && c1 == L'c') ||
           (c0 == L's' && c1 == L'k') ||
           (c0 == L's' && c1 == L'l') ||
           (c0 == L'w' && c1 == L'r');
}

/// Check if a consonant is impossible at the end of a Vietnamese word.
/// Vietnamese final consonants: c, ch, m, n, ng, nh, p, t
/// These NEVER end a Vietnamese word: x, r, z, f, b, d, g, h, k, l, q, s, v, w
[[nodiscard]] inline bool IsHardEnglishEnd(wchar_t c) noexcept {
    c = towlower(c);
    // Only check the most distinctive ones to avoid false positives during typing
    // (user might still type more chars). Focus on consonants that are also
    // tone keys in Telex (s, f, r, x, j) but never end Vietnamese words.
    return c == L'x' || c == L'r' || c == L'z' || c == L'f';
}

/// Q without U is impossible in Vietnamese
[[nodiscard]] inline bool IsQWithoutU(wchar_t c0, wchar_t c1) noexcept {
    return towlower(c0) == L'q' && towlower(c1) != L'u';
}

// =============================================================================
// TIER 2: Soft English Bias (y + vowel patterns)
// =============================================================================

/// Check if a y-initial sequence is a valid Vietnamese pattern.
/// Valid: yê + u/n/m/t (yêu, yên, yếu, yết...)
/// Template works with both Telex::CharState and Vni::CharState.
template<typename CharStateT>
[[nodiscard]] inline bool IsValidVietnameseYSequence(
        const CharStateT* states, size_t count) noexcept {
    if (count < 2) return false;
    if (towlower(states[0].base) != L'y') return false;

    // y + ê (e with circumflex) → valid start
    if (states[1].base == L'e' && states[1].HasModifier()) {
        if (count == 2) return true;  // Could still become yêu, yên...
        wchar_t c2 = towlower(states[2].base);
        return (c2 == L'u' || c2 == L'n' || c2 == L'm' || c2 == L't');
    }

    return false;
}

/// Check for soft English bias (y + vowel without valid VN continuation).
template<typename CharStateT>
[[nodiscard]] inline bool CheckSoftEnglishBias(
        const CharStateT* states, size_t count) noexcept {
    if (count < 2) return false;
    if (towlower(states[0].base) != L'y') return false;

    // y + a/e/o (ambiguous — could be English year/yes/you)
    wchar_t c1 = towlower(states[1].base);
    bool isAmbiguous = states[1].IsVowel() &&
                       (c1 == L'a' || c1 == L'o' || c1 == L'e');

    if (isAmbiguous && !IsValidVietnameseYSequence(states, count)) {
        return true;
    }
    return false;
}

// =============================================================================
// TIER 3: Same-Key Insistence
// =============================================================================

/// Update insistence tracking. Returns true if user has "insisted"
/// (pressed the same tone key twice consecutively).
inline bool UpdateToneInsistence(wchar_t toneKey,
                                 EnglishProtectionState& state) noexcept {
    if (toneKey == state.lastToneKey) {
        state.sameToneKeyCount++;
    } else {
        state.lastToneKey = toneKey;
        state.sameToneKeyCount = 1;
    }
    return state.sameToneKeyCount >= 2;
}

// =============================================================================
// Combined Bias Check — runs Tier 1 + Tier 2
// =============================================================================

/// Analyze states and update English protection bias.
/// Call after each PushChar() that adds a new state.
/// Template works with both Telex::CharState and Vni::CharState.
template<typename CharStateT>
inline void CheckEnglishBias(const CharStateT* states, size_t count,
                             EnglishProtectionState& prot) noexcept {
    // Skip if already determined as Vietnamese (modifier/tone was applied)
    if (prot.bias == LanguageBias::Vietnamese) return;

    // TIER 1: Hard reject — check start clusters
    if (count >= 2 && !states[0].IsVowel() && !states[1].IsVowel() &&
        !states[0].IsD()) {
        if (IsHardEnglishStart(states[0].base, states[1].base) ||
            IsQWithoutU(states[0].base, states[1].base)) {
            prot.bias = LanguageBias::HardEnglish;
            return;
        }
    }

    // TIER 1: Hard reject — check end consonants (only at 3+ chars, after a vowel)
    if (count >= 3) {
        const auto& last = states[count - 1];
        if (!last.IsVowel() && !last.IsD()) {
            // Only flag as hard English if previous char was a vowel
            // (consonant after vowel = potential word ending)
            const auto& prev = states[count - 2];
            if (prev.IsVowel() && IsHardEnglishEnd(last.base)) {
                prot.bias = LanguageBias::HardEnglish;
                return;
            }
        }
    }

    // TIER 2: Soft bias — y + vowel
    if (prot.bias == LanguageBias::Unknown && CheckSoftEnglishBias(states, count)) {
        prot.bias = LanguageBias::SoftEnglish;
    }
}

}  // namespace NextKey
