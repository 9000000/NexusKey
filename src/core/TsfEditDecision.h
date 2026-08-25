// VKey - Pure TSF edit decisions
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <cstdint>

namespace NextKey {

[[nodiscard]] constexpr bool ShouldApplyPreeditDisplayAttribute(
    bool hidePreeditUnderline) noexcept {
    return hidePreeditUnderline;
}

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

/// Windows opens its own floating composition box ("Finalize the string") when a
/// text service composes in a context that has nowhere to render the composition
/// — a shell list view, or a window whose focus is not a text control at all.
/// Those contexts report TS_SS_TRANSITORY and nothing else; real text stores
/// either pair it with another static flag or never set it, so only an exact
/// match may block input (#242 logs: Explorer's SysListView32 static=0x4
/// dyn=0x80000000, One Commander's WPF window static=0x4 dyn=0; against
/// Chromium 0x4|0x8, Firefox 0x8, WPF 0x2 — Chromium sets TRANSITORY on every
/// text store, editable ones included, so `& kTsfTransitoryDocumentFlag` would
/// kill typing in Edge, Chrome and Electron).
[[nodiscard]] constexpr bool IsTransitoryOnlyTsfDocument(
    uint32_t staticStatusFlags) noexcept {
    return staticStatusFlags == kTsfTransitoryDocumentFlag;
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
