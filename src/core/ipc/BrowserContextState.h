// VKey - Browser extension routing state
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace NextKey {

enum class BrowserRoute : std::uint8_t {
    Default = 0,
    ForceEnglish = 1,
    ForceTsf = 2,
};

inline constexpr std::uint32_t kBrowserContextMagic = 0x42594B56u; // "VKYB"
inline constexpr std::uint16_t kBrowserContextVersion = 1;
inline constexpr std::uint64_t kBrowserContextTtlMs = 5'000;

/// Fixed-size, same-user shared-memory contract between VKeyBrowserHost.exe
/// and VKeyApp/VKeyClassic. It deliberately contains only the effective route
/// and the active hostname; URLs, paths, queries and typed keys never cross the
/// native-messaging boundary.
struct alignas(8) BrowserContextState {
    constexpr BrowserContextState() noexcept;

    std::uint32_t generation{0}; // seqlock: odd while a writer owns the fields
    std::uint32_t magic{kBrowserContextMagic};
    std::uint16_t version{kBrowserContextVersion};
    std::uint16_t structSize{0};
    std::uint32_t ownerProcessId{0};
    std::uint64_t ownerNonce{0};
    std::uint64_t updatedTickMs{0};
    std::uint8_t focused{0};
    BrowserRoute route{BrowserRoute::Default};
    std::uint8_t reserved[6]{};
    char browserExe[32]{};
    char hostname[256]{};

    [[nodiscard]] bool HasValidHeader() const noexcept {
        return magic == kBrowserContextMagic
            && version == kBrowserContextVersion
            && structSize == sizeof(BrowserContextState);
    }
};

constexpr BrowserContextState::BrowserContextState() noexcept
    : structSize(static_cast<std::uint16_t>(sizeof(BrowserContextState))) {}

static_assert(offsetof(BrowserContextState, generation) == 0);
static_assert(sizeof(BrowserContextState) <= 384);

[[nodiscard]] inline bool ReadBrowserContextSeqlock(
        const volatile BrowserContextState* source,
        BrowserContextState& out) noexcept {
    if (source == nullptr) return false;
    for (int attempt = 0; attempt < 3; ++attempt) {
        const std::uint32_t before = source->generation;
        std::atomic_thread_fence(std::memory_order_acquire);
        if ((before & 1u) != 0) continue;

        out.generation = before;
        out.magic = source->magic;
        out.version = source->version;
        out.structSize = source->structSize;
        out.ownerProcessId = source->ownerProcessId;
        out.ownerNonce = source->ownerNonce;
        out.updatedTickMs = source->updatedTickMs;
        out.focused = source->focused;
        out.route = source->route;
        for (std::size_t i = 0; i < sizeof(out.reserved); ++i)
            out.reserved[i] = source->reserved[i];
        for (std::size_t i = 0; i < sizeof(out.browserExe); ++i)
            out.browserExe[i] = source->browserExe[i];
        for (std::size_t i = 0; i < sizeof(out.hostname); ++i)
            out.hostname[i] = source->hostname[i];

        std::atomic_thread_fence(std::memory_order_acquire);
        if (before == source->generation) return out.HasValidHeader();
    }
    return false;
}

[[nodiscard]] inline bool IsBrowserContextApplicable(
        const BrowserContextState& state,
        const char* foregroundExe,
        std::uint64_t nowTickMs) noexcept {
    if (!state.HasValidHeader() || state.focused == 0
        || state.route == BrowserRoute::Default || foregroundExe == nullptr)
        return false;
    if (nowTickMs < state.updatedTickMs
        || nowTickMs - state.updatedTickMs > kBrowserContextTtlMs)
        return false;
    return std::strcmp(state.browserExe, foregroundExe) == 0;
}

[[nodiscard]] constexpr bool ResolveEffectiveTsf(
        bool configuredTsf, bool excluded, bool tsfFeatureEnabled,
        BrowserRoute route) noexcept {
    if (excluded || route == BrowserRoute::ForceEnglish) return false;
    if (route == BrowserRoute::ForceTsf) return tsfFeatureEnabled;
    return configuredTsf;
}

} // namespace NextKey
