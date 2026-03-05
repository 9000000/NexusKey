// NexusKey - Hotkey Manager
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "core/config/TypingConfig.h"
#include <Windows.h>
#include <atomic>

namespace NextKey {

/// Manages keyboard hotkey for Vietnamese mode toggle.
/// NexusKey is a single-layout TIP on English — no layout switching needed.
/// On hotkey match: posts WM_HOTKEY to tray window → toggles SharedState VIETNAMESE_MODE.
class HotkeyManager {
public:
    HotkeyManager() = default;
    ~HotkeyManager();

    HotkeyManager(const HotkeyManager&) = delete;
    HotkeyManager& operator=(const HotkeyManager&) = delete;

    /// Install hotkey (reads config from TOML, falls back to Ctrl+Shift)
    void Initialize(const HotkeyConfig& config, HWND hwndMessage, HINSTANCE hInstance);

    /// Remove installed hotkey
    void Uninstall();

private:
    [[nodiscard]] static bool HasNexusKeyHotkey(const HotkeyConfig& cfg);
    void InstallRegisterHotKey(HWND hwndMessage);
    void InstallKeyboardHook(HINSTANCE hInstance);
    static LRESULT CALLBACK LowLevelKeyboardProc(int nCode, WPARAM wParam, LPARAM lParam);

    HotkeyConfig config_{};
    HWND hwndMessage_ = nullptr;
    HHOOK keyboardHook_ = nullptr;

    bool modCtrlDown_ = false;
    bool modShiftDown_ = false;
    bool modAltDown_ = false;
    bool modWinDown_ = false;
    bool otherKeyPressed_ = false;

    static std::atomic<HotkeyManager*> s_instance;
};

}  // namespace NextKey
