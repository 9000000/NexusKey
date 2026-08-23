// VKey browser native-messaging protocol
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "core/ipc/BrowserContextState.h"

#include <string>
#include <string_view>

namespace NextKey::BrowserHost {

inline constexpr std::size_t kMaxNativeMessageBytes = 8 * 1024;

struct NativeMessage {
    int protocol{1};
    std::string browserExe;
    std::string hostname;
    BrowserRoute route{BrowserRoute::Default};
    BrowserMode mode{BrowserMode::Default};
    bool focused{false};
};

struct NativeModeEvent {
    std::uint32_t sequence{0};
    std::string hostname;
    BrowserMode mode{BrowserMode::Default};
};

/// Parse the deliberately tiny protocol without accepting arbitrary URLs.
/// v1 requires browser/hostname/route/focused. v2 additionally requires mode.
[[nodiscard]] bool ParseNativeMessage(std::string_view json,
                                      NativeMessage& out,
                                      std::string& error) noexcept;

[[nodiscard]] bool TryReadModeEvent(const BrowserContextState& state,
                                    std::uint32_t ownerProcessId,
                                    std::uint64_t ownerNonce,
                                    std::uint32_t lastSequence,
                                    NativeModeEvent& out) noexcept;

[[nodiscard]] std::string SerializeModeEvent(const NativeModeEvent& event);

} // namespace NextKey::BrowserHost
