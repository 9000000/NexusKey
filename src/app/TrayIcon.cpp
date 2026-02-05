// NexusKey - System Tray Icon Implementation
// SPDX-License-Identifier: GPL-3.0-only

#include "TrayIcon.h"
#include "resource.h"
#include <strsafe.h>

namespace NextKey {

// Static instance pointer for WndProc callback
static TrayIcon* g_trayInstance = nullptr;

TrayIcon::TrayIcon() {
    g_trayInstance = this;
}

TrayIcon::~TrayIcon() {
    Destroy();
    g_trayInstance = nullptr;
}

bool TrayIcon::Create(HINSTANCE hInstance) {
    // Register window class for message-only window
    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(WNDCLASSEXW);
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = L"NexusKeyTrayClass";

    if (!RegisterClassExW(&wc)) {
        // Class might already be registered
        if (GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
            return false;
        }
    }

    // Create message-only window
    hwndMessage_ = CreateWindowExW(
        0,
        L"NexusKeyTrayClass",
        L"NexusKey Tray",
        0,
        0, 0, 0, 0,
        HWND_MESSAGE,  // Message-only window
        nullptr,
        hInstance,
        nullptr
    );

    if (!hwndMessage_) {
        return false;
    }

    // Initialize NOTIFYICONDATA
    ZeroMemory(&nid_, sizeof(nid_));
    nid_.cbSize = sizeof(NOTIFYICONDATAW);
    nid_.hWnd = hwndMessage_;
    nid_.uID = 1;
    nid_.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    nid_.uCallbackMessage = WM_TRAYICON;
    
    // Use Vietnamese ON icon by default
    nid_.hIcon = LoadIconW(hInstance, MAKEINTRESOURCEW(IDI_VIET_ON));
    if (!nid_.hIcon) {
        nid_.hIcon = LoadIconW(nullptr, IDI_APPLICATION);  // Fallback
    }
    StringCchCopyW(nid_.szTip, ARRAYSIZE(nid_.szTip), L"NexusKey - Vietnamese Input");

    // Add icon to tray
    if (!Shell_NotifyIconW(NIM_ADD, &nid_)) {
        DestroyWindow(hwndMessage_);
        hwndMessage_ = nullptr;
        return false;
    }

    return true;
}

void TrayIcon::Destroy() {
    if (nid_.hWnd) {
        Shell_NotifyIconW(NIM_DELETE, &nid_);
        ZeroMemory(&nid_, sizeof(nid_));
    }
    if (hwndMessage_) {
        DestroyWindow(hwndMessage_);
        hwndMessage_ = nullptr;
    }
}

void TrayIcon::SetVietnameseMode(bool enabled) {
    vietnameseMode_ = enabled;
    
    // Update icon based on mode
    HINSTANCE hInstance = GetModuleHandleW(nullptr);
    HICON newIcon = LoadIconW(hInstance, MAKEINTRESOURCEW(enabled ? IDI_VIET_ON : IDI_VIET_OFF));
    if (newIcon) {
        nid_.hIcon = newIcon;
    }
    
    // Update tooltip
    StringCchCopyW(
        nid_.szTip, 
        ARRAYSIZE(nid_.szTip), 
        enabled ? L"NexusKey - Vietnamese ON" : L"NexusKey - Vietnamese OFF"
    );
    Shell_NotifyIconW(NIM_MODIFY, &nid_);
}

void TrayIcon::ShowContextMenu() {
    HMENU hMenu = CreatePopupMenu();
    if (!hMenu) return;

    // Add menu items
    AppendMenuW(hMenu, MF_STRING, static_cast<UINT>(TrayMenuId::Settings), L"Settings...");
    AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(hMenu, MF_STRING, static_cast<UINT>(TrayMenuId::About), L"About NexusKey");
    AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(hMenu, MF_STRING, static_cast<UINT>(TrayMenuId::Exit), L"Exit");

    // Get cursor position
    POINT pt;
    GetCursorPos(&pt);

    // Required for menu to work correctly
    SetForegroundWindow(hwndMessage_);

    // Show menu
    UINT cmd = TrackPopupMenu(
        hMenu,
        TPM_RETURNCMD | TPM_NONOTIFY | TPM_RIGHTBUTTON,
        pt.x, pt.y,
        0,
        hwndMessage_,
        nullptr
    );

    DestroyMenu(hMenu);

    // Handle command
    if (cmd && menuCallback_) {
        menuCallback_(static_cast<TrayMenuId>(cmd));
    }

    // Post dummy message to fix menu issue
    PostMessageW(hwndMessage_, WM_NULL, 0, 0);
}

bool TrayIcon::ProcessMessage(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    UNREFERENCED_PARAMETER(wParam);
    if (msg == WM_TRAYICON && hwnd == hwndMessage_) {
        switch (LOWORD(lParam)) {
            case WM_RBUTTONUP:
            case WM_CONTEXTMENU:
                ShowContextMenu();
                return true;
            case WM_LBUTTONDBLCLK:
                // Double-click opens settings
                if (menuCallback_) {
                    menuCallback_(TrayMenuId::Settings);
                }
                return true;
        }
    }
    return false;
}

LRESULT CALLBACK TrayIcon::WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (g_trayInstance && g_trayInstance->ProcessMessage(hwnd, msg, wParam, lParam)) {
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

}  // namespace NextKey
