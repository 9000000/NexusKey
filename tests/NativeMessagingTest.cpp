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
