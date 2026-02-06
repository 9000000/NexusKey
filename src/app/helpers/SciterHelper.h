// NexusKey - Sciter Window Helper
// SPDX-License-Identifier: GPL-3.0-only
//
// Utilities for Sciter window effects: DWM blur, acrylic, window dragging.

#pragma once
#include <windows.h>

namespace NextKey {

/// Blur mode for Sciter windows
enum class BlurMode {
    Solid = 0,   // No blur effect
    Blur = 1     // DWM blur/acrylic effect
};

/// DWM accent state values (undocumented Windows API)
enum class AccentState : int {
    Disabled = 0,
    BlurBehind = 3,
    AcrylicBlurBehind = 4,
    HostBackdrop = 5
};

/// DWM accent policy structure (for SetWindowCompositionAttribute)
struct AccentPolicy {
    int AccentState;
    int AccentFlags;
    int GradientColor;
    int AnimationId;
};

/// DWM window composition attribute data
struct WindowCompositionAttribData {
    int Attrib;
    void* pvData;
    size_t cbData;
};

/// Undocumented DWM constants
namespace DwmConstants {
    constexpr int WCA_ACCENT_POLICY = 19;
    constexpr DWORD DWMWA_WINDOW_CORNER_PREFERENCE = 33;
    constexpr int DWMWCP_ROUND = 2;
}

/// Default dimensions for title bar drag handling
namespace TitleBarDefaults {
    constexpr int HEIGHT = 40;
    constexpr int BUTTONS_WIDTH = 80;
}

namespace SciterHelper {

    /**
     * Enable DWM blur/acrylic effect on a window.
     *
     * @param hwnd Window handle
     * @param mode BlurMode::Solid (no effect) or BlurMode::Blur (acrylic)
     */
    void enableWindowBlur(HWND hwnd, BlurMode mode) noexcept;

    /**
     * Handle WM_NCHITTEST for window dragging via title bar area.
     *
     * Call this from your SubclassProc when handling WM_NCHITTEST.
     * Returns HTCAPTION if mouse is in drag zone, HTCLIENT otherwise.
     *
     * @param hwnd Window handle
     * @param lParam LPARAM from WM_NCHITTEST (contains mouse position)
     * @param titleHeight Height of the title bar drag zone (default 40px)
     * @param buttonsWidth Width of buttons area to exclude from drag zone (default 80px)
     * @return HTCAPTION for drag zone, HTCLIENT for content area
     */
    [[nodiscard]] LRESULT handleWindowDrag(
        HWND hwnd,
        LPARAM lParam,
        int titleHeight = TitleBarDefaults::HEIGHT,
        int buttonsWidth = TitleBarDefaults::BUTTONS_WIDTH) noexcept;

}  // namespace SciterHelper

}  // namespace NextKey
