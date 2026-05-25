// src/core/engine/rule/VniHornProposal.cpp
#include "core/engine/rule/VniHornProposal.h"

namespace NextKey::EngineRule {

RelocationKind VniHornProposal::relocationKind() const noexcept {
    // TargetTone — HandleVniHorn calls RelocateToneToTarget() on every
    // apply path (uo pair, uu pattern, generic fallback via
    // ProcessVniVowelModifier). It does NOT call RelocateToneToHornVowel
    // despite the surface similarity to Telex HornW.
    return RelocationKind::TargetTone;
}

bool VniHornProposal::tryApply(TypingAction action, wchar_t keyChar) {
    return exec_.HandleVniHorn(action, keyChar);
}

}  // namespace NextKey::EngineRule
