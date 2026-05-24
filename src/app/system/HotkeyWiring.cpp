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

    // Register hotkeys (toggle V/E + quick convert).
    //
    // Wave 3 PR 3.7 — per-callback thread-affinity declaration. See
    // `docs/CODING_RULES/12-worker-thread-doctrine.md` §12.3 + HotkeyManager.h
    // for the contract semantics.
    //
    //   Toggle V/E callback: lambda body is a single `PostMessageW` to the
    //   tray's message window — Win32 cross-thread-safe, no shared state
    //   writes. `runsOnAnyThread = true`.
    //
    //   Convert callback: `hookEngine.CommitPending()` mutates `engine_`,
    //   which Rule 11.3 pins to the hook thread (single-writer). The
    //   callback MUST run on the hook thread; if hookThreadId is 0 the
    //   dispatch drops rather than execute on the wrong thread.
    //   `runsOnAnyThread = false`.
    HWND trayWnd = trayIcon.GetMessageWindow();
    outToggleSlot = hotkeyManager.AddHotkey(toggleConfig, [trayWnd]() {
        if (trayWnd) PostMessageW(trayWnd, WM_HOTKEY, 0, 0);
    }, /*runsOnAnyThread=*/true);
    // Refs to caller's globals — safe because globals outlive the callback
    outConvertSlot = hotkeyManager.AddHotkey(convertHotkeyCfg, [&hookEngine, &quickConvert]() {
        hookEngine.CommitPending();
        if (quickConvert) quickConvert->Execute();
    }, /*runsOnAnyThread=*/false);

    // Wave 3 PR 3.7 — caller must pass HookEngine's hook-thread id at
    // Initialize time. Prereq: caller has already invoked
    // `hookEngine.Start()` and we read the now-valid thread id directly.
    // The old two-step `SetHookThreadId() + Initialize()` contract had a
    // race window where the LL hook was installed before the tid was
    // published; collapsing into one call closes that window.
    hotkeyManager.Initialize(hInstance, hookEngine.GetHookThreadId());
}

}  // namespace NextKey
