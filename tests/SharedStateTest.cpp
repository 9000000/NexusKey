// NexusKey - SharedState Unit Tests
// SPDX-License-Identifier: GPL-3.0-only

#include <gtest/gtest.h>
#include "core/ipc/SharedState.h"
#include "core/config/TypingConfig.h"

namespace NextKey {
namespace {

class SharedStateTest : public ::testing::Test {};

// ============================================================================
// SharedState Struct Tests
// ============================================================================

TEST_F(SharedStateTest, InitDefaults_SetsMagic) {
    SharedState state{};
    state.InitDefaults();

    EXPECT_EQ(state.magic, SharedState::MAGIC_VALUE);
    EXPECT_TRUE(state.IsValid());
}

TEST_F(SharedStateTest, InitDefaults_SetsDefaultValues) {
    SharedState state{};
    state.InitDefaults();

    EXPECT_EQ(state.epoch, 0u);
    EXPECT_EQ(state.inputMethod, 0);  // Telex
    EXPECT_EQ(state.spellCheck, 0);
    EXPECT_EQ(state.optimizeLevel, 0);
    EXPECT_EQ(state.featureFlags, FeatureFlags::ALLOW_ZWJF);  // Default: tone keys enabled
}

TEST_F(SharedStateTest, InitDefaults_SetsVersioning) {
    SharedState state{};
    state.InitDefaults();

    EXPECT_EQ(state.structVersion, SharedState::CURRENT_VERSION);
    EXPECT_EQ(state.structSize, sizeof(SharedState));
}

TEST_F(SharedStateTest, InitDefaults_SetsVietnameseMode) {
    SharedState state{};
    state.InitDefaults();

    // Default flags should have Vietnamese mode enabled
    EXPECT_TRUE(state.flags & SharedFlags::VIETNAMESE_MODE);
    EXPECT_TRUE(state.flags & SharedFlags::ENGINE_ENABLED);
}

TEST_F(SharedStateTest, IsValid_FalseForUninitializedState) {
    SharedState state{};  // Zero-initialized
    EXPECT_FALSE(state.IsValid());
}

TEST_F(SharedStateTest, IsValid_FalseForWrongMagic) {
    SharedState state{};
    state.magic = 0xDEADBEEF;  // Wrong magic
    EXPECT_FALSE(state.IsValid());
}

TEST_F(SharedStateTest, IsValid_ChecksVersionBound) {
    SharedState state{};
    state.InitDefaults();

    // Valid version
    EXPECT_TRUE(state.IsValid());

    // Future version (from newer app) — should be invalid
    state.structVersion = SharedState::CURRENT_VERSION + 1;
    EXPECT_FALSE(state.IsValid());
}

TEST_F(SharedStateTest, IsValid_ChecksMinimumStructSize) {
    SharedState state{};
    state.InitDefaults();
    EXPECT_TRUE(state.IsValid());

    // Too small
    state.structSize = 10;
    EXPECT_FALSE(state.IsValid());
}

// ============================================================================
// SharedState + TypingConfig Integration Tests
// ============================================================================

TEST_F(SharedStateTest, ConfigToSharedState_Telex) {
    TypingConfig config;
    config.inputMethod = InputMethod::Telex;
    config.spellCheckEnabled = false;
    config.optimizeLevel = 0;

    SharedState state{};
    state.InitDefaults();
    state.inputMethod = static_cast<uint8_t>(config.inputMethod);
    state.spellCheck = config.spellCheckEnabled ? 1 : 0;
    state.optimizeLevel = config.optimizeLevel;

    EXPECT_EQ(state.inputMethod, 0);  // Telex = 0
    EXPECT_EQ(state.spellCheck, 0);
    EXPECT_TRUE(state.IsValid());
}

TEST_F(SharedStateTest, ConfigToSharedState_VNI) {
    TypingConfig config;
    config.inputMethod = InputMethod::VNI;
    config.spellCheckEnabled = true;
    config.optimizeLevel = 2;

    SharedState state{};
    state.InitDefaults();
    state.inputMethod = static_cast<uint8_t>(config.inputMethod);
    state.spellCheck = config.spellCheckEnabled ? 1 : 0;
    state.optimizeLevel = config.optimizeLevel;

    EXPECT_EQ(state.inputMethod, 1);  // VNI = 1
    EXPECT_EQ(state.spellCheck, 1);
    EXPECT_EQ(state.optimizeLevel, 2);
    EXPECT_TRUE(state.IsValid());
}

TEST_F(SharedStateTest, SharedStateToConfig_Telex) {
    SharedState state{};
    state.InitDefaults();
    state.inputMethod = 0;  // Telex
    state.spellCheck = 0;
    state.optimizeLevel = 1;

    // Simulate reading config from SharedState
    InputMethod method = static_cast<InputMethod>(state.inputMethod);
    bool spellCheck = state.spellCheck != 0;
    uint8_t optLevel = state.optimizeLevel;

    EXPECT_EQ(method, InputMethod::Telex);
    EXPECT_FALSE(spellCheck);
    EXPECT_EQ(optLevel, 1);
}

TEST_F(SharedStateTest, SharedStateToConfig_VNI) {
    SharedState state{};
    state.InitDefaults();
    state.inputMethod = 1;  // VNI
    state.spellCheck = 1;
    state.optimizeLevel = 0;

    InputMethod method = static_cast<InputMethod>(state.inputMethod);
    bool spellCheck = state.spellCheck != 0;

    EXPECT_EQ(method, InputMethod::VNI);
    EXPECT_TRUE(spellCheck);
}

// ============================================================================
// Epoch Tests (Seqlock & Config Change Detection)
// ============================================================================

TEST_F(SharedStateTest, Epoch_StartsAtZero) {
    SharedState state{};
    state.InitDefaults();
    EXPECT_EQ(state.epoch, 0u);
}

TEST_F(SharedStateTest, Epoch_EvenMeansStable) {
    SharedState state{};
    state.InitDefaults();

    // Epoch 0 = even = stable
    EXPECT_EQ(state.epoch & 1, 0u);
}

TEST_F(SharedStateTest, Epoch_DetectsChange) {
    SharedState state{};
    state.InitDefaults();

    uint32_t lastEpoch = state.epoch;

    // Simulate Write() auto-increment: 0 → 1 (writing) → 2 (done)
    state.epoch += 2;

    EXPECT_NE(state.epoch, lastEpoch);
    EXPECT_EQ(state.epoch & 1, 0u);  // Still even after write
}

// ============================================================================
// Reserved Space Tests
// ============================================================================

TEST_F(SharedStateTest, ReservedSpace_ZeroInitialized) {
    SharedState state{};
    state.InitDefaults();

    for (const auto& b : state.reserved) {
        EXPECT_EQ(b, 0);
    }
}

}  // namespace
}  // namespace NextKey
