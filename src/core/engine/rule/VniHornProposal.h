// src/core/engine/rule/VniHornProposal.h
//
// W8.5 — wraps TypingEngine::HandleVniHorn (VNI `7` modifier → ơ/ư).
// Body handles VNI 7 with uo-pair and uu-pattern logic, then falls
// through to ProcessVniVowelModifier(Horn, key) for the generic case.
//
// Metadata: relocationKind() == TargetTone. Every apply path in
// HandleVniHorn (uo, uu, generic) calls RelocateToneToTarget — NOT
// RelocateToneToHornVowel. Despite the surface similarity to Telex
// HornW (W8.2), the relocate function differs: Telex Horn uses
// RelocateToneToHornVowel while VNI Horn uses RelocateToneToTarget.
// Initial v3 commit mis-declared HornVowel; corrected post-W8 review.
#pragma once

#include "core/engine/rule/IModifierSubExecutor.h"
#include "core/engine/rule/ModifierProposal.h"

namespace NextKey::EngineRule {

class VniHornProposal final : public ModifierProposal {
public:
    explicit VniHornProposal(IModifierSubExecutor& exec) noexcept
        : exec_(exec) {}

    [[nodiscard]] RelocationKind relocationKind() const noexcept override;
    [[nodiscard]] bool tryApply(TypingAction action, wchar_t keyChar) override;

private:
    IModifierSubExecutor& exec_;
};

}  // namespace NextKey::EngineRule
