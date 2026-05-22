// src/core/brain/Brain.h
//
// The coordinator. Owns the feature registry per Stage + the gate registry.
// HandleKey is the per-keystroke entry: evaluates gates, dispatches features
// in (stage, priority) order, filtered by GateMask. Stops a stage on Handled,
// stops all stages on Veto.
//
// Wave 1: Brain is linked but not called from HookEngine — registry stays
// empty in production paths. Tests construct Brain instances directly.
#pragma once

#include <array>
#include <memory>
#include <vector>
#include "core/brain/Stage.h"
#include "core/brain/Result.h"
#include "core/brain/GateMask.h"
#include "core/brain/IFeature.h"
#include "core/brain/IGate.h"
#include "core/brain/KeyContext.h"
#include "core/brain/IntentSink.h"

namespace NextKey::Brain {

class Brain {
public:
    Brain();
    ~Brain();

    Brain(const Brain&)            = delete;
    Brain& operator=(const Brain&) = delete;

    // Take ownership; brain sorts features by Priority() at Register time.
    void Register(std::unique_ptr<IFeature> feature);
    void RegisterGate(std::unique_ptr<IGate> gate);

    // Per-keystroke dispatch.
    void HandleKey(const KeyContext& ctx, IntentSink& sink);

    // Test introspection.
    [[nodiscard]] std::size_t FeatureCountAtStage(Stage s) const noexcept;
    [[nodiscard]] std::size_t GateCount() const noexcept;

private:
    GateMask EvaluateGates(const KeyContext& ctx) const;

    std::array<std::vector<std::unique_ptr<IFeature>>, kStageCount> features_;
    std::vector<std::unique_ptr<IGate>>                              gates_;
};

}  // namespace NextKey::Brain
