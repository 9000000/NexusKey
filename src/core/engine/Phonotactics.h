// NexusKey - Vietnamese Phonotactics Implementation
// Copyright (c) 2024-2026 PhatMT. All rights reserved.
// SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-NexusKey-Commercial
//
// Concrete implementation of IPhonotactics encoding RuleTiengViet rules.
// Reuses VietnameseTables.h tables (kDiphthongClassic/Modern, IsTriphthong)
// where applicable; extends with N1/N2/N3 vowel-coda compatibility,
// closed/pending vowel sets, c/k/qu onset agreement.

#pragma once

#include "IPhonotactics.h"
#include "SpellChecker.h"

namespace NextKey {
namespace Phonology {

/// Tri-state result of structural syllable validation. Currently aliased to
/// SpellCheck::Result; the underlying enum lives in SpellChecker.h for now
/// and will be folded into this namespace when G-2.3.B moves the validator
/// rules out of SpellChecker.cpp into Phonotactics.cpp.
using SyllableState = SpellCheck::Result;

/// Validate a sequence of CharState as a Vietnamese syllable. Returns
/// SyllableState::Valid for a complete syllable, ValidPrefix for a prefix
/// that can still extend, Invalid otherwise. Currently delegates to
/// SpellCheck::Validate; production callers should route through this entry
/// point so the future rule migration is transparent.
template<typename CharStateT>
[[nodiscard]] inline SyllableState ValidateSyllableState(
        const CharStateT* states, size_t count, bool allowZwjf = false) noexcept {
    return SpellCheck::Validate(states, count, allowZwjf);
}

class Phonotactics final : public IPhonotactics {
public:
    Phonotactics() noexcept = default;
    ~Phonotactics() override = default;

    Phonotactics(const Phonotactics&) = delete;
    Phonotactics& operator=(const Phonotactics&) = delete;

    /// Returns the process-wide default Phonotactics instance. Stateless and
    /// thread-safe; used as the implicit dependency for callers that don't
    /// inject a custom IPhonotactics (e.g. TypingEngine's single-arg ctor).
    [[nodiscard]] static const Phonotactics& Default() noexcept;

    [[nodiscard]] size_t TonePosition(
        std::wstring_view vowelSeq,
        std::wstring_view coda,
        bool modernOrtho) const noexcept override;

    [[nodiscard]] bool IsValidSyllable(
        std::wstring_view onset,
        std::wstring_view vowelSeq,
        std::wstring_view coda,
        Tone tone,
        bool modernOrtho) const noexcept override;

    [[nodiscard]] bool CanComplete(
        std::wstring_view partial) const noexcept override;
};

}  // namespace Phonology
}  // namespace NextKey
