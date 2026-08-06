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

}  // namespace NextKey
