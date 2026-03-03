// NexusKey - Vietnamese Spell Checker (Syllable Structure Validation)
// SPDX-License-Identifier: GPL-3.0-only
//
// Validates that a character buffer forms a valid Vietnamese syllable:
//   [C₁] + V + [C₂]
// where C₁ is initial consonant, V is vowel nucleus, C₂ is final consonant.
//
// Template API works with both Telex::CharState and Vni::CharState.

#pragma once

#include <cstdint>
#include <cstddef>

namespace NextKey {
namespace SpellCheck {

/// Result of syllable validation
enum class Result : uint8_t {
    Valid,        // Complete valid syllable
    ValidPrefix,  // Could become valid with more characters
    Invalid       // Cannot form a valid Vietnamese syllable
};

/// Validate a sequence of CharState objects as a Vietnamese syllable.
/// Template works with both Telex::CharState and Vni::CharState.
/// Both have: base (wchar_t), mod (enum with None/Circumflex/Breve/Horn),
///            IsVowel(), IsD(), tone (enum with None/Acute/Grave/Hook/Tilde/Dot)
/// @param allowZwjf When true, accept z/j/w/f as valid initial consonants
///                  (z/j≡gi, w≡qu, f≡ph)
template<typename CharStateT>
Result Validate(const CharStateT* states, size_t count, bool allowZwjf = false) noexcept;

}  // namespace SpellCheck
}  // namespace NextKey
