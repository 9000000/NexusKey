// VKey - TSF edit decision regression tests
// SPDX-License-Identifier: AGPL-3.0-only

#include <gtest/gtest.h>

#include <cstdint>
#include <limits>

#include "core/TsfEditDecision.h"

namespace NextKey {
namespace {

TEST(TsfEditDecision, ExactBackwardShiftAcceptsWholeWordRange) {
    EXPECT_TRUE(IsExactBackwardRangeShift(/*requestedChars=*/5, /*shiftedChars=*/-5));
}

TEST(TsfEditDecision, PartialOrForwardShiftRejectsReviveRange) {
    EXPECT_FALSE(IsExactBackwardRangeShift(/*requestedChars=*/5, /*shiftedChars=*/-4));
    EXPECT_FALSE(IsExactBackwardRangeShift(/*requestedChars=*/5, /*shiftedChars=*/0));
    EXPECT_FALSE(IsExactBackwardRangeShift(/*requestedChars=*/5, /*shiftedChars=*/5));
    EXPECT_FALSE(IsExactBackwardRangeShift(
        static_cast<std::size_t>((std::numeric_limits<std::int32_t>::max)()) + 1,
        (std::numeric_limits<std::int32_t>::min)()));
}

TEST(TsfEditDecision, DuplicateInitialPrintableKeyDownIsSuppressed) {
    constexpr uint32_t vkSpace = 0x20;
    constexpr uint32_t initialKeyDown = 0;
    EXPECT_TRUE(ShouldSuppressClaimedPrintableKeyDown(
        vkSpace, vkSpace, initialKeyDown));
}

TEST(TsfEditDecision, AutoRepeatAndDifferentKeysAreNotSuppressed) {
    constexpr uint32_t vkSpace = 0x20;
    constexpr uint32_t vkA = 0x41;
    constexpr uint32_t previousKeyState = 1u << 30;

    EXPECT_FALSE(ShouldSuppressClaimedPrintableKeyDown(
        vkSpace, vkSpace, previousKeyState));
    EXPECT_FALSE(ShouldSuppressClaimedPrintableKeyDown(
        vkSpace, vkA, /*lParam=*/0));
    EXPECT_FALSE(ShouldSuppressClaimedPrintableKeyDown(
        /*pendingVk=*/0, vkSpace, /*lParam=*/0));
}

}  // namespace
}  // namespace NextKey
