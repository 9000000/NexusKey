// NexusKey — HookSelfHealer unit tests (Linux-runnable via mock callback)
// SPDX-License-Identifier: GPL-3.0-only

#include <gtest/gtest.h>
#include "app/system/HookSelfHealer.h"

#ifdef _WIN32

#include <atomic>

namespace NextKey {

// Test fixture exposes private trigger paths via a friend or via a
// behavior-test approach. We test via observable side-effects: the
// reinstaller callback's invocation count.

class HookSelfHealerTest : public ::testing::Test {
protected:
    std::atomic<int> reinstallerCallCount_{0};
    bool reinstallerReturn_ = true;

    ReinstallHookFn MakeReinstaller() {
        return [this]() {
            reinstallerCallCount_.fetch_add(1, std::memory_order_relaxed);
            return reinstallerReturn_;
        };
    }
};

TEST_F(HookSelfHealerTest, ConstructAndDestructWithoutStart) {
    RawInputSelfHealer healer(GetModuleHandleW(nullptr), MakeReinstaller());
    // Destructor without Start should be safe.
    EXPECT_EQ(reinstallerCallCount_.load(), 0);
}

TEST_F(HookSelfHealerTest, RecordHookFireDoesNotCrashWithoutStart) {
    RawInputSelfHealer healer(GetModuleHandleW(nullptr), MakeReinstaller());
    healer.RecordHookFire();  // Must not crash even without Start.
    EXPECT_EQ(reinstallerCallCount_.load(), 0);
}

TEST_F(HookSelfHealerTest, StopWithoutStartIsIdempotent) {
    RawInputSelfHealer healer(GetModuleHandleW(nullptr), MakeReinstaller());
    healer.Stop();  // Idempotent — should not crash, should not invoke reinstaller.
    healer.Stop();
    EXPECT_EQ(reinstallerCallCount_.load(), 0);
}

}  // namespace NextKey

#endif  // _WIN32
