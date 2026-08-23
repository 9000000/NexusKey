// NativeMessagingTest.cpp
// SPDX-License-Identifier: GPL-3.0-only

#include <gtest/gtest.h>

#include "browser_host/NativeMessaging.h"

namespace NextKey::BrowserHost {
namespace {

TEST(NativeMessagingTest, ParsesMinimalPrivacyPreservingContext) {
    NativeMessage message;
    std::string error;
    ASSERT_TRUE(ParseNativeMessage(
        R"({"protocol":1,"browser":"firefox.exe","hostname":"voz.vn","route":"tsf","focused":true})",
        message, error)) << error;
    EXPECT_EQ(message.browserExe, "firefox.exe");
    EXPECT_EQ(message.hostname, "voz.vn");
    EXPECT_EQ(message.route, BrowserRoute::ForceTsf);
    EXPECT_TRUE(message.focused);
}

TEST(NativeMessagingTest, ParsesProtocol2HardRouteAndSessionMode) {
    NativeMessage message;
    std::string error;
    ASSERT_TRUE(ParseNativeMessage(
        R"({"protocol":2,"browser":"chrome.exe","hostname":"example.com","route":"default","mode":"vietnamese","focused":true})",
        message, error)) << error;
    EXPECT_EQ(message.protocol, 2);
    EXPECT_EQ(message.route, BrowserRoute::Default);
    EXPECT_EQ(message.mode, BrowserMode::Vietnamese);
}

TEST(NativeMessagingTest, RejectsUrlsUnknownFieldsAndUnsupportedBrowsers) {
    NativeMessage message;
    std::string error;
    EXPECT_FALSE(ParseNativeMessage(
        R"({"protocol":1,"browser":"firefox.exe","hostname":"voz.vn/path","route":"tsf","focused":true})",
        message, error));
    EXPECT_FALSE(ParseNativeMessage(
        R"({"protocol":1,"browser":"unknown.exe","hostname":"voz.vn","route":"tsf","focused":true})",
        message, error));
    EXPECT_FALSE(ParseNativeMessage(
        R"({"protocol":1,"browser":"firefox.exe","hostname":"voz.vn","route":"tsf","focused":true,"url":"secret"})",
        message, error));
    EXPECT_FALSE(ParseNativeMessage(
        R"({"protocol":1,"protocol":1,"browser":"firefox.exe","hostname":"voz.vn","route":"tsf","focused":true})",
        message, error));
    EXPECT_FALSE(ParseNativeMessage(
        R"({"protocol":2,"browser":"firefox.exe","hostname":"voz.vn","route":"default","focused":true})",
        message, error));
    EXPECT_FALSE(ParseNativeMessage(
        R"({"protocol":2,"browser":"firefox.exe","hostname":"voz.vn","route":"default","mode":"secret","focused":true})",
        message, error));
}

TEST(NativeMessagingTest, ReadsAndSerializesOwnedModeEvent) {
    BrowserContextState state{};
    state.modeEventSequence = 7;
    state.modeEventOwnerProcessId = 101;
    state.modeEventOwnerNonce = 1001;
    state.modeEventMode = BrowserMode::English;
    std::memcpy(state.modeEventHostname, "example.com", sizeof("example.com"));

    NativeModeEvent event;
    ASSERT_TRUE(TryReadModeEvent(state, 101, 1001, 6, event));
    EXPECT_EQ(event.sequence, 7u);
    EXPECT_EQ(event.hostname, "example.com");
    EXPECT_EQ(SerializeModeEvent(event),
        R"({"ok":true,"protocol":2,"event":"mode-changed","hostname":"example.com","mode":"english"})");
    EXPECT_FALSE(TryReadModeEvent(state, 101, 1001, 7, event));
    EXPECT_FALSE(TryReadModeEvent(state, 202, 2002, 6, event));
}

TEST(NativeMessagingTest, AllowsBlurMessageWithoutHostname) {
    NativeMessage message;
    std::string error;
    EXPECT_TRUE(ParseNativeMessage(
        R"({"protocol":1,"browser":"chrome.exe","hostname":"","route":"default","focused":false})",
        message, error)) << error;
}

} // namespace
} // namespace NextKey::BrowserHost
