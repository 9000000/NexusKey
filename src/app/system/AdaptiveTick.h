// AdaptiveTick.h - idle-backoff cadence for MainThreadWorker.
// SPDX-License-Identifier: AGPL-3.0-only
//
// Pure C++, Linux-portable. Maps "milliseconds since last user activity"
// to the desired MainThreadWorker tick interval. Used by
// HookEngine::OnTickPoll and HookEngine::RetuneCadenceIfNeeded.
//
// Why backoff: MainThreadWorker tick (200 ms default) touches a small set
// of pages every iteration (atomic state, mailbox, std::function vtable,
// CV internals). Windows working-set manager can't age these pages out, so
// idle RAM stays elevated. Stretching the tick to 1 s after 10 s idle,
// 5 s after 60 s idle, and 30 s after 5 min idle lets the aging counter
// advance past the working-set trim threshold (~30 s).
//
// 5 s tick was insufficient — empirically (2026-05-28 benchmark) v3.0.0
// Modern + Classic both retained ~0.49 MB private working set vs v2.1.24
// because the 5 s cadence still woke MainThreadWorker faster than aging
// could age out pages. 30 s at deep idle puts the worker fully past the
// trim threshold.
//
// Trade-off at the 30 s bucket: `OnTickPoll`'s background work
// (histogram flush, deferred config reload, PID-change fallback,
// CheckLayoutChange post) can lag up to 30 s when the user is AFK for
// >5 min. Mailbox post + cv_.notify_all from `MarkActivity` /
// `SetTickInterval` wakes the worker immediately on any keystroke or
// cadence retune, so user-visible latency is unaffected.
//
// See docs/plans/2026-05-27-adaptive-tick-idle-backoff.md for full rationale.

#pragma once

#include <chrono>
#include <cstdint>

namespace NextKey {

// Tick intervals (milliseconds). Four buckets.
inline constexpr std::uint32_t kTickActiveMs    = 200;    // active typing / interaction
inline constexpr std::uint32_t kTickIdleShortMs = 1000;   // brief pause (reading, thinking)
inline constexpr std::uint32_t kTickIdleLongMs  = 5000;   // sustained idle (1–5 min)
inline constexpr std::uint32_t kTickAfkDeepMs   = 30000;  // deep AFK (>5 min) — past WS aging threshold

// Idle thresholds (milliseconds since last MarkActivity).
inline constexpr std::uint64_t kIdleShortThreshMs = 10000;   // > 10 s idle → short bucket
inline constexpr std::uint64_t kIdleLongThreshMs  = 60000;   // > 60 s idle → long bucket
inline constexpr std::uint64_t kAfkDeepThreshMs   = 300000;  // > 5 min idle → AFK-deep bucket

/// Given milliseconds since last user activity, return the tick interval the
/// MainThreadWorker should use. Pure function; no globals, no Win32. Boundary
/// behavior pinned by tests/AdaptiveTickTest.cpp.
[[nodiscard]] constexpr std::chrono::milliseconds
ComputeTickInterval(std::uint64_t idleMs) noexcept {
    if (idleMs < kIdleShortThreshMs) {
        return std::chrono::milliseconds(kTickActiveMs);
    }
    if (idleMs < kIdleLongThreshMs) {
        return std::chrono::milliseconds(kTickIdleShortMs);
    }
    if (idleMs < kAfkDeepThreshMs) {
        return std::chrono::milliseconds(kTickIdleLongMs);
    }
    return std::chrono::milliseconds(kTickAfkDeepMs);
}

}  // namespace NextKey
