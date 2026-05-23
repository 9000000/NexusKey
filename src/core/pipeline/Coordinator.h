// src/core/pipeline/Coordinator.h
//
// The coordinator. Owns the feature registry per Stage + the gate registry.
// HandleKey is the per-keystroke entry: evaluates gates, dispatches features
// in (stage, priority) order, filtered by GateMask. Stops a stage on Handled,
// stops all stages on Veto.
//
// Wave 1: Coordinator is linked but not called from HookEngine — registry stays
// empty in production paths. Tests construct Coordinator instances directly.
#pragma once

#include <array>
#include <memory>
#include <vector>
#include "core/pipeline/Stage.h"
#include "core/pipeline/Result.h"
#include "core/pipeline/GateMask.h"
#include "core/pipeline/IFeature.h"
#include "core/pipeline/IGate.h"
#include "core/pipeline/KeyContext.h"
#include "core/pipeline/IntentSink.h"

namespace NextKey::Pipeline {

class Coordinator {
public:
    Coordinator();
    ~Coordinator();

    Coordinator(const Coordinator&)            = delete;
    Coordinator& operator=(const Coordinator&) = delete;

    // Take ownership; coordinator sorts features by Priority() at Register time.
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

}  // namespace NextKey::Pipeline
