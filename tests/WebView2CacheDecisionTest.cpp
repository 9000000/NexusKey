// Tests for IsWebView2NegativeCacheFresh — pure WebView2 negative-cache TTL rule.
// FocusOwner::IsWebView2App is Windows-only (CreateToolhelp32Snapshot); the
// freshness decision is extracted so the TTL behaviour is verified on Linux.
// SPDX-License-Identifier: AGPL-3.0-only

#include <gtest/gtest.h>

#include <cstdint>

#include "core/WebView2CacheDecision.h"

using NextKey::IsWebView2NegativeCacheFresh;

namespace {
constexpr std::uint64_t kTtl = 30'000;  // mirrors FocusOwner::kWebView2NegativeTtlMs
}

TEST(WebView2NegativeCache, JustCachedIsFresh) {
    EXPECT_TRUE(IsWebView2NegativeCacheFresh(/*now*/ 1000, /*cached*/ 1000, kTtl));
}

TEST(WebView2NegativeCache, WithinTtlIsFresh) {
    EXPECT_TRUE(IsWebView2NegativeCacheFresh(1000 + (kTtl - 1), 1000, kTtl));
}

TEST(WebView2NegativeCache, ExactlyTtlIsStale) {
    // Boundary: elapsed == ttl is NOT fresh (strict <) — re-scan at the edge.
    EXPECT_FALSE(IsWebView2NegativeCacheFresh(1000 + kTtl, 1000, kTtl));
}

TEST(WebView2NegativeCache, BeyondTtlIsStale) {
    EXPECT_FALSE(IsWebView2NegativeCacheFresh(1000 + kTtl + 5000, 1000, kTtl));
}

TEST(WebView2NegativeCache, ClockGoingBackwardsIsStale) {
    // Defensive: if cached > now (clock anomaly), unsigned wrap → huge elapsed →
    // stale → safe re-scan rather than serving a stale negative forever.
    EXPECT_FALSE(IsWebView2NegativeCacheFresh(/*now*/ 500, /*cached*/ 1000, kTtl));
}

TEST(WebView2NegativeCache, ZeroTtlNeverFresh) {
    // A zero TTL degenerates to "always re-scan" (original pre-cache behaviour).
    EXPECT_FALSE(IsWebView2NegativeCacheFresh(1000, 1000, /*ttl*/ 0));
}

TEST(WebView2NegativeCache, IsConstexpr) {
    // The predicate must be usable in constant expressions (zero runtime cost
    // when both operands are known) — assert it compiles in a static_assert.
    static_assert(IsWebView2NegativeCacheFresh(10, 5, 100));
    static_assert(!IsWebView2NegativeCacheFresh(200, 5, 100));
    SUCCEED();
}
