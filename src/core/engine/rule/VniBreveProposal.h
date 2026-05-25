// src/core/engine/rule/VniBreveProposal.h
//
// W8.5 — wraps TypingEngine::HandleVniBreve (VNI `8` modifier → ă).
// Body forwards to ProcessVniVowelModifier(Breve, key).
//
// Metadata: relocationKind() == TargetTone (shared backing calls
// RelocateToneToTarget when needsRelocate; W8.5 ValidPrefix fix).
#pragma once

#include "core/engine/rule/IModifierSubExecutor.h"
#include "core/engine/rule/ModifierProposal.h"

namespace NextKey::EngineRule {

class VniBreveProposal final : public ModifierProposal {
public:
    explicit VniBreveProposal(IModifierSubExecutor& exec) noexcept
        : exec_(exec) {}

    [[nodiscard]] RelocationKind relocationKind() const noexcept override;
    [[nodiscard]] bool tryApply(TypingAction action, wchar_t keyChar) override;

private:
    IModifierSubExecutor& exec_;
};

}  // namespace NextKey::EngineRule
