// NexusKey — HeartbeatPublisher unit tests (Windows-only)
// SPDX-License-Identifier: GPL-3.0-only

#include <gtest/gtest.h>
#include "app/system/HeartbeatPublisher.h"

#ifdef _WIN32

namespace NextKey {

TEST(HeartbeatPublisherTest, StartCreatesNamedEvents) {
    HeartbeatPublisher pub;
    ASSERT_TRUE(pub.Start());

    // Verify external observer can open the events.
    HANDLE hb = OpenEventW(SYNCHRONIZE, FALSE, HEARTBEAT_EVENT_NAME);
    HANDLE gs = OpenEventW(SYNCHRONIZE, FALSE, GRACEFUL_SHUTDOWN_EVENT_NAME);
    EXPECT_NE(hb, nullptr);
    EXPECT_NE(gs, nullptr);
    if (hb) CloseHandle(hb);
    if (gs) CloseHandle(gs);

    pub.Stop();
}

TEST(HeartbeatPublisherTest, GracefulShutdownFlagSignalable) {
    HeartbeatPublisher pub;
    ASSERT_TRUE(pub.Start());

    HANDLE gs = OpenEventW(SYNCHRONIZE, FALSE, GRACEFUL_SHUTDOWN_EVENT_NAME);
    ASSERT_NE(gs, nullptr);

    // Initially not signaled.
    EXPECT_EQ(WaitForSingleObject(gs, 0), WAIT_TIMEOUT);

    pub.SignalGracefulShutdown();

    // Now signaled.
    EXPECT_EQ(WaitForSingleObject(gs, 100), WAIT_OBJECT_0);

    CloseHandle(gs);
    pub.Stop();
}

TEST(HeartbeatPublisherTest, StopIsIdempotent) {
    HeartbeatPublisher pub;
    ASSERT_TRUE(pub.Start());
    pub.Stop();
    pub.Stop();  // No crash.
}

}  // namespace NextKey

#endif  // _WIN32
