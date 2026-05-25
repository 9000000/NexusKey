// src/core/engine/rule/VniCircumflexProposal.h
//
// W8.5 — wraps TypingEngine::HandleVniCircumflex (VNI `6` modifier →
// â/ê/ô). Body forwards to ProcessVniVowelModifier(Circumflex, key).
//
// Metadata: relocationKind() == TargetTone. The shared backing
// ProcessVniVowelModifier calls RelocateToneToTarget after applying
// the modifier (post-W8.5: only when needsRelocate, mirroring the
// ad09f15 + c6369dd invariant from HandleAdjacentCircumflex).
#pragma once

#include "core/engine/rule/IModifierSubExecutor.h"
#include "core/engine/rule/ModifierProposal.h"

namespace NextKey::EngineRule {

class VniCircumflexProposal final : public ModifierProposal {
public:
    explicit VniCircumflexProposal(IModifierSubExecutor& exec) noexcept
        : exec_(exec) {}

    [[nodiscard]] RelocationKind relocationKind() const noexcept override;
    [[nodiscard]] bool tryApply(TypingAction action, wchar_t keyChar) override;

private:
    IModifierSubExecutor& exec_;
};

}  // namespace NextKey::EngineRule
