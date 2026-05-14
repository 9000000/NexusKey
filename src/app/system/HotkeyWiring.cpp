// VKey - Hotkey Wiring Helper Implementation
// SPDX-License-Identifier: GPL-3.0-only

#include "HotkeyWiring.h"
#include "HookEngine.h"
#include "TrayIcon.h"
#include "QuickConvert.h"
#include "core/config/ConfigManager.h"

namespace NextKey {

void WireHotkeys(
    HotkeyManager& hotkeyManager,
    HookEngine& hookEngine,
    TrayIcon& trayIcon,
    std::unique_ptr<QuickConvert>& quickConvert,
    HotkeyManager::SlotId& outToggleSlot,
    HotkeyManager::SlotId& outConvertSlot,
    HINSTANCE hInstance,
    const HotkeyConfig& toggleConfig
) {
    // Load convert config and create QuickConvert
    auto convertConfig = ConfigManager::LoadConvertConfigOrDefault();
    HotkeyConfig convertHotkeyCfg = convertConfig.hotkey;
    quickConvert = std::make_unique<QuickConvert>(convertConfig);

    // Config reload callback. Captures are refs to caller's globals (static lifetime).
    hookEngine.SetConfigReloadCallback([&hotkeyManager, &trayIcon, &quickConvert,
                                        &outToggleSlot, &outConvertSlot]() {
        auto cc = ConfigManager::LoadConvertConfigOrDefault();
        if (quickConvert) quickConvert->UpdateConfig(cc);
        hotkeyManager.UpdateHotkey(outConvertSlot, cc.hotkey);
        trayIcon.RefreshConvertHotkeyCache(cc);

        auto hk = ConfigManager::LoadHotkeyConfigOrDefault();
        hotkeyManager.UpdateHotkey(outToggleSlot, hk);
    });

    // Register hotkeys (toggle V/E + quick convert)
    // Callbacks run on the hook thread — keep them lock-free / PostMessage-style.
    HWND trayWnd = trayIcon.GetMessageWindow();
    outToggleSlot = hotkeyManager.AddHotkey(toggleConfig, [trayWnd]() {
        if (trayWnd) PostMessageW(trayWnd, WM_HOTKEY, 0, 0);
    });
    // Refs to caller's globals — safe because globals outlive the callback
    outConvertSlot = hotkeyManager.AddHotkey(convertHotkeyCfg, [&hookEngine, &quickConvert]() {
        hookEngine.CommitPending();
        if (quickConvert) quickConvert->Execute();
    });

    hotkeyManager.Initialize(hInstance);
}

}  // namespace NextKey
