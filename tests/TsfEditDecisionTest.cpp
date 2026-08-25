// VKey - TSF edit decision regression tests
// SPDX-License-Identifier: GPL-3.0-only

#include <gtest/gtest.h>

#include <cstdint>

#include "core/TsfEditDecision.h"

namespace NextKey {
namespace {

TEST(TsfEditDecision, ReadOnlyDocumentBlocksComposition) {
    EXPECT_TRUE(IsReadOnlyTsfDocument(kTsfReadOnlyDocumentFlag));
    EXPECT_TRUE(IsReadOnlyTsfDocument(kTsfReadOnlyDocumentFlag | 0x2u));
}

TEST(TsfEditDecision, WritableOrLoadingDocumentAllowsComposition) {
    EXPECT_FALSE(IsReadOnlyTsfDocument(/*dynamicStatusFlags=*/0));
    EXPECT_FALSE(IsReadOnlyTsfDocument(/*loadingFlag=*/0x2u));
}

// Static-flag values observed in the #242 logs and in the text stores of the
// hosts VKey has to keep working (Chromium, Firefox, WPF).
TEST(TsfEditDecision, TransitoryOnlyDocumentBlocksComposition) {
    EXPECT_TRUE(IsTransitoryOnlyTsfDocument(/*explorerListView=*/0x4u));
    EXPECT_TRUE(IsTransitoryOnlyTsfDocument(/*oneCommanderWindow=*/0x4u));
}

TEST(TsfEditDecision, RealTextStoresAllowComposition) {
    EXPECT_FALSE(IsTransitoryOnlyTsfDocument(/*chromium=*/0x4u | 0x8u));
    EXPECT_FALSE(IsTransitoryOnlyTsfDocument(/*firefox=*/0x8u));
    EXPECT_FALSE(IsTransitoryOnlyTsfDocument(/*wpf=*/0x2u));
    EXPECT_FALSE(IsTransitoryOnlyTsfDocument(/*noStaticFlags=*/0));
}

TEST(TsfEditDecision, ExactBackwardShiftAcceptsWholeWordRange) {
    EXPECT_TRUE(IsExactBackwardRangeShift(/*requestedChars=*/5, /*shiftedChars=*/-5));
}

TEST(TsfEditDecision, PartialOrForwardShiftRejectsReviveRange) {
    EXPECT_FALSE(IsExactBackwardRangeShift(/*requestedChars=*/5, /*shiftedChars=*/-4));
    EXPECT_FALSE(IsExactBackwardRangeShift(/*requestedChars=*/5, /*shiftedChars=*/0));
    EXPECT_FALSE(IsExactBackwardRangeShift(/*requestedChars=*/5, /*shiftedChars=*/5));
    EXPECT_FALSE(IsExactBackwardRangeShift(/*requestedChars=*/0, /*shiftedChars=*/0));
}

TEST(TsfEditDecision, DuplicateInitialKeyDownIsSuppressed) {
    constexpr uint32_t vkSpace = 0x20;
    constexpr uint32_t initialKeyDown = 0;
    EXPECT_TRUE(ShouldSuppressClaimedKeyDown(vkSpace, vkSpace, initialKeyDown));
}

TEST(TsfEditDecision, AutoRepeatAndDifferentKeysAreNotSuppressed) {
    constexpr uint32_t vkSpace = 0x20;
    constexpr uint32_t vkA = 0x41;
    constexpr uint32_t previousKeyState = 1u << 30;

    EXPECT_FALSE(ShouldSuppressClaimedKeyDown(vkSpace, vkSpace, previousKeyState));
    EXPECT_FALSE(ShouldSuppressClaimedKeyDown(vkSpace, vkA, /*lParam=*/0));
    EXPECT_FALSE(ShouldSuppressClaimedKeyDown(/*pendingVk=*/0, vkSpace, /*lParam=*/0));
}

TEST(TsfEditDecision, PreeditAttributeRequiresExplicitOptIn) {
    EXPECT_FALSE(ShouldApplyPreeditDisplayAttribute(false));
    EXPECT_TRUE(ShouldApplyPreeditDisplayAttribute(true));
}

}  // namespace
}  // namespace NextKey
