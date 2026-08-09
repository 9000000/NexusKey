// VKey - Pure TSF edit decisions
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <cstdint>

namespace NextKey {

// TS_SD_READONLY / TF_SD_READONLY from textstor.h. Keep the pure decision
// Windows-header-free so it can be covered by the cross-platform test target.
inline constexpr uint32_t kTsfReadOnlyDocumentFlag = 0x1u;

/// A read-only TSF document is often an empty text store exposed while focus is
/// on non-editable UI (for example a shell file list). Claiming an alpha key in
/// that context makes Windows open its fallback "Finalize the string" UI.
[[nodiscard]] constexpr bool IsReadOnlyTsfDocument(
    uint32_t dynamicStatusFlags) noexcept {
    return (dynamicStatusFlags & kTsfReadOnlyDocumentFlag) != 0;
}

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
