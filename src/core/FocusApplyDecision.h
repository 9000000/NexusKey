// VKey - delayed focus-classification apply decision
// SPDX-License-Identifier: GPL-3.0-only
//
// Focus classification is intentionally asynchronous: WinEvent publishes a
// request, the worker performs the heavy HWND/app probes, then the hook thread
// consumes the immutable result. Physical input can overtake that worker
// round-trip. Applying such a result in the middle of a live word destroys the
// engine-side composition while the host UI keeps the already-rendered text.
//
// Keep the ordering policy pure and Linux-testable. The caller owns all side
// effects (drop the snapshot, retain it in a hook-owned deferred slot, or apply
// the complete typing-context transaction).

#pragma once

#include <cstdint>

namespace NextKey {

enum class FocusApplyDisposition : std::uint8_t {
    DropStale,
    DeferUntilBoundary,
    ApplyNow,
};

struct FocusApplyInputs {
    std::uint64_t snapshotRequestSerial{0};
    std::uint64_t latestRequestSerial{0};
    std::uint64_t snapshotInputEpoch{0};
    std::uint64_t currentInputEpoch{0};
    bool hasComposition{false};
};

struct TsfFocusActivationState {
    std::uintptr_t lastTsfHwndOpaque{0};
};

/// Pure, allocation-free and syscall-free. Equality is intentional:
/// request serials identify the latest focus event, while input epochs answer
/// whether any physical key-down began after that event.
[[nodiscard]] constexpr FocusApplyDisposition
DecideFocusApply(const FocusApplyInputs& in) noexcept {
    if (in.snapshotRequestSerial != in.latestRequestSerial) {
        return FocusApplyDisposition::DropStale;
    }
    if (in.hasComposition && in.snapshotInputEpoch != in.currentInputEpoch) {
        return FocusApplyDisposition::DeferUntilBoundary;
    }
    return FocusApplyDisposition::ApplyNow;
}

/// Re-assert the VKey TIP once per TSF focus target. Poll-based
/// reclassification of the same HWND must not repeat the expensive profile
/// activation, while leaving TSF mode rearms the same target for a later visit.
[[nodiscard]] constexpr bool ShouldActivateTsfProfileForFocus(
        TsfFocusActivationState& state,
        bool isTsf,
        std::uintptr_t hwndOpaque) noexcept {
    if (!isTsf || hwndOpaque == 0) {
        state.lastTsfHwndOpaque = 0;
        return false;
    }

    const bool shouldActivate = state.lastTsfHwndOpaque != hwndOpaque;
    state.lastTsfHwndOpaque = hwndOpaque;
    return shouldActivate;
}

}  // namespace NextKey
