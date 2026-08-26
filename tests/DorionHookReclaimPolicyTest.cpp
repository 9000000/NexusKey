// VKey - Dorion foreground hook-reclaim policy tests
// SPDX-License-Identifier: GPL-3.0-only

#include <gtest/gtest.h>

#include <string_view>
#include <utility>

#include "core/DorionHookReclaimPolicy.h"

namespace NextKey {
namespace {

TEST(DorionHookReclaimPolicyTest, RequestsOncePerDorionProcessLifetime) {
    DorionHookReclaimPolicy policy;
    constexpr DorionProcessIdentity first{41, 101};
    constexpr DorionProcessIdentity second{42, 102};

    EXPECT_TRUE(policy.Observe(L"dorion.exe", first, true));
    EXPECT_FALSE(policy.Observe(L"dorion.exe", first, true));
    policy.Complete(first, true);

    EXPECT_FALSE(policy.Observe(L"notepad.exe", {7, 7}, true));
    EXPECT_FALSE(policy.Observe(L"dorion.exe", first, true));

    EXPECT_TRUE(policy.Observe(L"dorion.exe", second, true));
    EXPECT_FALSE(policy.Observe(L"dorion.exe", second, true));
    policy.Complete(second, true);

    EXPECT_FALSE(policy.Observe(L"dorion.exe", first, true));
}

TEST(DorionHookReclaimPolicyTest, RejectsOtherHookAndChromiumHosts) {
    DorionHookReclaimPolicy policy;

    EXPECT_FALSE(policy.Observe(L"discord.exe", {1, 1}, true));
    EXPECT_FALSE(policy.Observe(L"chrome.exe", {2, 2}, true));
    EXPECT_FALSE(policy.Observe(L"msedgewebview2.exe", {3, 3}, true));
    EXPECT_FALSE(policy.Observe(L"javaw.exe", {4, 4}, true));
    EXPECT_FALSE(policy.Observe(L"dorion-helper.exe", {5, 5}, true));
}

TEST(DorionHookReclaimPolicyTest, StaleAndUnknownEvidenceCannotMutateState) {
    DorionHookReclaimPolicy policy;
    constexpr DorionProcessIdentity dorion{41, 101};
    ASSERT_TRUE(policy.Observe(L"dorion.exe", dorion, true));

    EXPECT_FALSE(policy.Observe(L"notepad.exe", {7, 7}, false));
    EXPECT_FALSE(policy.Observe(L"", {}, true));
    EXPECT_FALSE(policy.Observe(L"dorion.exe", dorion, true));
}

TEST(DorionHookReclaimPolicyTest, InvalidIdentityDoesNotLatchOrRequest) {
    DorionHookReclaimPolicy policy;

    EXPECT_FALSE(policy.Observe(L"dorion.exe", {}, true));
    EXPECT_FALSE(policy.Observe(L"dorion.exe", {41, 0}, true));
    EXPECT_TRUE(policy.Observe(L"dorion.exe", {41, 101}, true));
}

TEST(DorionHookReclaimPolicyTest, ResetAllowsCurrentProcessToRequestAgain) {
    DorionHookReclaimPolicy policy;
    constexpr DorionProcessIdentity dorion{41, 101};
    ASSERT_TRUE(policy.Observe(L"dorion.exe", dorion, true));
    policy.Complete(dorion, true);
    ASSERT_FALSE(policy.Observe(L"dorion.exe", dorion, true));

    policy.Reset();

    EXPECT_TRUE(policy.Observe(L"dorion.exe", dorion, true));
}

TEST(DorionHookReclaimPolicyTest, FailedCompletionRearmsSameProcess) {
    DorionHookReclaimPolicy policy;
    constexpr DorionProcessIdentity dorion{41, 101};
    ASSERT_TRUE(policy.Observe(L"dorion.exe", dorion, true));
    ASSERT_FALSE(policy.Observe(L"dorion.exe", dorion, true));

    policy.Complete(dorion, false);

    EXPECT_TRUE(policy.Observe(L"dorion.exe", dorion, true));
}

TEST(DorionHookReclaimPolicyTest, ConcurrentProcessesAndAtoBtoAAreDeduplicated) {
    DorionHookReclaimPolicy policy;
    constexpr DorionProcessIdentity first{41, 101};
    constexpr DorionProcessIdentity second{42, 102};
    ASSERT_TRUE(policy.Observe(L"dorion.exe", first, true));
    ASSERT_TRUE(policy.Observe(L"dorion.exe", second, true));

    EXPECT_FALSE(policy.Observe(L"dorion.exe", first, true));
    EXPECT_FALSE(policy.Observe(L"dorion.exe", second, true));

    policy.Complete(first, true);
    policy.Complete(second, true);
    EXPECT_FALSE(policy.Observe(L"dorion.exe", first, true));
    EXPECT_FALSE(policy.Observe(L"dorion.exe", second, true));
}

TEST(DorionHookReclaimPolicyTest, ReusedPidWithNewCreationTagIsNewProcess) {
    DorionHookReclaimPolicy policy;
    constexpr DorionProcessIdentity oldProcess{41, 101};
    constexpr DorionProcessIdentity newProcess{41, 202};
    ASSERT_TRUE(policy.Observe(L"dorion.exe", oldProcess, true));
    policy.Complete(oldProcess, true);

    EXPECT_FALSE(policy.Observe(L"dorion.exe", oldProcess, true));
    EXPECT_TRUE(policy.Observe(L"dorion.exe", newProcess, true));
}

static_assert(noexcept(std::declval<DorionHookReclaimPolicy&>().Observe(
    std::wstring_view{}, DorionProcessIdentity{}, false)));
static_assert(noexcept(std::declval<DorionHookReclaimPolicy&>().Reset()));
static_assert(noexcept(std::declval<DorionHookReclaimPolicy&>().Complete(
    DorionProcessIdentity{}, false)));

}  // namespace
}  // namespace NextKey
