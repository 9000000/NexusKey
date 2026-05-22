// src/core/brain/IGate.h
//
// Gate predicate — Brain queries every registered gate once per keystroke,
// builds the cumulative GateMask of *raised* gates, then filters features
// by Requires() vs raised mask. Features never check gates inside Try().

#pragma once

#include "core/brain/GateMask.h"
#include "core/brain/KeyContext.h"

namespace NextKey::Brain {

class IGate {
public:
    virtual ~IGate() = default;
    [[nodiscard]] virtual GateId Id()                           const noexcept = 0;
    [[nodiscard]] virtual bool   IsRaised(const KeyContext&)    const noexcept = 0;
};

}  // namespace NextKey::Brain
