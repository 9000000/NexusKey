// VKey - Advanced engine download policy tests
// SPDX-License-Identifier: AGPL-3.0-only

#include "app/system/AdvancedEngineDownloadPolicy.h"
#include "core/Version.h"

#include <gtest/gtest.h>

using namespace NextKey;

TEST(AdvancedEngineDownloadPolicyTest, UsesCurrentReleaseTagAndExactAssetName) {
    EXPECT_EQ(BuildAdvancedEngineReleaseUrl(), std::wstring(L"https://github.com/phatMT97/VKey/releases/download/v") +
                                                   VKEY_VERSION_WSTR + L"/vkey_engine.dll");
}

TEST(AdvancedEngineDownloadPolicyTest, AllowsOnlyRequiredHttpsOrigins) {
    EXPECT_TRUE(IsAllowedAdvancedEngineUrl(L"https://github.com/phatMT97/VKey/releases/"
                                           L"download/v4.2.0/vkey_engine.dll"));
    EXPECT_TRUE(IsAllowedAdvancedEngineUrl(L"https://objects.githubusercontent.com/"
                                           L"github-production-release-asset/test"));
    EXPECT_TRUE(IsAllowedAdvancedEngineUrl(L"HTTPS://RELEASE-ASSETS.GITHUBUSERCONTENT.COM/"
                                           L"github-production-release-asset/test"));

    EXPECT_FALSE(IsAllowedAdvancedEngineUrl(L"http://github.com/phatMT97/VKey/releases/"
                                            L"download/v4.2.0/vkey_engine.dll"));
    EXPECT_FALSE(IsAllowedAdvancedEngineUrl(L"https://github.com.evil.example/vkey_engine.dll"));
    EXPECT_FALSE(IsAllowedAdvancedEngineUrl(L"https://github.com@evil.example/vkey_engine.dll"));
    EXPECT_FALSE(IsAllowedAdvancedEngineUrl(L"https://release-assets.githubusercontent.com.evil.example/"
                                            L"vkey_engine.dll"));
    EXPECT_FALSE(IsAllowedAdvancedEngineUrl(L"https://github.com:443/phatMT97/VKey/vkey_engine.dll"));
}
