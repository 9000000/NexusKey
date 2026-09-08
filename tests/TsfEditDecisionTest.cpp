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

TEST(TsfEditDecision, WritableTransitoryContextAllowsTyping) {
    // Captured in Excel: these flags describe a usable short-lived context,
    // not a disabled keyboard. No host name or window class enters the policy.
    EXPECT_FALSE(ShouldBlockTsfContext({
        .hasContext = true,
        .dynamicStatusFlags = 0x80000000u,
        .staticStatusFlags = kTsfTransitoryDocumentFlag,
    }));
}

TEST(TsfEditDecision, StaticCapabilitiesDoNotDisableTyping) {
    // Chromium, Firefox, WPF and a host advertising no static capabilities.
    for (const auto flags : {0x4u | 0x8u, 0x8u, 0x2u, 0u}) {
        EXPECT_FALSE(ShouldBlockTsfContext({
            .hasContext = true, .staticStatusFlags = flags,
        }));
    }
}

TEST(TsfEditDecision, MissingContextDoesNotClaimKeys) {
    EXPECT_TRUE(ShouldBlockTsfContext({}));
}

TEST(TsfEditDecision, ReadOnlyTransitoryContextStillBlocksTyping) {
    EXPECT_TRUE(ShouldBlockTsfContext({
        .hasContext = true,
        .dynamicStatusFlags = 0x80000000u | kTsfReadOnlyDocumentFlag,
        .staticStatusFlags = kTsfTransitoryDocumentFlag,
    }));
}

TEST(TsfEditDecision, ExplicitEmptyContextBlocksWritableDocument) {
    EXPECT_TRUE(ShouldBlockTsfContext({
        .hasContext = true,
        .staticStatusFlags = kTsfTransitoryDocumentFlag,
        .emptyContext = true,
    }));
}

TEST(TsfEditDecision, KeyboardDisabledIsNotOverriddenByNonEmptyContext) {
    EXPECT_TRUE(ShouldBlockTsfContext({
        .hasContext = true, .keyboardDisabled = true, .emptyContext = false,
    }));
}

TEST(TsfEditDecision, ProtectedInputScopeStillBlocksWritableContext) {
    EXPECT_TRUE(ShouldBlockTsfContext({
        .hasContext = true,
        .staticStatusFlags = kTsfTransitoryDocumentFlag,
        .scopeBlocked = true,
    }));
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

}  // namespace
}  // namespace NextKey
