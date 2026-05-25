// src/core/engine/rule/VniBreveProposal.cpp
#include "core/engine/rule/VniBreveProposal.h"

namespace NextKey::EngineRule {

RelocationKind VniBreveProposal::relocationKind() const noexcept {
    return RelocationKind::TargetTone;
}

bool VniBreveProposal::tryApply(TypingAction action, wchar_t keyChar) {
    return exec_.HandleVniBreve(action, keyChar);
}

}  // namespace NextKey::EngineRule
