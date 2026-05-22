// src/core/brain/gates/EnglishBiasGate.h
//
// Wave 1 shell — always unraised. Wave 2 wires this to the engine's
// engProt_.bias == LanguageBias::HardEnglish detection.

#pragma once

#include "core/brain/IGate.h"

namespace NextKey::Brain {

class EnglishBiasGate final : public IGate {
public:
    [[nodiscard]] GateId Id() const noexcept override { return GateId::EnglishBias; }
    [[nodiscard]] bool   IsRaised(const KeyContext&) const noexcept override { return false; }
};

}  // namespace NextKey::Brain
