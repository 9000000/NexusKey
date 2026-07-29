// VKey - Pure TSF edit decisions
// SPDX-License-Identifier: AGPL-3.0-only

#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>

namespace NextKey {

[[nodiscard]] constexpr bool IsExactBackwardRangeShift(
    std::size_t requestedChars, std::int32_t shiftedChars) noexcept {
    return requestedChars <=
            static_cast<std::size_t>((std::numeric_limits<std::int32_t>::max)())
        && shiftedChars == -static_cast<std::int32_t>(requestedChars);
}

[[nodiscard]] constexpr bool ShouldSuppressClaimedPrintableKeyDown(
    uint32_t pendingVk, uint32_t currentVk, uint32_t lParam) noexcept {
    constexpr uint32_t kPreviousKeyState = 1u << 30;
    return pendingVk != 0
        && pendingVk == currentVk
        && (lParam & kPreviousKeyState) == 0;
}

}  // namespace NextKey
