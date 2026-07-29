// VKey - Pure TSF edit decisions
// SPDX-License-Identifier: AGPL-3.0-only

#pragma once

#include <cstdint>

namespace NextKey {

/// A revive range is only safe when the backward shift covered exactly the
/// characters asked for — a partial shift would put the composition over the
/// wrong text.
[[nodiscard]] constexpr bool IsExactBackwardRangeShift(
    std::int32_t requestedChars, std::int32_t shiftedChars) noexcept {
    return requestedChars > 0 && shiftedChars == -requestedChars;
}

[[nodiscard]] constexpr bool ShouldSuppressClaimedKeyDown(
    uint32_t pendingVk, uint32_t currentVk, uint32_t lParam) noexcept {
    constexpr uint32_t kPreviousKeyState = 1u << 30;
    return pendingVk != 0
        && pendingVk == currentVk
        && (lParam & kPreviousKeyState) == 0;
}

}  // namespace NextKey
