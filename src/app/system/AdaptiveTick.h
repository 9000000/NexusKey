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
// idle RAM stays elevated. Stretching the tick to 1 s after 10 s idle and
// 5 s after 60 s idle reduces the churn — but backing off is NOT enough:
// touching pages every few seconds still resets the aging clock, so the
// working set never trims (measured: 5 s and even a 30 s bucket stayed flat
// ~1.6 MB; v2.1.24, which had no worker thread, aged to ~0.3 MB at idle).
//
// The fix is to STOP entirely after deep idle: ComputeTickInterval returns
// 0 ms above kIdleStopThreshMs, which the owner maps to SetTickInterval(0)
// → the worker blocks on cv_.wait (∞), touches no pages, and Windows trims
// the working set (v2.1.24 parity). The next keystroke resumes the cadence
// via HookEngine::MarkActivity → workerSignalFn_ (the detector / layout poll
// don't need to run while there is no typing). See
// docs/plans/2026-05-27-adaptive-tick-idle-backoff.md for full rationale.

#pragma once

#include <chrono>
#include <cstdint>

namespace NextKey {

// Tick intervals (milliseconds). Three buckets.
inline constexpr std::uint32_t kTickActiveMs    = 200;   // active typing / interaction
inline constexpr std::uint32_t kTickIdleShortMs = 1000;  // brief pause (reading, thinking)
inline constexpr std::uint32_t kTickIdleLongMs  = 5000;  // AFK / sustained idle

// Idle thresholds (milliseconds since last MarkActivity).
inline constexpr std::uint64_t kIdleShortThreshMs = 10000;   // > 10 s idle → short bucket
inline constexpr std::uint64_t kIdleLongThreshMs  = 60000;   // > 60 s idle → long bucket
inline constexpr std::uint64_t kIdleStopThreshMs  = 120000;  // > 120 s idle → STOP (park, trim)

/// Given milliseconds since last user activity, return the tick interval the
/// MainThreadWorker should use. Pure function; no globals, no Win32. Boundary
/// behavior pinned by tests/AdaptiveTickTest.cpp.
///
/// A return of **0 ms means STOP** — the owner maps it to SetTickInterval(0),
/// the worker parks on cv_.wait (∞), and Windows trims the working set. The
/// next keystroke resumes the cadence (MarkActivity → Signal). This is the
/// only bucket that actually lets the working set trim; the positive buckets
/// just reduce churn.
[[nodiscard]] constexpr std::chrono::milliseconds
ComputeTickInterval(std::uint64_t idleMs) noexcept {
    if (idleMs < kIdleShortThreshMs) {
        return std::chrono::milliseconds(kTickActiveMs);
    }
    if (idleMs < kIdleLongThreshMs) {
        return std::chrono::milliseconds(kTickIdleShortMs);
    }
    if (idleMs < kIdleStopThreshMs) {
        return std::chrono::milliseconds(kTickIdleLongMs);
    }
    return std::chrono::milliseconds(0);  // deep idle → STOP (park; WS trims)
}

}  // namespace NextKey
