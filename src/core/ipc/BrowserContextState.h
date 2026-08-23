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

enum class BrowserMode : std::uint8_t {
    Default = 0,
    Vietnamese = 1,
    English = 2,
};

inline constexpr std::uint32_t kBrowserContextMagic = 0x42594B56u; // "VKYB"
inline constexpr std::uint16_t kBrowserContextVersion = 2;
inline constexpr std::uint64_t kBrowserContextTtlMs = 5'000;

/// Fixed-size, same-user shared-memory contract between VKeyBrowserHost.exe
/// and VKeyApp/VKeyClassic. It deliberately contains only the hard route,
/// session V/E preference, active hostname, and the result of an accepted mode
/// toggle. URLs, paths, queries and typed keys never cross the boundary.
struct alignas(8) BrowserContextState {
    constexpr BrowserContextState() noexcept;

    std::uint32_t generation{0}; // seqlock: odd while a writer owns the fields
    std::uint32_t magic{kBrowserContextMagic};
    std::uint16_t version{kBrowserContextVersion};
    std::uint16_t structSize{0};
    std::uint32_t ownerProcessId{0};
    std::uint64_t ownerNonce{0};
    std::uint64_t updatedTickMs{0};
    std::uint32_t modeEventSequence{0};
    std::uint32_t modeEventOwnerProcessId{0};
    std::uint64_t modeEventOwnerNonce{0};
    std::uint8_t focused{0};
    BrowserRoute route{BrowserRoute::Default};
    BrowserMode mode{BrowserMode::Default};
    BrowserMode modeEventMode{BrowserMode::Default};
    std::uint8_t reserved[4]{};
    char browserExe[32]{};
    char hostname[256]{};
    char modeEventHostname[256]{};

    [[nodiscard]] bool HasValidHeader() const noexcept {
        return magic == kBrowserContextMagic
            && version == kBrowserContextVersion
            && structSize == sizeof(BrowserContextState);
    }
};

constexpr BrowserContextState::BrowserContextState() noexcept
    : structSize(static_cast<std::uint16_t>(sizeof(BrowserContextState))) {}

static_assert(offsetof(BrowserContextState, generation) == 0);
static_assert(sizeof(BrowserContextState) <= 640);

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
        out.modeEventSequence = source->modeEventSequence;
        out.modeEventOwnerProcessId = source->modeEventOwnerProcessId;
        out.modeEventOwnerNonce = source->modeEventOwnerNonce;
        out.focused = source->focused;
        out.route = source->route;
        out.mode = source->mode;
        out.modeEventMode = source->modeEventMode;
        for (std::size_t i = 0; i < sizeof(out.reserved); ++i)
            out.reserved[i] = source->reserved[i];
        for (std::size_t i = 0; i < sizeof(out.browserExe); ++i)
            out.browserExe[i] = source->browserExe[i];
        for (std::size_t i = 0; i < sizeof(out.hostname); ++i)
            out.hostname[i] = source->hostname[i];
        for (std::size_t i = 0; i < sizeof(out.modeEventHostname); ++i)
            out.modeEventHostname[i] = source->modeEventHostname[i];
        // Publishers bound all strings, but the mapping is writable by any
        // same-user process. Force termination so comparisons cannot run into
        // the following field.
        out.browserExe[sizeof(out.browserExe) - 1] = '\0';
        out.hostname[sizeof(out.hostname) - 1] = '\0';
        out.modeEventHostname[sizeof(out.modeEventHostname) - 1] = '\0';

        std::atomic_thread_fence(std::memory_order_acquire);
        if (before == source->generation) return out.HasValidHeader();
    }
    return false;
}

[[nodiscard]] inline bool IsBrowserContextFocusedForExe(
        const BrowserContextState& state,
        const char* foregroundExe,
        std::uint64_t nowTickMs) noexcept {
    if (!state.HasValidHeader() || state.focused == 0 || foregroundExe == nullptr)
        return false;
    if (nowTickMs < state.updatedTickMs
        || nowTickMs - state.updatedTickMs > kBrowserContextTtlMs)
        return false;
    return std::strcmp(state.browserExe, foregroundExe) == 0;
}

[[nodiscard]] inline bool IsBrowserContextApplicable(
        const BrowserContextState& state,
        const char* foregroundExe,
        std::uint64_t nowTickMs) noexcept {
    return IsBrowserContextFocusedForExe(state, foregroundExe, nowTickMs)
        && (state.route != BrowserRoute::Default
            || state.mode != BrowserMode::Default);
}

[[nodiscard]] constexpr bool IsBrowserContextOwnedBy(
        const BrowserContextState& state,
        std::uint32_t ownerProcessId,
        std::uint64_t ownerNonce) noexcept {
    return ownerProcessId != 0 && ownerNonce != 0
        && state.ownerProcessId == ownerProcessId
        && state.ownerNonce == ownerNonce;
}

[[nodiscard]] constexpr bool ResolveEffectiveTsf(
        bool configuredTsf, bool excluded, bool tsfFeatureEnabled,
        BrowserRoute route) noexcept {
    if (excluded || route == BrowserRoute::ForceEnglish) return false;
    if (route == BrowserRoute::ForceTsf) return tsfFeatureEnabled;
    return configuredTsf;
}

/// Returns -1 when the browser must inherit the current logical V/E state,
/// otherwise 0=English or 1=Vietnamese. App hard locks and domain hard-English
/// have precedence over the session preference.
[[nodiscard]] constexpr int ResolveBrowserModeTarget(
        bool excluded, bool forcedVietnamese,
        BrowserRoute route, BrowserMode mode) noexcept {
    if (excluded || forcedVietnamese || route == BrowserRoute::ForceEnglish
        || mode == BrowserMode::Default)
        return -1;
    if (mode == BrowserMode::Vietnamese) return 1;
    if (mode == BrowserMode::English) return 0;
    return -1;
}

/// A heartbeat can race the accepted hotkey event and briefly republish the
/// old session mode before the extension receives the event. Suppress that
/// stale echo for the same owner+hostname so the user's toggle never flickers
/// back while the round trip completes.
[[nodiscard]] inline BrowserMode ResolveBrowserModeForRestore(
        const BrowserContextState& state) noexcept {
    if (state.mode != BrowserMode::Default
        && state.mode != BrowserMode::Vietnamese
        && state.mode != BrowserMode::English)
        return BrowserMode::Default;
    const bool pendingEcho = state.modeEventSequence != 0
        && state.modeEventOwnerProcessId == state.ownerProcessId
        && state.modeEventOwnerNonce == state.ownerNonce
        && state.modeEventMode != BrowserMode::Default
        && state.modeEventMode != state.mode
        && std::strcmp(state.modeEventHostname, state.hostname) == 0;
    return pendingEcho ? BrowserMode::Default : state.mode;
}

} // namespace NextKey
