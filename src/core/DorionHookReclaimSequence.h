// VKey - bounded delayed Dorion keyboard-hook reclaim state
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "DorionHookReclaimPolicy.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>

namespace NextKey {

struct DorionDelayedReclaimTicket {
    DorionProcessIdentity identity{};
    std::uintptr_t timerId{0};
    std::uint64_t dueTickMs{0};
    std::uint8_t remainingGateChecks{0};

    [[nodiscard]] bool IsValid() const noexcept {
        return identity.IsValid() && timerId != 0 && remainingGateChecks != 0;
    }
};

/// Hook-pump-owned fixed storage for the small number of concurrently starting
/// Dorion processes. Timer ID plus monotonic deadline fences a stale queued
/// WM_TIMER if Win32 recycles an ID after cancellation.
class DorionDelayedReclaimSlots {
public:
    static constexpr std::size_t kCapacity = 4;

    [[nodiscard]] bool Arm(DorionDelayedReclaimTicket ticket) noexcept {
        if (!ticket.IsValid()) return false;
        for (const auto& slot : slots_) {
            if (slot.identity == ticket.identity || slot.timerId == ticket.timerId) {
                return false;
            }
        }
        for (auto& slot : slots_) {
            if (!slot.IsValid()) {
                slot = ticket;
                return true;
            }
        }
        return false;
    }

    [[nodiscard]] std::optional<DorionDelayedReclaimTicket> TakeIfDue(
            std::uintptr_t timerId, std::uint64_t nowTickMs) noexcept {
        if (timerId == 0) return std::nullopt;
        for (auto& slot : slots_) {
            if (slot.timerId != timerId || nowTickMs < slot.dueTickMs) continue;
            const DorionDelayedReclaimTicket result = slot;
            slot = {};
            return result;
        }
        return std::nullopt;
    }

    [[nodiscard]] std::optional<DorionDelayedReclaimTicket> Cancel(
            DorionProcessIdentity identity) noexcept {
        if (!identity.IsValid()) return std::nullopt;
        for (auto& slot : slots_) {
            if (slot.identity != identity) continue;
            const DorionDelayedReclaimTicket result = slot;
            slot = {};
            return result;
        }
        return std::nullopt;
    }

    [[nodiscard]] bool HasPending(DorionProcessIdentity identity) const noexcept {
        if (!identity.IsValid()) return false;
        for (const auto& slot : slots_) {
            if (slot.identity == identity) return true;
        }
        return false;
    }

    [[nodiscard]] bool HasTimer(std::uintptr_t timerId) const noexcept {
        if (timerId == 0) return false;
        for (const auto& slot : slots_) {
            if (slot.IsValid() && slot.timerId == timerId) return true;
        }
        return false;
    }

    [[nodiscard]] bool HasAnyPending() const noexcept {
        for (const auto& slot : slots_) {
            if (slot.IsValid()) return true;
        }
        return false;
    }

    [[nodiscard]] std::array<std::uintptr_t, kCapacity>
    ActiveTimerIds() const noexcept {
        std::array<std::uintptr_t, kCapacity> ids{};
        for (std::size_t i = 0; i < kCapacity; ++i) ids[i] = slots_[i].timerId;
        return ids;
    }

    void Clear() noexcept { slots_.fill({}); }

private:
    std::array<DorionDelayedReclaimTicket, kCapacity> slots_{};
};

struct DorionReclaimQuietEvidence {
    bool lastInputQuerySucceeded{false};
    std::uint32_t nowTickMs{0};
    std::uint32_t lastInputTickMs{0};
    bool anyPhysicalKeyDown{false};
    bool syntheticDispatchActive{false};
    bool compositionActive{false};
};

inline constexpr std::uint32_t kDorionReclaimQuietMs = 100;

/// No replacement is attempted during active typing, a held shortcut, VKey's
/// synthetic output, or a live composition. DWORD subtraction intentionally
/// preserves GetTickCount/GetLastInputInfo wrap semantics; a delta above half
/// the range is treated as anomalous/future evidence and fails closed.
[[nodiscard]] inline bool IsDorionReclaimQuiet(
        const DorionReclaimQuietEvidence& evidence) noexcept {
    if (!evidence.lastInputQuerySucceeded || evidence.anyPhysicalKeyDown
        || evidence.syntheticDispatchActive || evidence.compositionActive) {
        return false;
    }
    const std::uint32_t elapsed =
        evidence.nowTickMs - evidence.lastInputTickMs;
    return elapsed <= static_cast<std::uint32_t>(
               (std::numeric_limits<std::int32_t>::max)())
        && elapsed >= kDorionReclaimQuietMs;
}

}  // namespace NextKey
