// VKey - Hotkey Manager
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "core/config/TypingConfig.h"
#include <Windows.h>
#include <atomic>
#include <functional>
#include <mutex>
#include <vector>

namespace NextKey {

/// Multi-slot keyboard hotkey manager. Each slot holds a HotkeyConfig + callback.
/// VKey is a single-layout TIP on English — no layout switching needed.
///
/// Implemented via WH_KEYBOARD_LL (no RegisterHotKey) so we can:
///   1. Eat DOWN/UP of the target key to prevent double-activation.
///   2. Suppress auto-repeat while the combo is held.
///   3. Inject a dummy key tagged with VKEY_EXTRA_INFO to break Windows
///      "Alt/Win tapped alone" detection (browser menu activation bug).
class HotkeyManager {
public:
    using Callback = std::function<void()>;
    using SlotId = size_t;

    HotkeyManager() = default;
    ~HotkeyManager();

    HotkeyManager(const HotkeyManager&) = delete;
    HotkeyManager& operator=(const HotkeyManager&) = delete;

    /// Register a hotkey slot. Returns the slot id for later UpdateHotkey calls.
    /// Callback runs on the hook thread — keep it quick (PostMessage preferred).
    SlotId AddHotkey(const HotkeyConfig& config, Callback callback);

    /// Replace an existing slot's config (used on config reload).
    void UpdateHotkey(SlotId slot, const HotkeyConfig& config);

    /// Install the LL keyboard hook. All AddHotkey() calls must happen first.
    void Initialize(HINSTANCE hInstance);

    /// Unhook and clear slots.
    void Uninstall();

private:
    struct Slot {
        HotkeyConfig config{};
        Callback callback;
        BYTE vkCached = 0;   // VkKeyScanW(config.key) — layout-aware target VK for combo hotkeys
        bool comboKeyDown = false;
    };

    static BYTE ResolveVk(wchar_t key) noexcept;

    void InstallKeyboardHook(HINSTANCE hInstance);
    static LRESULT CALLBACK LowLevelKeyboardProc(int nCode, WPARAM wParam, LPARAM lParam);
    static void InjectDummyKey() noexcept;

    std::vector<Slot> slots_;
    std::mutex slotsMutex_;  // Guards slots_ config updates (hook thread reads, main thread writes via UpdateHotkey)
    HHOOK keyboardHook_ = nullptr;

    bool modCtrlDown_ = false;
    bool modShiftDown_ = false;
    bool modAltDown_ = false;
    bool modWinDown_ = false;
    bool otherKeyPressed_ = false;

    static std::atomic<HotkeyManager*> s_instance;
};

}  // namespace NextKey
