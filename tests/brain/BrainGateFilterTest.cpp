// tests/brain/BrainGateFilterTest.cpp
#include <gtest/gtest.h>
#include <vector>
#include <string>
#include "core/brain/Brain.h"
#include "core/brain/IFeature.h"
#include "core/brain/IGate.h"
#include "core/brain/ICompositionSession.h"

using namespace NextKey::Brain;

namespace {

class TaggingFeature final : public IFeature {
public:
    TaggingFeature(std::string tag, GateMask requires_mask, std::vector<std::string>& log)
        : tag_{std::move(tag)}, requires_{requires_mask}, log_{log} {}
    [[nodiscard]] Stage    FeatureStage() const noexcept override { return Stage::Engine; }
    [[nodiscard]] int      Priority()     const noexcept override { return 10; }
    [[nodiscard]] GateMask Requires()     const noexcept override { return requires_; }
    [[nodiscard]] Result   Try(const KeyContext&, IntentSink&) override {
        log_.push_back(tag_);
        return Result::Pass;
    }
private:
    std::string tag_;
    GateMask    requires_;
    std::vector<std::string>& log_;
};

class FixedGate final : public IGate {
public:
    FixedGate(GateId id, bool raised) : id_{id}, raised_{raised} {}
    [[nodiscard]] GateId Id() const noexcept override { return id_; }
    [[nodiscard]] bool   IsRaised(const KeyContext&) const noexcept override { return raised_; }
private:
    GateId id_;
    bool   raised_;
};

class FakeSession final : public ICompositionSession {
public:
    [[nodiscard]] std::wstring_view PreviousRendered() const noexcept override { return {}; }
    [[nodiscard]] std::wstring_view EngineRendered()   const noexcept override { return {}; }
    [[nodiscard]] std::wstring_view RawInput()         const noexcept override { return {}; }
};

class NullSink final : public IntentSink {
public:
    void Emit(Intent) override {}
};

KeyContext makeCtx(const ICompositionSession& session) {
    return KeyContext{
        .vk = 0x41, .keyChar = L'a',
        .shift = false, .capsLock = false, .ctrl = false, .alt = false, .win = false,
        .session = session, .reinjectVk = 0,
    };
}

}  // namespace

TEST(BrainGateFilter, FeatureWithRequiresZeroAlwaysRuns) {
    std::vector<std::string> log;
    Brain brain;
    brain.RegisterGate(std::make_unique<FixedGate>(GateId::EnglishBias, /*raised=*/true));
    brain.Register(std::make_unique<TaggingFeature>("always", /*requires=*/0u, log));

    FakeSession session;
    NullSink sink;
    brain.HandleKey(makeCtx(session), sink);

    ASSERT_EQ(log.size(), 1u);
    EXPECT_EQ(log[0], "always");
}

TEST(BrainGateFilter, FeatureRequiringRaisedGateIsSkipped) {
    std::vector<std::string> log;
    Brain brain;
    brain.RegisterGate(std::make_unique<FixedGate>(GateId::EnglishBias, /*raised=*/true));
    brain.Register(std::make_unique<TaggingFeature>("blocked", GateMaskFor(GateId::EnglishBias), log));

    FakeSession session;
    NullSink sink;
    brain.HandleKey(makeCtx(session), sink);

    EXPECT_EQ(log.size(), 0u);
}

TEST(BrainGateFilter, FeatureRequiringUnraisedGateRuns) {
    std::vector<std::string> log;
    Brain brain;
    brain.RegisterGate(std::make_unique<FixedGate>(GateId::EnglishBias, /*raised=*/false));
    brain.Register(std::make_unique<TaggingFeature>("ok", GateMaskFor(GateId::EnglishBias), log));

    FakeSession session;
    NullSink sink;
    brain.HandleKey(makeCtx(session), sink);

    ASSERT_EQ(log.size(), 1u);
    EXPECT_EQ(log[0], "ok");
}
