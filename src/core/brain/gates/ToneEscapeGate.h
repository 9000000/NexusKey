// src/core/brain/gates/ToneEscapeGate.h
//
// Wave 1 shell — always unraised. Wave 2 wires this to TypingEngine's
// toneEscaped_ flag (set by all modifier/tone escape paths).

#pragma once

#include "core/brain/IGate.h"

namespace NextKey::Brain {

class ToneEscapeGate final : public IGate {
public:
    [[nodiscard]] GateId Id() const noexcept override { return GateId::ToneEscape; }
    [[nodiscard]] bool   IsRaised(const KeyContext&) const noexcept override { return false; }
};

}  // namespace NextKey::Brain
