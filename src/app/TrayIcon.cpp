// NexusKey - System Tray Icon Implementation
// SPDX-License-Identifier: GPL-3.0-only

#include "TrayIcon.h"
#include "resource.h"
#include <strsafe.h>

namespace NextKey {

static TrayIcon* g_trayInstance = nullptr;

TrayIcon::TrayIcon() {
    g_trayInstance = this;
}

TrayIcon::~TrayIcon() {
    Destroy();
    g_trayInstance = nullptr;
}

bool TrayIcon::Create(HINSTANCE hInstance) {
    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(WNDCLASSEXW);
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = L"NexusKeyTrayClass";

    if (!RegisterClassExW(&wc)) {
        if (GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
            return false;
        }
    }

    hwndMessage_ = CreateWindowExW(
        0, L"NexusKeyTrayClass", L"NexusKey Tray", 0,
        0, 0, 0, 0, nullptr, nullptr, hInstance, nullptr);

    if (!hwndMessage_) return false;

    ZeroMemory(&nid_, sizeof(nid_));
    nid_.cbSize = sizeof(NOTIFYICONDATAW);
    nid_.hWnd = hwndMessage_;
    nid_.uID = 1;
    nid_.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    nid_.uCallbackMessage = WM_TRAYICON;

    nid_.hIcon = LoadIconW(hInstance, MAKEINTRESOURCEW(IDI_VIET_ON));
    if (!nid_.hIcon) {
        nid_.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    }
    StringCchCopyW(nid_.szTip, ARRAYSIZE(nid_.szTip), L"NexusKey - Vietnamese");

    // Always visible
    Shell_NotifyIconW(NIM_ADD, &nid_);
    return true;
}

void TrayIcon::Destroy() noexcept {
    if (nid_.hWnd) {
        Shell_NotifyIconW(NIM_DELETE, &nid_);
    }
    ZeroMemory(&nid_, sizeof(nid_));
    if (hwndMessage_) {
        DestroyWindow(hwndMessage_);
        hwndMessage_ = nullptr;
    }
}

void TrayIcon::SetVietnameseMode(bool enabled) noexcept {
    if (vietnameseMode_ == enabled) return;
    vietnameseMode_ = enabled;

    HINSTANCE hInstance = GetModuleHandleW(nullptr);
    HICON newIcon = LoadIconW(hInstance, MAKEINTRESOURCEW(enabled ? IDI_VIET_ON : IDI_VIET_OFF));
    if (newIcon) {
        nid_.hIcon = newIcon;
    }

    StringCchCopyW(nid_.szTip, ARRAYSIZE(nid_.szTip),
        enabled ? L"NexusKey - Vietnamese" : L"NexusKey - English");

    if (nid_.hWnd) {
        Shell_NotifyIconW(NIM_MODIFY, &nid_);
    }
}

void TrayIcon::ShowContextMenu() {
    HMENU hMenu = CreatePopupMenu();
    if (!hMenu) return;

    AppendMenuW(hMenu, MF_STRING, static_cast<UINT>(TrayMenuId::Settings), L"Settings...");
    AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(hMenu, MF_STRING, static_cast<UINT>(TrayMenuId::About), L"About NexusKey");
    AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(hMenu, MF_STRING, static_cast<UINT>(TrayMenuId::Exit), L"Exit");

    POINT pt;
    GetCursorPos(&pt);
    SetForegroundWindow(hwndMessage_);

    UINT cmd = TrackPopupMenu(hMenu, TPM_RETURNCMD | TPM_NONOTIFY | TPM_RIGHTBUTTON,
        pt.x, pt.y, 0, hwndMessage_, nullptr);
    DestroyMenu(hMenu);

    if (cmd && menuCallback_) {
        menuCallback_(static_cast<TrayMenuId>(cmd));
    }
    PostMessageW(hwndMessage_, WM_NULL, 0, 0);
}

bool TrayIcon::ProcessMessage(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) noexcept {
    // Settings dialog requesting a specific V/E mode (cross-process)
    if (msg == WM_NEXUSKEY_SET_MODE && hwnd == hwndMessage_) {
        if (modeRequestCallback_) {
            modeRequestCallback_(wParam != 0);
        }
        return true;
    }

    // WM_HOTKEY is handled by the caller's WndProc, not here
    if (msg == WM_HOTKEY && hwnd == hwndMessage_) {
        if (menuCallback_) {
            menuCallback_(TrayMenuId::ToggleMode);
        }
        return true;
    }

    if (msg != WM_TRAYICON || hwnd != hwndMessage_) return false;

    switch (LOWORD(lParam)) {
        case WM_RBUTTONUP:
        case WM_CONTEXTMENU:
            ShowContextMenu();
            return true;
        case WM_LBUTTONUP:
            // Toggle immediately (no delay)
            toggledByClick_ = true;
            if (menuCallback_) {
                menuCallback_(TrayMenuId::ToggleMode);
            }
            return true;
        case WM_LBUTTONDBLCLK:
            // Undo the toggle from the first click, then open settings
            if (toggledByClick_ && menuCallback_) {
                menuCallback_(TrayMenuId::ToggleMode);  // Undo
            }
            toggledByClick_ = false;
            if (menuCallback_) {
                menuCallback_(TrayMenuId::Settings);
            }
            return true;
    }

    // Any non-left-button event clears the toggle tracking
    toggledByClick_ = false;
    return false;
}

LRESULT CALLBACK TrayIcon::WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (g_trayInstance && g_trayInstance->ProcessMessage(hwnd, msg, wParam, lParam)) {
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

}  // namespace NextKey
