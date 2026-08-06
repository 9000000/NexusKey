// VKey - Advanced engine installer
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <windows.h>

#include <cstdint>

namespace NextKey {

/// Ready is the only result that permits storing Advanced. Every other result
/// must be persisted synchronously so startup never re-prompts without another
/// explicit selection: startup repair writes Standard (the stored level was
/// Advanced), while a level selector reverts to the level it came from — that
/// may be Off, and a declined download is not a request to switch spell check on.
enum class AdvancedEngineStatus : std::uint8_t {
    Ready,
    /// The user chose manual installation. Persist before opening any browser or
    /// follow-up UI, then call ShowManualInstallWithUi.
    ManualRequested,
    /// The user chose Standard mode or cancelled.
    Declined,
    /// Installation failed and the user did not switch to manual installation.
    Unavailable,
};

class AdvancedEngineInstaller final {
public:
    [[nodiscard]] static AdvancedEngineStatus EnsureInstalledWithUi(HWND parent);
    static void ShowManualInstallWithUi(HWND parent);
};

} // namespace NextKey
