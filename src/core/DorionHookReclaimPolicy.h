// VKey - exact-Dorion bounded keyboard-hook reclaim policy
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace NextKey {

/// Numeric PID alone is not a process-lifetime identity because Windows may
/// reuse it. `creationTime` is the full nonzero GetProcessTimes creation
/// FILETIME, queried again immediately before hook replacement.
struct DorionProcessIdentity {
    std::uint32_t pid{0};
    std::uint64_t creationTime{0};

    [[nodiscard]] constexpr bool IsValid() const noexcept {
        return pid != 0 && creationTime != 0;
    }

    friend constexpr bool operator==(
        const DorionProcessIdentity&, const DorionProcessIdentity&) = default;
};

/// Allows one terminally successful compatibility replacement per verified
/// Dorion process lifetime. Both pending and successful histories are fixed
/// arrays: Observe can run while a focus result is drained from an LL callback
/// and must never allocate there.
class DorionHookReclaimPolicy {
public:
    static constexpr std::size_t kMaxPending = 4;
    static constexpr std::size_t kMaxSuccessful = 8;

    [[nodiscard]] bool Observe(std::wstring_view exeName,
                               DorionProcessIdentity identity,
                               bool isCurrent) noexcept {
        if (!isCurrent || exeName != L"dorion.exe" || !identity.IsValid()) {
            return false;
        }
        if (Contains(pending_, identity) || Contains(successful_, identity)
            || suppressNewIdentities_) {
            return false;
        }
        for (auto& slot : pending_) {
            if (!slot.IsValid()) {
                slot = identity;
                return true;
            }
        }
        // More than four concurrently-starting Dorion processes is outside the
        // compatibility contract. Fail closed without allocating or evicting a
        // live pending identity (which could permit duplicate hook churn).
        return false;
    }

    void Complete(DorionProcessIdentity identity, bool succeeded) noexcept {
        Erase(pending_, identity);
        if (!succeeded || !identity.IsValid()
            || Contains(successful_, identity)) {
            return;
        }
        for (auto& slot : successful_) {
            if (!slot.IsValid()) {
                slot = identity;
                return;
            }
        }
        // Never forget a completed identity merely to make room: doing so
        // could rehook it again during Alt+Tab churn. Suppress future new ones
        // for this VKey run if the fixed history is exhausted.
        suppressNewIdentities_ = true;
    }

    void Reset() noexcept {
        pending_.fill({});
        successful_.fill({});
        suppressNewIdentities_ = false;
    }

private:
    template <std::size_t N>
    [[nodiscard]] static bool Contains(
            const std::array<DorionProcessIdentity, N>& identities,
            DorionProcessIdentity needle) noexcept {
        for (const auto identity : identities) {
            if (identity == needle) return true;
        }
        return false;
    }

    template <std::size_t N>
    static void Erase(std::array<DorionProcessIdentity, N>& identities,
                      DorionProcessIdentity needle) noexcept {
        for (auto& identity : identities) {
            if (identity == needle) {
                identity = {};
                return;
            }
        }
    }

    std::array<DorionProcessIdentity, kMaxPending> pending_{};
    std::array<DorionProcessIdentity, kMaxSuccessful> successful_{};
    bool suppressNewIdentities_{false};
};

}  // namespace NextKey
