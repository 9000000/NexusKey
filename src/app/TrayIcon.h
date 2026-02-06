// NexusKey - System Tray Icon
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <Windows.h>
#include <shellapi.h>
#include <functional>
#include <utility>

namespace NextKey {

/// Menu item identifiers
enum class TrayMenuId : UINT {
    Settings = 1001,
    About = 1002,
    Exit = 1003
};

/// Callback types for tray events
using MenuCallback = std::function<void(TrayMenuId)>;

/// System tray icon manager
class TrayIcon {
public:
    TrayIcon();
    ~TrayIcon();

    // Non-copyable
    TrayIcon(const TrayIcon&) = delete;
    TrayIcon& operator=(const TrayIcon&) = delete;

    /// Initialize tray icon with message window
    [[nodiscard]] bool Create(HINSTANCE hInstance);

    /// Destroy tray icon
    void Destroy() noexcept;

    /// Set Vietnamese mode indicator (changes icon)
    void SetVietnameseMode(bool enabled) noexcept;

    /// Set callback for menu actions
    void SetMenuCallback(MenuCallback callback) noexcept { menuCallback_ = std::move(callback); }

    /// Process window messages (call from message loop)
    [[nodiscard]] bool ProcessMessage(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) noexcept;

    /// Get the hidden message window handle
    [[nodiscard]] HWND GetMessageWindow() const noexcept { return hwndMessage_; }

private:
    void ShowContextMenu();
    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

    HWND hwndMessage_ = nullptr;
    NOTIFYICONDATAW nid_ = {};
    bool vietnameseMode_ = true;
    MenuCallback menuCallback_;

    static constexpr UINT WM_TRAYICON = WM_USER + 1;
};

}  // namespace NextKey
