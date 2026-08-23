// BrowserContextStateTest.cpp
// SPDX-License-Identifier: GPL-3.0-only

#include <gtest/gtest.h>

#include "core/ipc/BrowserContextState.h"

namespace NextKey {
namespace {

TEST(BrowserContextStateTest, AppliesOnlyToFreshMatchingForegroundBrowser) {
    BrowserContextState state{};
    state.focused = 1;
    state.route = BrowserRoute::ForceEnglish;
    state.updatedTickMs = 1'000;
    std::memcpy(state.browserExe, "firefox.exe", sizeof("firefox.exe"));

    EXPECT_TRUE(IsBrowserContextApplicable(state, "firefox.exe", 5'999));
    EXPECT_FALSE(IsBrowserContextApplicable(state, "chrome.exe", 5'999));
    EXPECT_FALSE(IsBrowserContextApplicable(state, "firefox.exe", 6'001));
}

TEST(BrowserContextStateTest, DefaultAndBlurredContextsNeverOverrideVKey) {
    BrowserContextState state{};
    state.updatedTickMs = 1'000;
    std::memcpy(state.browserExe, "chrome.exe", sizeof("chrome.exe"));
    state.focused = 1;
    state.route = BrowserRoute::Default;
    EXPECT_FALSE(IsBrowserContextApplicable(state, "chrome.exe", 1'001));
    EXPECT_TRUE(IsBrowserContextFocusedForExe(state, "chrome.exe", 1'001));

    state.mode = BrowserMode::Vietnamese;
    EXPECT_TRUE(IsBrowserContextApplicable(state, "chrome.exe", 1'001));

    state.route = BrowserRoute::ForceTsf;
    state.focused = 0;
    EXPECT_FALSE(IsBrowserContextApplicable(state, "chrome.exe", 1'001));
}

TEST(BrowserContextStateTest, HardLocksWinOverSessionMode) {
    EXPECT_EQ(ResolveBrowserModeTarget(false, false, BrowserRoute::Default,
                                       BrowserMode::Vietnamese), 1);
    EXPECT_EQ(ResolveBrowserModeTarget(false, false, BrowserRoute::Default,
                                       BrowserMode::English), 0);
    EXPECT_EQ(ResolveBrowserModeTarget(false, false, BrowserRoute::Default,
                                       BrowserMode::Default), -1);
    EXPECT_EQ(ResolveBrowserModeTarget(false, false, BrowserRoute::ForceEnglish,
                                       BrowserMode::Vietnamese), -1);
    EXPECT_EQ(ResolveBrowserModeTarget(true, false, BrowserRoute::Default,
                                       BrowserMode::Vietnamese), -1);
    EXPECT_EQ(ResolveBrowserModeTarget(false, true, BrowserRoute::Default,
                                       BrowserMode::English), -1);
    EXPECT_EQ(ResolveBrowserModeTarget(false, false, BrowserRoute::Default,
                                       static_cast<BrowserMode>(99)), -1);
}

TEST(BrowserContextStateTest, PendingHotkeyEventSuppressesStaleHeartbeatMode) {
    BrowserContextState state{};
    state.ownerProcessId = 101;
    state.ownerNonce = 1001;
    state.mode = BrowserMode::Vietnamese;
    state.modeEventSequence = 7;
    state.modeEventOwnerProcessId = 101;
    state.modeEventOwnerNonce = 1001;
    state.modeEventMode = BrowserMode::English;
    std::memcpy(state.hostname, "example.com", sizeof("example.com"));
    std::memcpy(state.modeEventHostname, "example.com", sizeof("example.com"));

    EXPECT_EQ(ResolveBrowserModeForRestore(state), BrowserMode::Default);
    state.mode = BrowserMode::English;
    EXPECT_EQ(ResolveBrowserModeForRestore(state), BrowserMode::English);
    std::memcpy(state.hostname, "other.example.com", sizeof("other.example.com"));
    EXPECT_EQ(ResolveBrowserModeForRestore(state), BrowserMode::English);
}

TEST(BrowserContextStateTest, DomainRouteHasExpectedPrecedence) {
    EXPECT_TRUE(ResolveEffectiveTsf(false, false, true, BrowserRoute::ForceTsf));
    EXPECT_FALSE(ResolveEffectiveTsf(true, false, true, BrowserRoute::ForceEnglish));
    EXPECT_TRUE(ResolveEffectiveTsf(true, false, true, BrowserRoute::Default));
    EXPECT_FALSE(ResolveEffectiveTsf(true, true, true, BrowserRoute::ForceTsf));
    EXPECT_FALSE(ResolveEffectiveTsf(false, false, false, BrowserRoute::ForceTsf));
}

TEST(BrowserContextStateTest, BlurCannotClearAnotherBrowserConnection) {
    BrowserContextState state{};
    state.ownerProcessId = 101;
    state.ownerNonce = 1001;

    EXPECT_TRUE(IsBrowserContextOwnedBy(state, 101, 1001));
    EXPECT_FALSE(IsBrowserContextOwnedBy(state, 202, 2002));
    EXPECT_FALSE(IsBrowserContextOwnedBy(state, 101, 2002));
    EXPECT_FALSE(IsBrowserContextOwnedBy(state, 0, 1001));
}

} // namespace
} // namespace NextKey
