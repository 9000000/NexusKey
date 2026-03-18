// NexusKey - System Configuration
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <cstdint>

namespace NextKey {

/// Icon style for tray/language bar
enum class IconStyle : uint8_t {
    Color = 0,   // Default colored icons (V=pink, E=blue)
    Dark = 1,    // White icons (for dark taskbar)
    Light = 2,   // Black icons (for light taskbar)
    Custom = 3   // User-selected custom colors
};

/// Default icon colors (COLORREF format: 0x00BBGGRR)
/// V: RGB(243, 98, 103) = #F36267
/// E: RGB(47, 175, 218)  = #2FAFDA
inline constexpr uint32_t DEFAULT_ICON_COLOR_V = 0x006762F3;  // BGR
inline constexpr uint32_t DEFAULT_ICON_COLOR_E = 0x00DAAF2F;  // BGR

/// System-level configuration (EXE-only, not shared with DLL)
/// Stored in [system] section of config.toml
struct SystemConfig {
    bool runAtStartup = false;     // Register to run on Windows logon
    bool runAsAdmin = false;       // Run with elevated privileges (Task Scheduler)
    bool showOnStartup = true;     // Open settings dialog on app startup
    bool desktopShortcut = false;  // Desktop shortcut exists

    // UI language (0=Vietnamese, 1=English)
    uint8_t language = 0;

    // Icon customization
    uint8_t iconStyle = 0;         // IconStyle enum (0=Color, 1=Dark, 2=Light, 3=Custom)
    uint32_t customColorV = 0;     // Custom V color (COLORREF, 0 = use default)
    uint32_t customColorE = 0;     // Custom E color (COLORREF, 0 = use default)

    // Auto-update
    bool autoCheckUpdate = true;   // Check for updates on startup

    /// Get effective V color (default if custom not set)
    [[nodiscard]] uint32_t GetEffectiveColorV() const noexcept {
        return customColorV != 0 ? customColorV : DEFAULT_ICON_COLOR_V;
    }

    /// Get effective E color (default if custom not set)
    [[nodiscard]] uint32_t GetEffectiveColorE() const noexcept {
        return customColorE != 0 ? customColorE : DEFAULT_ICON_COLOR_E;
    }

    SystemConfig() = default;
};

}  // namespace NextKey
