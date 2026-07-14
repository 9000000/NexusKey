// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace NextKey {

// Sliding-window restart throttle for the watchdog supervisor.
//
// Pure logic, no platform deps → unit-testable on Linux (see RestartThrottleTest).
//
// Rationale (2026-07-01 Kaspersky incident): when a persistent kill source — AV
// quarantine, a deleted/corrupt binary, a boot-loop crash — takes VKey down, an
// unconditional respawn loop reads as malware self-defense to behavior-detection
// engines and escalates the verdict. After `Cap` restarts within `windowMs`, the
// supervisor must STOP respawning: a repeated kill is not a transient crash, and
// the watchdog fighting it only makes things worse. Recovery is by the user
// starting VKey again (which relaunches the watchdog), not by hammering.
template <std::size_t Cap>
class RestartThrottle {
public:
    explicit constexpr RestartThrottle(uint64_t windowMs) noexcept
        : windowMs_(windowMs) {}

    // Records a restart attempt at `nowMs` (a monotonic clock, e.g.
    // GetTickCount64). Returns true if the attempt is under the cap for the
    // trailing window; false once `Cap` attempts have occurred within `windowMs`.
    // `nowMs` is assumed monotonic non-decreasing across calls.
    [[nodiscard]] bool AllowRestart(uint64_t nowMs) noexcept {
        // Compact out timestamps older than the trailing window.
        std::size_t kept = 0;
        for (std::size_t i = 0; i < count_; ++i) {
            if (times_[i] + windowMs_ > nowMs) {
                times_[kept++] = times_[i];
            }
        }
        count_ = kept;

        if (count_ >= Cap) return false;  // window full → refuse (never overflows)
        times_[count_++] = nowMs;
        return true;
    }

    [[nodiscard]] std::size_t RecentCount() const noexcept { return count_; }

private:
    std::array<uint64_t, Cap> times_{};
    std::size_t count_ = 0;
    uint64_t windowMs_;
};

}  // namespace NextKey
