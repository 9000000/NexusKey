// VKey - tray status tooltip formatter tests
// SPDX-License-Identifier: AGPL-3.0-only

#include <gtest/gtest.h>

#include "app/system/TrayStatusText.h"

namespace NextKey {
namespace {

TEST(TrayStatusTextTest, DefaultContextShowsCppHook) {
    EXPECT_EQ(
        FormatTrayStatusText(TrayStatusContext{}),
        L"C++ \x2022 Hook");
}

TEST(TrayStatusTextTest, AppContextPrefixesExecutable) {
    const TrayStatusContext context{
        .exeName = L"notepad.exe",
    };

    EXPECT_EQ(
        FormatTrayStatusText(context),
        L"notepad.exe \x2014 C++ \x2022 Hook");
}

TEST(TrayStatusTextTest, FullContextUsesStableSingleLineOrder) {
    const TrayStatusContext context{
        .exeName = L"chrome.exe",
        .ruleText = L"Smart Switch",
        .isTsf = true,
        .isRustEngine = true,
    };

    EXPECT_EQ(
        FormatTrayStatusText(context),
        L"chrome.exe \x2014 Rust \x2022 TSF \x2022 Smart Switch");
}

TEST(TrayStatusTextTest, RuleDoesNotRequireExecutable) {
    const TrayStatusContext context{
        .ruleText = L"Lock E",
        .isRustEngine = true,
    };

    EXPECT_EQ(
        FormatTrayStatusText(context),
        L"Rust \x2022 Hook \x2022 Lock E");
}

}  // namespace
}  // namespace NextKey
