// NexusKey - Sciter Window Helper Implementation
// SPDX-License-Identifier: GPL-3.0-only

#include "SciterHelper.h"
#include <dwmapi.h>
#include <windowsx.h>

#pragma comment(lib, "dwmapi.lib")

namespace NextKey {
namespace SciterHelper {

void enableWindowBlur(HWND hwnd, BlurMode mode) noexcept {
    if (!hwnd) return;

    const bool isBlur = (mode == BlurMode::Blur);

    // 1. Apply SetWindowCompositionAttribute for blur effect (undocumented API)
    HMODULE hUser = GetModuleHandle(L"user32.dll");
    if (hUser) {
        using SetWindowCompositionAttributeFn = BOOL(WINAPI*)(HWND, WindowCompositionAttribData*);
        auto SetWindowCompositionAttribute = reinterpret_cast<SetWindowCompositionAttributeFn>(
            GetProcAddress(hUser, "SetWindowCompositionAttribute"));

        if (SetWindowCompositionAttribute) {
            AccentPolicy policy{};
            if (isBlur) {
                // Try AcrylicBlurBehind first (Windows 10 1803+), fallback to BlurBehind
                policy.AccentState = static_cast<int>(AccentState::AcrylicBlurBehind);
                policy.AccentFlags = 2;  // Enable blur behind
                policy.GradientColor = 0x01000000;  // ABGR: very slight tint
            } else {
                policy.AccentState = static_cast<int>(AccentState::Disabled);
            }

            WindowCompositionAttribData data{};
            data.Attrib = DwmConstants::WCA_ACCENT_POLICY;
            data.pvData = &policy;
            data.cbData = sizeof(policy);

            SetWindowCompositionAttribute(hwnd, &data);
        }
    }

    // 2. Enable rounded corners on Windows 11
    int cornerPreference = DwmConstants::DWMWCP_ROUND;
    DwmSetWindowAttribute(hwnd, DwmConstants::DWMWA_WINDOW_CORNER_PREFERENCE,
                          &cornerPreference, sizeof(cornerPreference));
}

LRESULT handleWindowDrag(HWND hwnd, LPARAM lParam, int titleHeight, int buttonsWidth) noexcept {
    POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
    ScreenToClient(hwnd, &pt);

    RECT clientRect{};
    GetClientRect(hwnd, &clientRect);

    // Drag zone: top area excluding buttons on the right
    const int buttonsZone = clientRect.right - buttonsWidth;

    if (pt.y < titleHeight && pt.x < buttonsZone) {
        return HTCAPTION;
    }

    return HTCLIENT;
}

}  // namespace SciterHelper
}  // namespace NextKey
