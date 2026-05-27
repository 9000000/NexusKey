// AdaptiveTick.h - idle-backoff cadence for MainThreadWorker.
// SPDX-License-Identifier: GPL-3.0-only
//
// Pure C++, Linux-portable. Maps "milliseconds since last user activity"
// to the desired MainThreadWorker tick interval. Used by
// HookEngine::OnTickPoll and HookEngine::RetuneCadenceIfNeeded.
//
// Why backoff: MainThreadWorker tick (200 ms default) touches a small set
// of pages every iteration (atomic state, mailbox, std::function vtable,
// CV internals). Windows working-set manager can't age these pages out, so
// idle RAM stays elevated. Stretching the tick to 1 s after 10 s idle and
// 5 s after 60 s idle lets the aging counter advance past the trim threshold.
//
// See docs/plans/2026-05-27-adaptive-tick-idle-backoff.md for full rationale.

#pragma once

#include <chrono>
#include <cstdint>

namespace NextKey {

// Tick intervals (milliseconds). Three buckets.
inline constexpr std::uint32_t kTickActiveMs    = 200;   // active typing / interaction
inline constexpr std::uint32_t kTickIdleShortMs = 1000;  // brief pause (reading, thinking)
inline constexpr std::uint32_t kTickIdleLongMs  = 5000;  // AFK / sustained idle

// Idle thresholds (milliseconds since last MarkActivity).
inline constexpr std::uint64_t kIdleShortThreshMs = 10000;  // > 10 s idle → short bucket
inline constexpr std::uint64_t kIdleLongThreshMs  = 60000;  // > 60 s idle → long bucket

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
    return std::chrono::milliseconds(kTickIdleLongMs);
}

}  // namespace NextKey
