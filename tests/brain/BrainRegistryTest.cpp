// tests/brain/BrainRegistryTest.cpp
#include <gtest/gtest.h>
#include "core/brain/Brain.h"
#include "core/brain/IFeature.h"

using namespace NextKey::Brain;

namespace {

class NoOpFeature final : public IFeature {
public:
    NoOpFeature(Stage s, int p) : stage_{s}, prio_{p} {}
    [[nodiscard]] Stage    FeatureStage() const noexcept override { return stage_; }
    [[nodiscard]] int      Priority()     const noexcept override { return prio_; }
    [[nodiscard]] GateMask Requires()     const noexcept override { return 0u; }
    [[nodiscard]] Result   Try(const KeyContext&, IntentSink&) override { return Result::Pass; }
private:
    Stage stage_;
    int   prio_;
};

}  // namespace

TEST(BrainRegistry, EmptyRegistryReturnsZeroFeaturesAtEveryStage) {
    Brain brain;
    EXPECT_EQ(brain.FeatureCountAtStage(Stage::PreEngine),  0u);
    EXPECT_EQ(brain.FeatureCountAtStage(Stage::Engine),     0u);
    EXPECT_EQ(brain.FeatureCountAtStage(Stage::PostEngine), 0u);
}

TEST(BrainRegistry, RegisterPutsFeatureInDeclaredStage) {
    Brain brain;
    auto f = std::make_unique<NoOpFeature>(Stage::Engine, 10);
    brain.Register(std::move(f));
    EXPECT_EQ(brain.FeatureCountAtStage(Stage::PreEngine),  0u);
    EXPECT_EQ(brain.FeatureCountAtStage(Stage::Engine),     1u);
    EXPECT_EQ(brain.FeatureCountAtStage(Stage::PostEngine), 0u);
}

TEST(BrainRegistry, MultipleFeaturesAtSameStageKeepsAll) {
    Brain brain;
    brain.Register(std::make_unique<NoOpFeature>(Stage::PreEngine, 10));
    brain.Register(std::make_unique<NoOpFeature>(Stage::PreEngine, 20));
    brain.Register(std::make_unique<NoOpFeature>(Stage::PreEngine, 5));
    EXPECT_EQ(brain.FeatureCountAtStage(Stage::PreEngine), 3u);
}
