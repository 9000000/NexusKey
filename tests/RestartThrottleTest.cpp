// VKey — RestartThrottle unit tests (Linux-portable)
// SPDX-License-Identifier: AGPL-3.0-only

#include <gtest/gtest.h>

#include "core/RestartThrottle.h"

namespace NextKey {
namespace {

// Window = 10s, cap = 3 for compact test arithmetic.
constexpr uint64_t kWindow = 10'000;

TEST(RestartThrottle, AllowsUpToCapWithinWindow) {
    RestartThrottle<3> t{kWindow};
    EXPECT_TRUE(t.AllowRestart(0));
    EXPECT_TRUE(t.AllowRestart(1000));
    EXPECT_TRUE(t.AllowRestart(2000));
    // 4th within the window is refused — this is the anti-AV-self-defense stop.
    EXPECT_FALSE(t.AllowRestart(3000));
    EXPECT_EQ(t.RecentCount(), 3u);
}

TEST(RestartThrottle, OldAttemptsExpireSoNewOnesAllowed) {
    RestartThrottle<3> t{kWindow};
    EXPECT_TRUE(t.AllowRestart(0));
    EXPECT_TRUE(t.AllowRestart(1000));
    EXPECT_TRUE(t.AllowRestart(2000));
    EXPECT_FALSE(t.AllowRestart(3000));  // window full

    // At t=11000 the first three (0,1000,2000) have aged past the 10s window;
    // 0+10000=10000 <= 11000 so it drops, freeing a slot.
    EXPECT_TRUE(t.AllowRestart(11'000));
}

TEST(RestartThrottle, BoundaryEntryOnWindowEdgeStillCounts) {
    RestartThrottle<1> t{kWindow};
    EXPECT_TRUE(t.AllowRestart(0));
    // times_[i] + window > now  → 0+10000 > 10000 is false, so exactly at the
    // edge the old entry is dropped and a new one is allowed.
    EXPECT_TRUE(t.AllowRestart(10'000));
    // Just before the edge it is still live → refused.
    EXPECT_FALSE(t.AllowRestart(19'999));
}

TEST(RestartThrottle, SteadyDripNeverTripsCap) {
    // One restart every full window never accumulates — a genuine long-uptime
    // process that crashes rarely is always allowed to recover.
    RestartThrottle<3> t{kWindow};
    uint64_t now = 0;
    for (int i = 0; i < 100; ++i) {
        EXPECT_TRUE(t.AllowRestart(now));
        now += kWindow;  // exactly one window apart → prior entry always expired
    }
}

}  // namespace
}  // namespace NextKey
