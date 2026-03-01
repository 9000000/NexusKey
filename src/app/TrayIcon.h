// NexusKey - System Tray Icon
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "core/ipc/SharedConstants.h"
#include <Windows.h>
#include <shellapi.h>
#include <functional>
#include <utility>

namespace NextKey {

/// Menu item identifiers
enum class TrayMenuId : UINT {
    Settings = 1001,
    About = 1002,
    Exit = 1003,
    ToggleMode = 1004
};

/// Callback type for tray events
using MenuCallback = std::function<void(TrayMenuId)>;

/// Callback for settings dialog requesting a specific V/E mode
using ModeRequestCallback = std::function<void(bool vietnamese)>;

/// System tray icon manager — always visible, shows V/E state
class TrayIcon {
public:
    TrayIcon();
    ~TrayIcon();

    TrayIcon(const TrayIcon&) = delete;
    TrayIcon& operator=(const TrayIcon&) = delete;

    /// Initialize and add tray icon
    [[nodiscard]] bool Create(HINSTANCE hInstance);

    /// Destroy tray icon
    void Destroy() noexcept;

    /// Update icon to reflect Vietnamese/English mode
    void SetVietnameseMode(bool enabled) noexcept;

    /// Set callback for menu/click actions
    void SetMenuCallback(MenuCallback callback) noexcept { menuCallback_ = std::move(callback); }

    /// Set callback for settings dialog mode requests (cross-process)
    void SetModeRequestCallback(ModeRequestCallback callback) noexcept { modeRequestCallback_ = std::move(callback); }

    /// Process window messages (call from WndProc)
    [[nodiscard]] bool ProcessMessage(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) noexcept;

    /// Get the hidden message window handle (for timers/hotkeys)
    [[nodiscard]] HWND GetMessageWindow() const noexcept { return hwndMessage_; }

    [[nodiscard]] bool IsVietnameseMode() const noexcept { return vietnameseMode_; }

private:
    void ShowContextMenu();
    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

    HWND hwndMessage_ = nullptr;
    NOTIFYICONDATAW nid_ = {};
    bool vietnameseMode_ = true;
    bool toggledByClick_ = false;        // Single-click toggled — undo if double-click follows
    MenuCallback menuCallback_;
    ModeRequestCallback modeRequestCallback_;
    static constexpr UINT WM_TRAYICON = WM_USER + 1;
};

}  // namespace NextKey
