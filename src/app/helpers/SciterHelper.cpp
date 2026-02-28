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

    // 1. Set WS_EX_LAYERED to allow alpha/blur composition
    SetWindowLong(hwnd, GWL_EXSTYLE, GetWindowLong(hwnd, GWL_EXSTYLE) | WS_EX_LAYERED);

    // 2. Apply SetWindowCompositionAttribute for blur effect (undocumented API)
    HMODULE hUser = GetModuleHandle(L"user32.dll");
    if (hUser) {
        using SetWindowCompositionAttributeFn = BOOL(WINAPI*)(HWND, WindowCompositionAttribData*);
        auto SetWindowCompositionAttribute = reinterpret_cast<SetWindowCompositionAttributeFn>(
            GetProcAddress(hUser, "SetWindowCompositionAttribute"));

        if (SetWindowCompositionAttribute) {
            AccentPolicy policy{};
            if (isBlur) {
                // Use BlurBehind (3) - most stable across Win10/11 versions
                // (AcrylicBlurBehind=4 has compatibility issues)
                policy.AccentState = static_cast<int>(AccentState::BlurBehind);
                policy.AccentFlags = 0;
                policy.GradientColor = 0;
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

    // 3. Enable rounded corners on Windows 11
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
