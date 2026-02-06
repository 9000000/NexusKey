// NexusKey - DPI & Resolution Scale Helper
// SPDX-License-Identifier: GPL-3.0-only
//
// Utility for scaling dialog sizes based on DPI and screen resolution.
// Ensures UI looks correct across different monitors and DPI settings.

#pragma once
#include <windows.h>
#include <algorithm>

namespace NextKey {

/**
 * ScaleHelper - Utility for scaling dialog sizes based on DPI and resolution.
 *
 * Considers both:
 * 1. DPI scaling (125%, 150%, etc.) - Sciter renders at native DPI
 * 2. Screen resolution (1366x768, etc.) - scales down proportionally
 *
 * Reference: 1920x1080 @ 100% DPI = scale factor 1.0
 */
class ScaleHelper {
public:
    // Reference resolution (development baseline at 100% DPI)
    static constexpr int REF_SCREEN_WIDTH = 1920;
    static constexpr int REF_SCREEN_HEIGHT = 1080;
    static constexpr double MIN_SCALE = 0.7;
    static constexpr double MAX_SCALE = 1.6;
    static constexpr int DEFAULT_DPI = 96;

    /**
     * Get DPI scale factor (1.0 = 100%, 1.25 = 125%, etc.)
     */
    [[nodiscard]] static double getDpiScale() noexcept {
        HDC hdc = GetDC(nullptr);
        if (!hdc) return 1.0;
        int dpiX = GetDeviceCaps(hdc, LOGPIXELSX);
        ReleaseDC(nullptr, hdc);
        return static_cast<double>(dpiX) / DEFAULT_DPI;
    }

    /**
     * Get the combined scale factor for dialogs.
     *
     * Examples:
     * - 1920x1080 @ 100%: scale = 1.0
     * - 1920x1080 @ 125%: scale = 1.25
     * - 1920x1080 @ 150%: scale = 1.5
     * - 1366x768 @ 100%:  scale = 0.71
     * - 1366x768 @ 125%:  scale = 0.89
     */
    [[nodiscard]] static double getScaleFactor() noexcept {
        // Factor 1: Resolution scaling (for small screens)
        int screenWidth = GetSystemMetrics(SM_CXSCREEN);
        int screenHeight = GetSystemMetrics(SM_CYSCREEN);
        double resScaleX = static_cast<double>(screenWidth) / REF_SCREEN_WIDTH;
        double resScaleY = static_cast<double>(screenHeight) / REF_SCREEN_HEIGHT;
        // Note: Parentheses around std::min/max to prevent Windows macro expansion
        double resScale = (std::min)(resScaleX, resScaleY);

        // Factor 2: DPI scaling - Sciter renders at native DPI
        double dpiScale = getDpiScale();

        // Apply both factors and clamp
        double scale = resScale * dpiScale;
        return (std::max)(MIN_SCALE, (std::min)(MAX_SCALE, scale));
    }

    /**
     * Scale a single dimension (width or height).
     */
    [[nodiscard]] static int scale(int baseDimension) noexcept {
        return static_cast<int>(baseDimension * getScaleFactor());
    }

    /**
     * Scale a RECT for Sciter window initialization.
     */
    [[nodiscard]] static RECT scaleRect(int baseWidth, int baseHeight) noexcept {
        double factor = getScaleFactor();
        return RECT{
            0, 0,
            static_cast<LONG>(baseWidth * factor),
            static_cast<LONG>(baseHeight * factor)
        };
    }

    /**
     * Get scaled width and height as separate output values.
     */
    static void getScaledSize(int baseWidth, int baseHeight, int& outWidth, int& outHeight) noexcept {
        double factor = getScaleFactor();
        outWidth = static_cast<int>(baseWidth * factor);
        outHeight = static_cast<int>(baseHeight * factor);
    }
};

}  // namespace NextKey
