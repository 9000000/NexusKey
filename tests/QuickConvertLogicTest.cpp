// VKey - Quick-convert shared logic tests
// SPDX-License-Identifier: GPL-3.0-only

#include <gtest/gtest.h>

#include "core/QuickConvertLogic.h"

namespace NextKey {
namespace {

TEST(QuickConvertBackendDecision, UsesHookUnlessTsfAppAndNativeReady) {
    EXPECT_EQ(DecideQuickConvertBackend(false, false), QuickConvertBackend::HookClipboard);
    EXPECT_EQ(DecideQuickConvertBackend(false, true), QuickConvertBackend::HookClipboard);
    EXPECT_EQ(DecideQuickConvertBackend(true, false), QuickConvertBackend::HookClipboard);
    EXPECT_EQ(DecideQuickConvertBackend(true, true), QuickConvertBackend::TsfNative);
}

TEST(QuickConvertLogic, AppliesConfiguredCaseWithoutDuplicatingBackendRules) {
    ConvertConfig config{};
    config.capsFirst = true;

    EXPECT_EQ(ApplyQuickConvertConfig(L"vkey.com. vKey API", config),
              L"Vkey.com. VKey API");
}

TEST(QuickConvertLogic, RemoveMarkRunsBeforeCaseConversion) {
    ConvertConfig config{};
    config.removeMark = true;
    config.allCaps = true;

    EXPECT_EQ(ApplyQuickConvertConfig(L"Việt Nam", config), L"VIET NAM");
}

TEST(QuickConvertLogic, EnabledOptionsFollowStableCycleOrder) {
    ConvertConfig config{};
    config.allLower = true;
    config.capsFirst = true;
    config.removeMark = true;

    const auto options = GetEnabledQuickConvertOptions(config);
    ASSERT_EQ(options.size(), 3u);
    EXPECT_EQ(options[0], QuickConvertOption::Lower);
    EXPECT_EQ(options[1], QuickConvertOption::SentenceCase);
    EXPECT_EQ(options[2], QuickConvertOption::RemoveDiacritics);
}

}  // namespace
}  // namespace NextKey
