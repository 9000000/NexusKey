// VKey - bounded Dorion delayed hook-reclaim state tests
// SPDX-License-Identifier: GPL-3.0-only

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>

#include "core/DorionHookReclaimSequence.h"

namespace NextKey {
namespace {

TEST(DorionHookReclaimSequenceTest, ArmsConcurrentIdentitiesAndFiresAtDeadline) {
    DorionDelayedReclaimSlots slots;
    constexpr DorionProcessIdentity first{41, 101};
    constexpr DorionProcessIdentity second{42, 102};

    EXPECT_TRUE(slots.Arm({first, 7, 1800, 30}));
    EXPECT_TRUE(slots.Arm({second, 8, 2200, 30}));
    EXPECT_TRUE(slots.HasTimer(7));
    EXPECT_FALSE(slots.HasTimer(99));
    EXPECT_FALSE(slots.TakeIfDue(7, 1799).has_value());

    const auto taken = slots.TakeIfDue(7, 1800);
    ASSERT_TRUE(taken.has_value());
    EXPECT_EQ(taken->identity, first);
    EXPECT_EQ(taken->remainingGateChecks, 30u);
    EXPECT_TRUE(slots.HasPending(second));
}

TEST(DorionHookReclaimSequenceTest, RejectsDuplicateIdentityOrLiveTimer) {
    DorionDelayedReclaimSlots slots;
    constexpr DorionProcessIdentity first{41, 101};
    constexpr DorionProcessIdentity second{42, 102};
    ASSERT_TRUE(slots.Arm({first, 7, 1800, 30}));

    EXPECT_FALSE(slots.Arm({first, 8, 2200, 30}));
    EXPECT_FALSE(slots.Arm({second, 7, 2200, 30}));
}

TEST(DorionHookReclaimSequenceTest, InvalidIdentityNeverMatchesEmptyStorage) {
    DorionDelayedReclaimSlots slots;

    EXPECT_FALSE(slots.HasPending({}));
    EXPECT_FALSE(slots.Cancel({}).has_value());
}

TEST(DorionHookReclaimSequenceTest, ReusedTimerCannotFireBeforeNewDeadline) {
    DorionDelayedReclaimSlots slots;
    constexpr DorionProcessIdentity first{41, 101};
    constexpr DorionProcessIdentity second{42, 102};
    ASSERT_TRUE(slots.Arm({first, 7, 1800, 30}));
    ASSERT_TRUE(slots.Cancel(first).has_value());
    ASSERT_TRUE(slots.Arm({second, 7, 3600, 30}));

    EXPECT_FALSE(slots.TakeIfDue(7, 1800).has_value());
    const auto taken = slots.TakeIfDue(7, 3600);
    ASSERT_TRUE(taken.has_value());
    EXPECT_EQ(taken->identity, second);
}

TEST(DorionHookReclaimSequenceTest, CapacityIsBoundedWithoutAllocation) {
    DorionDelayedReclaimSlots slots;
    for (std::size_t i = 0; i < DorionDelayedReclaimSlots::kCapacity; ++i) {
        EXPECT_TRUE(slots.Arm({
            {static_cast<std::uint32_t>(40 + i),
             static_cast<std::uint32_t>(100 + i)},
            static_cast<std::uintptr_t>(7 + i), 1800, 30}));
    }
    EXPECT_FALSE(slots.Arm({{99, 199}, 99, 1800, 30}));
}

TEST(DorionHookReclaimSequenceTest, ActiveTimerSnapshotSupportsCleanShutdown) {
    DorionDelayedReclaimSlots slots;
    ASSERT_TRUE(slots.Arm({{41, 101}, 7, 1800, 30}));
    ASSERT_TRUE(slots.Arm({{42, 102}, 8, 2200, 30}));

    const auto ids = slots.ActiveTimerIds();
    EXPECT_EQ(ids[0], 7u);
    EXPECT_EQ(ids[1], 8u);
    slots.Clear();
    EXPECT_FALSE(slots.HasAnyPending());
}

TEST(DorionHookReclaimSequenceTest, QuietGateRequiresRealIdleAndEmptyState) {
    const DorionReclaimQuietEvidence quiet{
        .lastInputQuerySucceeded = true,
        .nowTickMs = 1000,
        .lastInputTickMs = 900,
    };
    EXPECT_TRUE(IsDorionReclaimQuiet(quiet));

    auto recent = quiet;
    recent.lastInputTickMs = 901;
    EXPECT_FALSE(IsDorionReclaimQuiet(recent));

    auto keyDown = quiet;
    keyDown.anyPhysicalKeyDown = true;
    EXPECT_FALSE(IsDorionReclaimQuiet(keyDown));

    auto synthetic = quiet;
    synthetic.syntheticDispatchActive = true;
    EXPECT_FALSE(IsDorionReclaimQuiet(synthetic));

    auto composition = quiet;
    composition.compositionActive = true;
    EXPECT_FALSE(IsDorionReclaimQuiet(composition));

    auto failedQuery = quiet;
    failedQuery.lastInputQuerySucceeded = false;
    EXPECT_FALSE(IsDorionReclaimQuiet(failedQuery));
}

TEST(DorionHookReclaimSequenceTest, QuietGateHandlesTickWrapAndFutureAnomaly) {
    const DorionReclaimQuietEvidence wrapped{
        .lastInputQuerySucceeded = true,
        .nowTickMs = 50,
        .lastInputTickMs = 0xFFFFFFB0u,
    };
    EXPECT_TRUE(IsDorionReclaimQuiet(wrapped));

    auto anomalousFuture = wrapped;
    anomalousFuture.nowTickMs = 1000;
    anomalousFuture.lastInputTickMs = 1001;
    EXPECT_FALSE(IsDorionReclaimQuiet(anomalousFuture));
}

}  // namespace
}  // namespace NextKey
