// VKey - single-active keyboard-hook replacement transaction tests
// SPDX-License-Identifier: GPL-3.0-only

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <utility>

#include "core/SingleHookReplacement.h"

namespace NextKey {
namespace {

struct FakeHookOps {
    SingleHookUnhookStatus unhookStatus = SingleHookUnhookStatus::Removed;
    std::uintptr_t nextInstallHandle = 22;
    std::array<int, 2> calls{};
    std::size_t callCount = 0;
    std::uintptr_t unhookedHandle = 0;
    int installCalls = 0;

    static SingleHookUnhookStatus Unhook(
            void* context, std::uintptr_t handle) noexcept {
        auto& self = *static_cast<FakeHookOps*>(context);
        if (self.callCount < self.calls.size()) self.calls[self.callCount++] = 1;
        self.unhookedHandle = handle;
        return self.unhookStatus;
    }

    static std::uintptr_t Install(void* context) noexcept {
        auto& self = *static_cast<FakeHookOps*>(context);
        if (self.callCount < self.calls.size()) self.calls[self.callCount++] = 2;
        ++self.installCalls;
        return self.nextInstallHandle;
    }

    [[nodiscard]] SingleHookReplacementOps Ops() noexcept {
        return {
            .context = this,
            .unhook = &FakeHookOps::Unhook,
            .install = &FakeHookOps::Install,
        };
    }
};

TEST(SingleHookReplacementTest, ExistingHandleIsRemovedBeforeInstall) {
    FakeHookOps fake;
    std::uintptr_t handle = 11;

    EXPECT_EQ(ReplaceSingleHook(handle, fake.Ops()),
              SingleHookReplacementStatus::Replaced);

    EXPECT_EQ(fake.callCount, 2u);
    EXPECT_EQ(fake.calls[0], 1);
    EXPECT_EQ(fake.calls[1], 2);
    EXPECT_EQ(fake.unhookedHandle, 11u);
    EXPECT_EQ(handle, 22u);
}

TEST(SingleHookReplacementTest, UnhookFailureNeverCreatesSecondHook) {
    FakeHookOps fake;
    fake.unhookStatus = SingleHookUnhookStatus::Failed;
    std::uintptr_t handle = 11;

    EXPECT_EQ(ReplaceSingleHook(handle, fake.Ops()),
              SingleHookReplacementStatus::UnhookFailed);

    EXPECT_EQ(fake.callCount, 1u);
    EXPECT_EQ(fake.calls[0], 1);
    EXPECT_EQ(fake.installCalls, 0);
    EXPECT_EQ(handle, 11u);
}

TEST(SingleHookReplacementTest, AlreadyAbsentStaleHandleCanRecover) {
    FakeHookOps fake;
    fake.unhookStatus = SingleHookUnhookStatus::AlreadyAbsent;
    std::uintptr_t handle = 11;

    EXPECT_EQ(ReplaceSingleHook(handle, fake.Ops()),
              SingleHookReplacementStatus::Replaced);

    EXPECT_EQ(fake.callCount, 2u);
    EXPECT_EQ(fake.calls[0], 1);
    EXPECT_EQ(fake.calls[1], 2);
    EXPECT_EQ(handle, 22u);
}

TEST(SingleHookReplacementTest, InstallFailureLeavesNoActiveHandle) {
    FakeHookOps fake;
    fake.nextInstallHandle = 0;
    std::uintptr_t handle = 11;

    EXPECT_EQ(ReplaceSingleHook(handle, fake.Ops()),
              SingleHookReplacementStatus::InstallFailed);

    EXPECT_EQ(fake.callCount, 2u);
    EXPECT_EQ(fake.calls[0], 1);
    EXPECT_EQ(fake.calls[1], 2);
    EXPECT_EQ(handle, 0u);
}

TEST(SingleHookReplacementTest, MissingOldHandleInstallsDirectly) {
    FakeHookOps fake;
    std::uintptr_t handle = 0;

    EXPECT_EQ(ReplaceSingleHook(handle, fake.Ops()),
              SingleHookReplacementStatus::Replaced);

    EXPECT_EQ(fake.callCount, 1u);
    EXPECT_EQ(fake.calls[0], 2);
    EXPECT_EQ(fake.unhookedHandle, 0u);
    EXPECT_EQ(handle, 22u);
}

static_assert(noexcept(ReplaceSingleHook(
    std::declval<std::uintptr_t&>(),
    std::declval<const SingleHookReplacementOps&>())));

}  // namespace
}  // namespace NextKey
