// VKey browser native-messaging protocol
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "core/ipc/BrowserContextState.h"

#include <string>
#include <string_view>

namespace NextKey::BrowserHost {

inline constexpr std::size_t kMaxNativeMessageBytes = 8 * 1024;

struct NativeMessage {
    std::string browserExe;
    std::string hostname;
    BrowserRoute route{BrowserRoute::Default};
    bool focused{false};
};

/// Parse the deliberately tiny v1 protocol without accepting arbitrary URLs.
/// Required JSON fields: protocol=1, browser, hostname, route, focused.
[[nodiscard]] bool ParseNativeMessage(std::string_view json,
                                      NativeMessage& out,
                                      std::string& error) noexcept;

} // namespace NextKey::BrowserHost
