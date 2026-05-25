// src/core/engine/rule/VniCircumflexProposal.cpp
#include "core/engine/rule/VniCircumflexProposal.h"

namespace NextKey::EngineRule {

RelocationKind VniCircumflexProposal::relocationKind() const noexcept {
    return RelocationKind::TargetTone;
}

bool VniCircumflexProposal::tryApply(TypingAction action, wchar_t keyChar) {
    return exec_.HandleVniCircumflex(action, keyChar);
}

}  // namespace NextKey::EngineRule
