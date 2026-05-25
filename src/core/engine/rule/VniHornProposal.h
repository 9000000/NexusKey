// src/core/engine/rule/VniHornProposal.h
//
// W8.5 — wraps TypingEngine::HandleVniHorn (VNI `7` modifier → ơ/ư).
// Body implements priority order similar to HandleHornW but specific
// to the VNI 7 keystroke.
//
// Metadata: relocationKind() == HornVowel — apply path calls
// RelocateToneToHornVowel (matches HornModifierProposal W8.2).
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
