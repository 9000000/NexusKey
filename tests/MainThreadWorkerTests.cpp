// MainThreadWorkerTests.cpp
// SPDX-License-Identifier: GPL-3.0-only
//
// Sprint 1 D8: scaffolding contract tests for MainThreadWorker.
//
// D8 is "start/stop only" — no work items yet. These tests lock in the
// lifecycle contract that D9 (config-event channel) and D10 (heartbeat +
// CJK poll) will extend without rewriting:
//   - Start launches a worker thread; IsRunning reports true.
//   - Stop signals shutdown and joins; IsRunning reports false.
//   - Stop completes within the < 100 ms DoD budget (plan §C D8).
//   - Repeated Start / Stop / destructor calls are idempotent and safe.
//   - Destructor implies Stop (RAII; no thread leak).
//
// MainThreadWorker is portable (std::thread + condition_variable) so this
// suite runs on the Linux test build. D9 will swap the internal wait
// primitive to Win32 WaitForMultipleObjects when ConfigEvent is wired in;
// the Start/Stop/IsRunning public contract this suite checks must still
// hold then.

#include <gtest/gtest.h>

#include <chrono>
#include <thread>

#include "app/system/MainThreadWorker.h"

namespace NextKey {
namespace {

using namespace std::chrono_literals;

class MainThreadWorkerTest : public ::testing::Test {
protected:
    MainThreadWorker worker_;
};

TEST_F(MainThreadWorkerTest, StartLaunchesThread_StopJoinsCleanly) {
    EXPECT_FALSE(worker_.IsRunning());
    EXPECT_TRUE(worker_.Start());
    EXPECT_TRUE(worker_.IsRunning());

    const auto t0 = std::chrono::steady_clock::now();
    worker_.Stop();
    const auto elapsed = std::chrono::steady_clock::now() - t0;

    EXPECT_FALSE(worker_.IsRunning());
    // DoD: start/stop completes in < 100 ms (plan §C D8).
    EXPECT_LT(std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count(), 100);
}

TEST_F(MainThreadWorkerTest, SecondStartWhileRunningReturnsFalse) {
    ASSERT_TRUE(worker_.Start());
    EXPECT_FALSE(worker_.Start());  // already running
    EXPECT_TRUE(worker_.IsRunning());
    worker_.Stop();
}

TEST_F(MainThreadWorkerTest, StopWithoutStartIsNoOp) {
    EXPECT_FALSE(worker_.IsRunning());
    EXPECT_NO_THROW(worker_.Stop());
    EXPECT_FALSE(worker_.IsRunning());
}

TEST_F(MainThreadWorkerTest, StopAfterStopIsNoOp) {
    ASSERT_TRUE(worker_.Start());
    worker_.Stop();
    EXPECT_FALSE(worker_.IsRunning());
    EXPECT_NO_THROW(worker_.Stop());
    EXPECT_FALSE(worker_.IsRunning());
}

TEST_F(MainThreadWorkerTest, MultipleStartStopCyclesWork) {
    for (int i = 0; i < 3; ++i) {
        EXPECT_TRUE(worker_.Start()) << "cycle " << i;
        EXPECT_TRUE(worker_.IsRunning()) << "cycle " << i;
        worker_.Stop();
        EXPECT_FALSE(worker_.IsRunning()) << "cycle " << i;
    }
}

TEST(MainThreadWorkerLifetimeTest, DestructorImpliesStop_NoThreadLeak) {
    // Worker goes out of scope while running — destructor must Stop+join.
    // If RAII is broken, the thread leaks and ASan/TSan / process exit
    // detect a dangling thread; here we just check the destructor returns.
    {
        MainThreadWorker w;
        ASSERT_TRUE(w.Start());
        ASSERT_TRUE(w.IsRunning());
        // Let the worker reach its idle wait before destruction.
        std::this_thread::sleep_for(5ms);
    }
    // If we get here without the test runner hanging, RAII held.
    SUCCEED();
}

TEST(MainThreadWorkerLifetimeTest, RapidStartStop_NoDeadlockOrCrash) {
    // Stress the lifecycle to surface any race in the start/stop signal.
    for (int i = 0; i < 50; ++i) {
        MainThreadWorker w;
        ASSERT_TRUE(w.Start()) << "iteration " << i;
        w.Stop();
    }
}

}  // namespace
}  // namespace NextKey
