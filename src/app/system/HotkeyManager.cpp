// NexusKey - Hotkey Manager Implementation
// SPDX-License-Identifier: GPL-3.0-only

#include "HotkeyManager.h"
#include "core/Debug.h"

namespace NextKey {

std::atomic<HotkeyManager*> HotkeyManager::s_instance{nullptr};

static constexpr int HOTKEY_ID = 1;

HotkeyManager::~HotkeyManager() {
    Uninstall();
}

void HotkeyManager::Initialize(const HotkeyConfig& config, HWND hwndMessage, HINSTANCE hInstance) {
    config_ = config;
    hwndMessage_ = hwndMessage;
    s_instance = this;

    if (!HasNexusKeyHotkey(config_)) {
        NEXTKEY_LOG(L"NexusKey hotkey disabled by user");
        return;
    }

    // Always install our hotkey. NexusKey is a single-layout TIP —
    // toggle is via SharedState flag, not keyboard layout switching.
    if (config_.key != 0) {
        InstallRegisterHotKey(hwndMessage);
    } else {
        InstallKeyboardHook(hInstance);
    }

    NEXTKEY_LOG(L"Hotkey installed (ctrl=%d, shift=%d, alt=%d, win=%d, key=%d)",
                config_.ctrl, config_.shift, config_.alt, config_.win, config_.key);
}

void HotkeyManager::Uninstall() {
    if (keyboardHook_) {
        UnhookWindowsHookEx(keyboardHook_);
        keyboardHook_ = nullptr;
    }
    if (hwndMessage_) {
        UnregisterHotKey(hwndMessage_, HOTKEY_ID);
    }
    if (s_instance == this) {
        s_instance = nullptr;
    }
}

bool HotkeyManager::HasNexusKeyHotkey(const HotkeyConfig& cfg) {
    return cfg.ctrl || cfg.shift || cfg.alt || cfg.win || cfg.key != 0;
}

void HotkeyManager::InstallRegisterHotKey(HWND hwndMessage) {
    UINT modifiers = 0;
    if (config_.ctrl) modifiers |= MOD_CONTROL;
    if (config_.shift) modifiers |= MOD_SHIFT;
    if (config_.alt) modifiers |= MOD_ALT;
    if (config_.win) modifiers |= MOD_WIN;

    UINT vk = static_cast<UINT>(config_.key);
    if (RegisterHotKey(hwndMessage, HOTKEY_ID, modifiers, vk)) {
        NEXTKEY_LOG(L"RegisterHotKey installed (mod=0x%X, key='%c')", modifiers, config_.key);
    } else {
        NEXTKEY_LOG(L"RegisterHotKey failed (error: %lu)", GetLastError());
    }
}

void HotkeyManager::InstallKeyboardHook(HINSTANCE hInstance) {
    keyboardHook_ = SetWindowsHookExW(WH_KEYBOARD_LL, LowLevelKeyboardProc, hInstance, 0);
    if (keyboardHook_) {
        NEXTKEY_LOG(L"Keyboard hook installed for modifier-only hotkey");
    } else {
        NEXTKEY_LOG(L"Keyboard hook failed (error: %lu)", GetLastError());
    }
}

LRESULT CALLBACK HotkeyManager::LowLevelKeyboardProc(int nCode, WPARAM wParam, LPARAM lParam) {
    HotkeyManager* inst = s_instance.load(std::memory_order_relaxed);
    if (nCode == HC_ACTION && inst) {
        auto& self = *inst;
        auto* pKey = reinterpret_cast<KBDLLHOOKSTRUCT*>(lParam);
        bool isDown = (wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN);
        bool isUp = (wParam == WM_KEYUP || wParam == WM_SYSKEYUP);
        DWORD vk = pKey->vkCode;

        bool isCtrl = (vk == VK_LCONTROL || vk == VK_RCONTROL);
        bool isShift = (vk == VK_LSHIFT || vk == VK_RSHIFT);
        bool isAlt = (vk == VK_LMENU || vk == VK_RMENU);
        bool isWin = (vk == VK_LWIN || vk == VK_RWIN);

        auto checkMatch = [&]() -> bool {
            return (!self.config_.ctrl || self.modCtrlDown_) &&
                   (!self.config_.shift || self.modShiftDown_) &&
                   (!self.config_.alt || self.modAltDown_) &&
                   (!self.config_.win || self.modWinDown_);
        };

        // On match: post WM_HOTKEY to tray window → OnMenuCommand(ToggleMode)
        auto notifyToggle = [&]() {
            if (self.hwndMessage_) {
                PostMessageW(self.hwndMessage_, WM_HOTKEY, 0, 0);
            }
        };

        if (isCtrl) {
            if (isDown && !self.modCtrlDown_) { self.modCtrlDown_ = true; self.otherKeyPressed_ = false; }
            else if (isUp) {
                if (!self.otherKeyPressed_ && checkMatch()) notifyToggle();
                self.modCtrlDown_ = false;
            }
        } else if (isShift) {
            if (isDown && !self.modShiftDown_) { self.modShiftDown_ = true; self.otherKeyPressed_ = false; }
            else if (isUp) {
                if (!self.otherKeyPressed_ && checkMatch()) notifyToggle();
                self.modShiftDown_ = false;
            }
        } else if (isAlt) {
            if (isDown && !self.modAltDown_) { self.modAltDown_ = true; self.otherKeyPressed_ = false; }
            else if (isUp) {
                if (!self.otherKeyPressed_ && checkMatch()) notifyToggle();
                self.modAltDown_ = false;
            }
        } else if (isWin) {
            if (isDown && !self.modWinDown_) { self.modWinDown_ = true; self.otherKeyPressed_ = false; }
            else if (isUp) {
                if (!self.otherKeyPressed_ && checkMatch()) notifyToggle();
                self.modWinDown_ = false;
            }
        } else if (isDown) {
            self.otherKeyPressed_ = true;
        }
    }

    HotkeyManager* inst2 = s_instance.load(std::memory_order_relaxed);
    return CallNextHookEx(inst2 ? inst2->keyboardHook_ : nullptr, nCode, wParam, lParam);
}

}  // namespace NextKey
