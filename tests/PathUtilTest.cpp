// VKey - PathUtil tests
// SPDX-License-Identifier: AGPL-3.0-only
//
// Locks PathBasename — the testable home for path->basename normalization that
// ExcludedAppsDialog::addApp (#209 browse-to-add) and Logger route through.

#include <gtest/gtest.h>
#include "core/PathUtil.h"

namespace NextKey {
namespace {

TEST(PathUtilTest, BackslashPath) {
    EXPECT_EQ(PathBasename(L"C:\\Windows\\System32\\Taskmgr.exe"), L"Taskmgr.exe");
}

TEST(PathUtilTest, ForwardSlashPath) {
    EXPECT_EQ(PathBasename(L"/usr/local/bin/foo"), L"foo");
}

TEST(PathUtilTest, MixedSeparators) {
    EXPECT_EQ(PathBasename(L"C:/Program Files\\VKey\\VKey.exe"), L"VKey.exe");
}

TEST(PathUtilTest, NoSeparator_ReturnsWhole) {
    EXPECT_EQ(PathBasename(L"notepad.exe"), L"notepad.exe");
}

TEST(PathUtilTest, Empty) {
    EXPECT_EQ(PathBasename(L""), L"");
}

TEST(PathUtilTest, TrailingSeparator_Empty) {
    EXPECT_EQ(PathBasename(L"C:\\dir\\"), L"");
}

TEST(PathUtilTest, OnlySeparator_Empty) {
    EXPECT_EQ(PathBasename(L"\\"), L"");
}

}  // namespace
}  // namespace NextKey
