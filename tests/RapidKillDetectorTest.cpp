// NexusKey — RapidKillDetector unit tests (Linux-portable)
// SPDX-License-Identifier: GPL-3.0-only

#include <gtest/gtest.h>
#include "watchdog/RapidKillDetector.h"

using NextKey::ShouldStopOnRapidKill;

TEST(RapidKillDetector, NeverSpawnedReturnsFalse) {
    // lastSpawnTime == 0 sentinel: watchdog hasn't respawned yet, so a
    // current "respawn about to happen" is the first one — not rapid.
    EXPECT_FALSE(ShouldStopOnRapidKill(0, 100'000));
    EXPECT_FALSE(ShouldStopOnRapidKill(0, 0));
    EXPECT_FALSE(ShouldStopOnRapidKill(0, 60'000));
}

TEST(RapidKillDetector, WithinThresholdReturnsTrue) {
    EXPECT_TRUE(ShouldStopOnRapidKill(100, 200));        // 100ms gap
    EXPECT_TRUE(ShouldStopOnRapidKill(1'000, 30'000));   // 29s gap
    EXPECT_TRUE(ShouldStopOnRapidKill(1'000, 60'999));   // 59.999s gap (just under)
}

TEST(RapidKillDetector, AtOrAboveThresholdReturnsFalse) {
    EXPECT_FALSE(ShouldStopOnRapidKill(1'000, 61'000));  // 60s exactly == threshold
    EXPECT_FALSE(ShouldStopOnRapidKill(1'000, 200'000)); // 199s gap
    EXPECT_FALSE(ShouldStopOnRapidKill(1'000, 3'600'000));
}

TEST(RapidKillDetector, WrapsAroundCorrectly) {
    // GetTickCount wraps around every ~49.7 days. Unsigned subtraction
    // gives the correct elapsed time across the boundary.
    constexpr uint32_t kMax = 0xFFFFFFFFu;

    // last = max - 1000 (~49.7 days uptime), now = 20 ⇒ real elapsed = 1020 ms
    EXPECT_TRUE(ShouldStopOnRapidKill(kMax - 1000u, 20u));

    // last = max - 100000 + 1, now = 20 ⇒ real elapsed = 100020 ms (> 60s)
    EXPECT_FALSE(ShouldStopOnRapidKill(kMax - 100'000u + 1u, 20u));
}

TEST(RapidKillDetector, CustomThreshold) {
    // Custom threshold lets callers tune sensitivity (used in tests).
    EXPECT_TRUE(ShouldStopOnRapidKill(100, 200, 1000));    // 100 < 1000
    EXPECT_FALSE(ShouldStopOnRapidKill(100, 1500, 1000));  // 1400 >= 1000
    EXPECT_FALSE(ShouldStopOnRapidKill(100, 1100, 1000));  // 1000 == threshold
}
