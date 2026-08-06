// FocusApplyDecisionTest.cpp
// SPDX-License-Identifier: GPL-3.0-only

#include <gtest/gtest.h>

#include <atomic>

#include "core/FocusApplyDecision.h"

namespace NextKey {
namespace {

TEST(FocusApplyDecisionTest, LatestSnapshotWithoutOvertakingInputAppliesNow) {
    const FocusApplyInputs in{
        .snapshotRequestSerial = 7,
        .latestRequestSerial = 7,
        .snapshotInputEpoch = 12,
        .currentInputEpoch = 12,
        .hasComposition = true,
    };

    EXPECT_EQ(DecideFocusApply(in), FocusApplyDisposition::ApplyNow);
}

TEST(FocusApplyDecisionTest, LatestSnapshotOvertakenByInputDefersLiveComposition) {
    const FocusApplyInputs in{
        .snapshotRequestSerial = 7,
        .latestRequestSerial = 7,
        .snapshotInputEpoch = 12,
        .currentInputEpoch = 16,
        .hasComposition = true,
    };

    EXPECT_EQ(DecideFocusApply(in),
              FocusApplyDisposition::DeferUntilBoundary);
}

TEST(FocusApplyDecisionTest, OvertakenSnapshotAppliesOnceCompositionIsEmpty) {
    const FocusApplyInputs in{
        .snapshotRequestSerial = 7,
        .latestRequestSerial = 7,
        .snapshotInputEpoch = 12,
        .currentInputEpoch = 16,
        .hasComposition = false,
    };

    EXPECT_EQ(DecideFocusApply(in), FocusApplyDisposition::ApplyNow);
}

TEST(FocusApplyDecisionTest, OlderRequestAlwaysDropsEvenAtSafeBoundary) {
    for (const bool hasComposition : {false, true}) {
        const FocusApplyInputs in{
            .snapshotRequestSerial = 7,
            .latestRequestSerial = 8,
            .snapshotInputEpoch = 12,
            .currentInputEpoch = 12,
            .hasComposition = hasComposition,
        };

        EXPECT_EQ(DecideFocusApply(in), FocusApplyDisposition::DropStale);
    }
}

TEST(FocusApplyDecisionTest, SerialIdentityDoesNotDependOnArrivalOrder) {
    const FocusApplyInputs newerResultArrivedFirst{
        .snapshotRequestSerial = 42,
        .latestRequestSerial = 42,
        .snapshotInputEpoch = 100,
        .currentInputEpoch = 100,
        .hasComposition = false,
    };
    const FocusApplyInputs olderResultArrivedLast{
        .snapshotRequestSerial = 41,
        .latestRequestSerial = 42,
        .snapshotInputEpoch = 100,
        .currentInputEpoch = 100,
        .hasComposition = false,
    };

    EXPECT_EQ(DecideFocusApply(newerResultArrivedFirst),
              FocusApplyDisposition::ApplyNow);
    EXPECT_EQ(DecideFocusApply(olderResultArrivedLast),
              FocusApplyDisposition::DropStale);
}

TEST(FocusApplyDecisionTest, ExhaustiveOrderingMatrixMatchesPolicy) {
    for (const bool isLatest : {false, true}) {
        for (const bool inputWasOvertaken : {false, true}) {
            for (const bool hasComposition : {false, true}) {
                const FocusApplyInputs in{
                    .snapshotRequestSerial = isLatest ? 9u : 8u,
                    .latestRequestSerial = 9u,
                    .snapshotInputEpoch = 21u,
                    .currentInputEpoch = inputWasOvertaken ? 22u : 21u,
                    .hasComposition = hasComposition,
                };

                const auto expected = !isLatest
                    ? FocusApplyDisposition::DropStale
                    : (inputWasOvertaken && hasComposition
                        ? FocusApplyDisposition::DeferUntilBoundary
                        : FocusApplyDisposition::ApplyNow);
                EXPECT_EQ(DecideFocusApply(in), expected)
                    << "isLatest=" << isLatest
                    << " inputWasOvertaken=" << inputWasOvertaken
                    << " hasComposition=" << hasComposition;
            }
        }
    }
}

static_assert(noexcept(DecideFocusApply(FocusApplyInputs{})));
// The hook hot path cost of the epoch is exactly this property. A wall-clock
// benchmark here measured ~1 ns against a 50 ns bar — it could never fail for a
// real regression, only for a loaded CI box, so the static_assert is the check.
static_assert(std::atomic<std::uint64_t>::is_always_lock_free,
              "focus input epoch must stay lock-free on the hook hot path");

}  // namespace
}  // namespace NextKey
