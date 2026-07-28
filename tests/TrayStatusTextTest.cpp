// VKey - tray status tooltip formatter tests
// SPDX-License-Identifier: AGPL-3.0-only

#include <gtest/gtest.h>

#include "app/system/TrayStatusText.h"

namespace NextKey {
namespace {

constexpr std::wstring_view kModeV = L"VKey - Vietnamese";
constexpr std::wstring_view kModeE = L"VKey - English";

// Cold start: no focus classification has landed, so the tooltip must stay the
// plain mode line rather than guess an engine/method it has not observed.
TEST(TrayStatusTextTest, EmptyAppContextShowsModeOnly) {
    EXPECT_EQ(FormatTrayStatusText(kModeV, TrayStatusContext{}, true), kModeV);
    EXPECT_EQ(FormatTrayStatusText(kModeE, TrayStatusContext{}, false), kModeE);
}

TEST(TrayStatusTextTest, ModeLabelAlwaysLeadsTheLine) {
    const TrayStatusContext context{.exeName = L"notepad.exe"};

    EXPECT_EQ(
        FormatTrayStatusText(kModeE, context, true),
        L"VKey - English \x2014 notepad.exe \x2022 C++ \x2022 Hook");
}

TEST(TrayStatusTextTest, FullContextUsesStableSingleLineOrder) {
    const TrayStatusContext context{
        .exeName = L"chrome.exe",
        .ruleText = L"Smart Switch",
        .isTsf = true,
        .isRustEngine = true,
    };

    EXPECT_EQ(
        FormatTrayStatusText(kModeV, context, true),
        L"VKey - Vietnamese \x2014 chrome.exe \x2022 Rust \x2022 TSF "
        L"\x2022 Smart Switch");
}

// #209: with the "T" indicator off the icon is plain V/E, so the tooltip must
// not mention the input method either — in TSF hosts or Hook hosts.
TEST(TrayStatusTextTest, MethodTextIsGatedByTheIndicatorSetting) {
    const TrayStatusContext tsf{
        .exeName = L"chrome.exe",
        .isTsf = true,
        .isRustEngine = true,
    };

    EXPECT_EQ(
        FormatTrayStatusText(kModeV, tsf, false),
        L"VKey - Vietnamese \x2014 chrome.exe \x2022 Rust");

    const TrayStatusContext hook{.exeName = L"chrome.exe"};
    EXPECT_EQ(
        FormatTrayStatusText(kModeV, hook, false),
        L"VKey - Vietnamese \x2014 chrome.exe \x2022 C++");
}

TEST(TrayStatusTextTest, RuleSurvivesTheGatedMethod) {
    const TrayStatusContext context{
        .exeName = L"cmd.exe",
        .ruleText = L"Lock E",
        .isRustEngine = true,
    };

    EXPECT_EQ(
        FormatTrayStatusText(kModeE, context, false),
        L"VKey - English \x2014 cmd.exe \x2022 Rust \x2022 Lock E");
}

}  // namespace
}  // namespace NextKey
