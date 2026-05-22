// src/core/brain/IFeature.h
//
// The plugin interface. Each feature declares its stage, priority, and the
// gate-mask it requires to be unblocked. Brain dispatches features in
// (stage, priority) order, skipping any with at least one required gate
// raised. Result tells Brain whether to continue, stop this stage, or
// veto remaining stages.
#pragma once

#include "core/brain/Stage.h"
#include "core/brain/Result.h"
#include "core/brain/GateMask.h"
#include "core/brain/KeyContext.h"
#include "core/brain/IntentSink.h"

namespace NextKey::Brain {

class IFeature {
public:
    virtual ~IFeature() = default;

    // Static metadata — must return the same value for the lifetime of the
    // feature instance. Brain caches these at Register() time.
    [[nodiscard]] virtual Stage    FeatureStage() const noexcept = 0;
    [[nodiscard]] virtual int      Priority()     const noexcept = 0;
    [[nodiscard]] virtual GateMask Requires()     const noexcept = 0;

    // Per-keystroke entry point. May emit zero or more intents via sink.
    // Brain calls Try only when this feature's `Requires()` is satisfied
    // by the current gates evaluation.
    [[nodiscard]] virtual Result Try(const KeyContext& ctx, IntentSink& sink) = 0;
};

}  // namespace NextKey::Brain
