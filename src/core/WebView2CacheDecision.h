// VKey - Pure WebView2 negative-cache freshness predicate
// SPDX-License-Identifier: AGPL-3.0-only
//
// FocusOwner::IsWebView2App is Win32-only (CreateToolhelp32Snapshot). The TTL
// rule that decides whether a cached "not WebView2" verdict may still be reused
// is extracted here as a pure function so it is unit-tested on Linux, mirroring
// core/PerAppModeDecision.h.

#pragma once

#include <cstdint>

namespace NextKey {

/// True when a cached negative ("not a WebView2 host") verdict recorded at
/// `cachedMs` is still fresh at `nowMs` under a `ttlMs` time-to-live, i.e. it
/// may be reused to skip the ~150 ms cross-process module/process snapshot.
///
/// Both ticks come from the same monotonic source (GetTickCount64), so the
/// unsigned subtraction is well-defined for the expected `nowMs >= cachedMs`.
/// If a clock anomaly ever yields `cachedMs > nowMs`, the unsigned wrap makes
/// the result enormous → `>= ttlMs` → "stale" → re-scan, which is the safe
/// failure (re-detect rather than serve a stale negative).
[[nodiscard]] constexpr bool IsWebView2NegativeCacheFresh(std::uint64_t nowMs,
                                                          std::uint64_t cachedMs,
                                                          std::uint64_t ttlMs) noexcept {
    return (nowMs - cachedMs) < ttlMs;
}

}  // namespace NextKey
