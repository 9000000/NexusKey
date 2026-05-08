// NexusKey Watchdog - Rapid-Kill Detector
// SPDX-License-Identifier: GPL-3.0-only
//
// User-quit signal: if the user kills NexusKey twice in quick succession,
// treat the second kill as a request to stop the watchdog itself (so the
// app does NOT auto-restart). The detector returns true when the elapsed
// time since the previous successful respawn is under the threshold —
// caller exits the watchdog loop on true.

#pragma once

#include <cstdint>

namespace NextKey {

inline constexpr uint32_t RAPID_KILL_THRESHOLD_MS = 60'000;

/// Returns true if a respawn is happening within `thresholdMs` of the
/// previous one (lastSpawnTime in GetTickCount() ms units). Uses unsigned
/// subtraction so it stays correct across the GetTickCount() wraparound
/// (~49.7 days). lastSpawnTime == 0 means "never spawned" and returns
/// false unconditionally.
[[nodiscard]] inline bool ShouldStopOnRapidKill(uint32_t lastSpawnTime,
                                                uint32_t now,
                                                uint32_t thresholdMs = RAPID_KILL_THRESHOLD_MS) noexcept {
    if (lastSpawnTime == 0) return false;
    const uint32_t elapsed = now - lastSpawnTime;
    return elapsed < thresholdMs;
}

}  // namespace NextKey
