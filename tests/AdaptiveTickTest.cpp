// Tests for ComputeTickInterval — pure idle-backoff cadence function.
// Linux-portable: no Win32, no threads, no atomics. Pins the boundary mapping
// that drives MainThreadWorker cadence retune in HookEngine::OnTickPoll +
// HookEngine::RetuneCadenceIfNeeded.
//
// Plan reference: docs/plans/2026-05-27-adaptive-tick-idle-backoff.md §3.1
// SPDX-License-Identifier: AGPL-3.0-only

#include <gtest/gtest.h>

#include "app/system/AdaptiveTick.h"

using NextKey::ComputeTickInterval;
using NextKey::kIdleLongThreshMs;
using NextKey::kIdleShortThreshMs;
using NextKey::kIdleStopThreshMs;
using NextKey::kTickActiveMs;
using NextKey::kTickIdleLongMs;
using NextKey::kTickIdleShortMs;

TEST(AdaptiveTickTest, ZeroIdle_ReturnsActive) {
    EXPECT_EQ(ComputeTickInterval(0), std::chrono::milliseconds(kTickActiveMs));
}

TEST(AdaptiveTickTest, JustBelowShortThreshold_ReturnsActive) {
    EXPECT_EQ(ComputeTickInterval(kIdleShortThreshMs - 1),
              std::chrono::milliseconds(kTickActiveMs));
}

TEST(AdaptiveTickTest, AtShortThreshold_ReturnsIdleShort) {
    EXPECT_EQ(ComputeTickInterval(kIdleShortThreshMs),
              std::chrono::milliseconds(kTickIdleShortMs));
}

TEST(AdaptiveTickTest, JustBelowLongThreshold_ReturnsIdleShort) {
    EXPECT_EQ(ComputeTickInterval(kIdleLongThreshMs - 1),
              std::chrono::milliseconds(kTickIdleShortMs));
}

TEST(AdaptiveTickTest, AtLongThreshold_ReturnsIdleLong) {
    EXPECT_EQ(ComputeTickInterval(kIdleLongThreshMs),
              std::chrono::milliseconds(kTickIdleLongMs));
}

TEST(AdaptiveTickTest, JustBelowStopThreshold_ReturnsIdleLong) {
    EXPECT_EQ(ComputeTickInterval(kIdleStopThreshMs - 1),
              std::chrono::milliseconds(kTickIdleLongMs));
}

TEST(AdaptiveTickTest, AtStopThreshold_ReturnsStop) {
    // 0 ms is the STOP sentinel — owner maps it to SetTickInterval(0) so the
    // worker parks (cv_.wait ∞) and Windows trims the working set.
    EXPECT_EQ(ComputeTickInterval(kIdleStopThreshMs),
              std::chrono::milliseconds(0));
}

TEST(AdaptiveTickTest, FarPastStopThreshold_StaysStopped) {
    // 24 hours of idle — must stay STOPPED (0), not roll over into anything
    // weird (catches accidental wrap or off-by-one).
    EXPECT_EQ(ComputeTickInterval(24ULL * 60 * 60 * 1000),
              std::chrono::milliseconds(0));
}

TEST(AdaptiveTickTest, ConstantsMatchDocumentedThresholds) {
    // Pin the documented values so a stealth retune of constants in the header
    // forces a deliberate test update (and a doc/plan update with it).
    EXPECT_EQ(kTickActiveMs,      200u);
    EXPECT_EQ(kTickIdleShortMs,   1000u);
    EXPECT_EQ(kTickIdleLongMs,    5000u);
    EXPECT_EQ(kIdleShortThreshMs, 10000u);
    EXPECT_EQ(kIdleLongThreshMs,  60000u);
    EXPECT_EQ(kIdleStopThreshMs,  120000u);
}
