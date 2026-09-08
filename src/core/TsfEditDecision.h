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

// TS_SS_TRANSITORY / TF_SS_TRANSITORY from textstor.h.
inline constexpr uint32_t kTsfTransitoryDocumentFlag = 0x4u;

struct TsfContextInputState {
    bool hasContext = false;
    uint32_t dynamicStatusFlags = 0;
    uint32_t staticStatusFlags = 0;
    bool keyboardDisabled = false;
    bool emptyContext = false;
    bool scopeBlocked = false;
};

/// TRANSITORY describes a short-lived document, not whether typing is allowed.
/// In particular, a writable composition-only context may have static=0x4.
/// Follow the explicit TSF input gates instead. EMPTYCONTEXT is a compartment
/// flag supplied by TSF/the host, not a check for an empty document string.
/// https://learn.microsoft.com/windows/win32/tsf/predefined-compartments
[[nodiscard]] constexpr bool ShouldBlockTsfContext(
    const TsfContextInputState& state) noexcept {
    return !state.hasContext
        || IsReadOnlyTsfDocument(state.dynamicStatusFlags)
        || state.keyboardDisabled
        || state.emptyContext
        || state.scopeBlocked;
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
