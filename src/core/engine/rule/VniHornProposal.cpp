// src/core/engine/rule/VniHornProposal.cpp
#include "core/engine/rule/VniHornProposal.h"

namespace NextKey::EngineRule {

RelocationKind VniHornProposal::relocationKind() const noexcept {
    return RelocationKind::HornVowel;
}

bool VniHornProposal::tryApply(TypingAction action, wchar_t keyChar) {
    return exec_.HandleVniHorn(action, keyChar);
}

}  // namespace NextKey::EngineRule
