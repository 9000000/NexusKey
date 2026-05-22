// src/core/brain/gates/SpellCheckGate.h
//
// Wave 1 shell — always unraised. Wave 2 wires this to TypingEngine's
// spellCheckDisabled_ flag.

#pragma once

#include "core/brain/IGate.h"

namespace NextKey::Brain {

class SpellCheckGate final : public IGate {
public:
    [[nodiscard]] GateId Id() const noexcept override { return GateId::SpellCheck; }
    [[nodiscard]] bool   IsRaised(const KeyContext&) const noexcept override { return false; }
};

}  // namespace NextKey::Brain
