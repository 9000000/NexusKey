// NexusKey - UpdateSecurity Unit Tests
// SPDX-License-Identifier: GPL-3.0-only

#include <gtest/gtest.h>
#include "app/system/UpdateSecurity.h"

using namespace NextKey;

// ── SEC-002: PowerShell escaping ───────────────────────────────────────────

TEST(EscapePowerShellSingleQuoteTest, NoQuotes) {
    EXPECT_EQ(EscapePowerShellSingleQuote(L"C:\\Users\\Admin\\file.zip"),
              L"C:\\Users\\Admin\\file.zip");
}

TEST(EscapePowerShellSingleQuoteTest, SingleQuoteInUsername) {
    EXPECT_EQ(EscapePowerShellSingleQuote(L"C:\\Users\\O'Brien\\AppData\\file.zip"),
              L"C:\\Users\\O''Brien\\AppData\\file.zip");
}

TEST(EscapePowerShellSingleQuoteTest, MultipleQuotes) {
    EXPECT_EQ(EscapePowerShellSingleQuote(L"it's a 'test'"),
              L"it''s a ''test''");
}

TEST(EscapePowerShellSingleQuoteTest, EmptyString) {
    EXPECT_EQ(EscapePowerShellSingleQuote(L""), L"");
}

TEST(EscapePowerShellSingleQuoteTest, OnlyQuotes) {
    EXPECT_EQ(EscapePowerShellSingleQuote(L"'''"), L"''''''");
}

TEST(EscapePowerShellSingleQuoteTest, ConsecutiveQuotes) {
    EXPECT_EQ(EscapePowerShellSingleQuote(L"a''b"), L"a''''b");
}

// ── SEC-003: URL domain validation (wide) ──────────────────────────────────

TEST(IsAllowedDownloadUrlWideTest, GitHubReleasesUrl) {
    EXPECT_TRUE(IsAllowedDownloadUrl(
        L"https://github.com/phatMT97/NextKey/releases/download/v2.0.0/NexusKey-x64.zip"));
}

TEST(IsAllowedDownloadUrlWideTest, GitHubObjectsUrl) {
    EXPECT_TRUE(IsAllowedDownloadUrl(
        L"https://objects.githubusercontent.com/github-production-release-asset/12345/abc.zip"));
}

TEST(IsAllowedDownloadUrlWideTest, GitHubCodeloadUrl) {
    EXPECT_TRUE(IsAllowedDownloadUrl(
        L"https://codeload.github.com/phatMT97/NextKey/zip/refs/tags/v2.0.0"));
}

TEST(IsAllowedDownloadUrlWideTest, RejectsArbitraryDomain) {
    EXPECT_FALSE(IsAllowedDownloadUrl(L"https://evil.com/NexusKey-x64.zip"));
}

TEST(IsAllowedDownloadUrlWideTest, RejectsHttp) {
    EXPECT_FALSE(IsAllowedDownloadUrl(L"http://github.com/foo/bar.zip"));
}

TEST(IsAllowedDownloadUrlWideTest, RejectsSimilarDomain) {
    EXPECT_FALSE(IsAllowedDownloadUrl(L"https://github.com.evil.com/foo.zip"));
}

TEST(IsAllowedDownloadUrlWideTest, RejectsEmpty) {
    EXPECT_FALSE(IsAllowedDownloadUrl(std::wstring{}));
}

TEST(IsAllowedDownloadUrlWideTest, CaseInsensitive) {
    EXPECT_TRUE(IsAllowedDownloadUrl(
        L"HTTPS://GITHUB.COM/phatMT97/NextKey/releases/download/v2.0.0/NexusKey-x64.zip"));
}

// ── SEC-003: URL domain validation (narrow) ────────────────────────────────

TEST(IsAllowedDownloadUrlNarrowTest, GitHubReleasesUrl) {
    EXPECT_TRUE(IsAllowedDownloadUrl(
        std::string("https://github.com/phatMT97/NextKey/releases/download/v2.0.0/NexusKey-x64.zip")));
}

TEST(IsAllowedDownloadUrlNarrowTest, RejectsArbitraryDomain) {
    EXPECT_FALSE(IsAllowedDownloadUrl(std::string("https://evil.com/NexusKey-x64.zip")));
}

TEST(IsAllowedDownloadUrlNarrowTest, RejectsSubdomain) {
    EXPECT_FALSE(IsAllowedDownloadUrl(std::string("https://github.com.evil.com/foo.zip")));
}

TEST(IsAllowedDownloadUrlNarrowTest, CaseInsensitive) {
    EXPECT_TRUE(IsAllowedDownloadUrl(
        std::string("HTTPS://GITHUB.COM/phatMT97/NextKey/releases/download/v2.0.0/NexusKey-x64.zip")));
}
