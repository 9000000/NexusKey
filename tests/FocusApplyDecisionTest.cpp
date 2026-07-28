// FocusApplyDecisionTest.cpp
// SPDX-License-Identifier: AGPL-3.0-only

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>

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
static_assert(std::atomic<std::uint64_t>::is_always_lock_free,
              "focus input epoch must stay lock-free on the hook hot path");

TEST(FocusEpochBenchmark, RelaxedIncrementStaysWithinHookBudget) {
    std::atomic<std::uint64_t> epoch{0};
    constexpr int kIterations = 1'000'000;

    const auto started = std::chrono::steady_clock::now();
    for (int i = 0; i < kIterations; ++i) {
        epoch.fetch_add(1, std::memory_order_relaxed);
    }
    const auto elapsedNs =
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now() - started)
            .count();
    const auto nsPerIncrement = elapsedNs / kIterations;

    EXPECT_EQ(epoch.load(std::memory_order_relaxed),
              static_cast<std::uint64_t>(kIterations));
    EXPECT_LT(nsPerIncrement, 50)
        << "physical-input epoch increment exceeds the established hook "
           "hot-path budget. Per increment: "
        << nsPerIncrement << " ns";
}

}  // namespace
}  // namespace NextKey
