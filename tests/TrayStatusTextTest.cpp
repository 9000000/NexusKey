// VKey - tray status tooltip formatter tests
// SPDX-License-Identifier: GPL-3.0-only

#include <gtest/gtest.h>

#include "app/system/TrayStatusText.h"

namespace NextKey {
namespace {

constexpr std::wstring_view kModeV = L"VKey - V";
constexpr std::wstring_view kModeE = L"VKey - E";

// Cold start: no focus classification has landed, so the tooltip must stay the
// plain mode line rather than guess an engine/method it has not observed.
TEST(TrayStatusTextTest, EmptyAppContextShowsModeOnly) {
    EXPECT_EQ(FormatTrayStatusText(kModeV, TrayStatusContext{}), kModeV);
    EXPECT_EQ(FormatTrayStatusText(kModeE, TrayStatusContext{}), kModeE);
}

TEST(TrayStatusTextTest, ModeLabelAlwaysLeadsTheLine) {
    const TrayStatusContext context{.exeName = L"notepad.exe"};

    EXPECT_EQ(
        FormatTrayStatusText(kModeE, context),
        L"VKey - E \x2014 notepad.exe \x2022 C++ \x2022 Hook");
}

TEST(TrayStatusTextTest, FullContextUsesStableSingleLineOrder) {
    const TrayStatusContext context{
        .exeName = L"chrome.exe",
        .ruleText = L"Smart Switch",
        .isTsf = true,
        .isRustEngine = true,
    };

    EXPECT_EQ(
        FormatTrayStatusText(kModeV, context),
        L"VKey - V \x2014 chrome.exe \x2022 Rust \x2022 TSF "
        L"\x2022 Smart Switch");
}

TEST(TrayStatusTextTest, MethodTextIsAlwaysIncluded) {
    const TrayStatusContext tsf{
        .exeName = L"chrome.exe",
        .isTsf = true,
        .isRustEngine = true,
    };

    EXPECT_EQ(
        FormatTrayStatusText(kModeV, tsf),
        L"VKey - V \x2014 chrome.exe \x2022 Rust \x2022 TSF");

    const TrayStatusContext hook{.exeName = L"chrome.exe"};
    EXPECT_EQ(
        FormatTrayStatusText(kModeV, hook),
        L"VKey - V \x2014 chrome.exe \x2022 C++ \x2022 Hook");
}

TEST(TrayStatusTextTest, RuleSurvivesWithMethod) {
    const TrayStatusContext context{
        .exeName = L"cmd.exe",
        .ruleText = L"Lock E",
        .isRustEngine = true,
    };

    EXPECT_EQ(
        FormatTrayStatusText(kModeE, context),
        L"VKey - E \x2014 cmd.exe \x2022 Rust \x2022 Hook \x2022 Lock E");
}

}  // namespace
}  // namespace NextKey
