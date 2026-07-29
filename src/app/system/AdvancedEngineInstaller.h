// VKey - Advanced engine installer
// SPDX-License-Identifier: AGPL-3.0-only

#pragma once

#include <windows.h>

#include <cstdint>

namespace NextKey {

enum class AdvancedEngineStatus : std::uint8_t {
    Ready,
    /// The user declined the download or cancelled it. Honour that choice —
    /// persisting a downgrade to Standard is what they asked for.
    Declined,
    /// Network, storage or verification failure. The stored level must survive so
    /// the next launch retries; a Wi-Fi hiccup is not a request to change it.
    Unavailable,
};

class AdvancedEngineInstaller final {
public:
    [[nodiscard]] static AdvancedEngineStatus EnsureInstalledWithUi(HWND parent);
};

} // namespace NextKey
