// VKey - Hotkey Wiring Helper Implementation
// SPDX-License-Identifier: GPL-3.0-only

#include "HotkeyWiring.h"
#include "HookEngine.h"
#include "TrayIcon.h"
#include "QuickConvert.h"
#include "core/config/ConfigManager.h"
#include "core/ipc/SharedStateManager.h"

namespace NextKey {

namespace {

void PublishConvertConfig(SharedStateManager& sharedState,
                          const ConvertConfig& config) noexcept {
    // Drop the native-routing handshake before changing the binding. Until the
    // foreground TSF DLL preserves the new key and raises readiness again, the
    // low-level hook remains the lossless fallback.
    sharedState.SetOrClearFlag(SharedFlags::TSF_NATIVE_CONVERT_READY, false);
    SharedState state = sharedState.Read();
    if (!state.IsValid()) return;
    // No point clearing the bit in `state` — Write() carves TSF_NATIVE_CONVERT_READY
    // out of the snapshot and keeps the live value. The SetOrClearFlag calls that
    // bracket this block are what actually drop it.
    state.SetConvertConfig(config);
    sharedState.Write(state);
    // Close the narrow race where the old preserved-key owner republishes
    // readiness while the new config snapshot is being written.
    sharedState.SetOrClearFlag(SharedFlags::TSF_NATIVE_CONVERT_READY, false);
}

bool ShouldPassQuickConvertToTsf(const void* context) noexcept {
    return static_cast<const HookEngine*>(context)->ShouldUseNativeQuickConvert();
}

}  // namespace

void WireHotkeys(
    HotkeyManager& hotkeyManager,
    HookEngine& hookEngine,
    TrayIcon& trayIcon,
    SharedStateManager& sharedState,
    std::unique_ptr<QuickConvert>& quickConvert,
    HotkeyManager::SlotId& outToggleSlot,
    HotkeyManager::SlotId& outConvertSlot,
    const HotkeyConfig& toggleConfig
) {
    // Load convert config and create QuickConvert
    auto convertConfig = ConfigManager::LoadConvertConfigOrDefault();
    HotkeyConfig convertHotkeyCfg = convertConfig.hotkey;
    quickConvert = std::make_unique<QuickConvert>(convertConfig);
    PublishConvertConfig(sharedState, convertConfig);

    // Config reload callback. Captures are refs to caller's globals (static lifetime).
    //
    // Wave 3 PR 3.8 — toggle V/E hotkey is no longer reloaded here. The
    // live SharedState bus (HookEngine::hotkeyChangedCallback_, wired in
    // main.cpp / main_lite.cpp) propagates the binding in ~ms instead of
    // ~30s (TOML deferred save). Reading toggle hotkey from TOML here
    // would race the SharedState update and overwrite the fresh binding
    // with a stale disk value. Convert config is also published to SharedState
    // here so the in-process TSF backend can refresh its preserved key and
    // transformation options from the same snapshot.
    (void)outToggleSlot;  // referenced by hotkeyChangedCallback_ in caller
    hookEngine.SetConfigReloadCallback([&hotkeyManager, &trayIcon, &sharedState, &quickConvert,
                                        &outConvertSlot]() {
        auto cc = ConfigManager::LoadConvertConfigOrDefault();
        if (quickConvert) quickConvert->UpdateConfig(cc);
        hotkeyManager.UpdateHotkey(outConvertSlot, cc.hotkey);
        PublishConvertConfig(sharedState, cc);
        trayIcon.RefreshConvertHotkeyCache(cc);
    });

    // Register hotkeys (toggle V/E + quick convert).
    //
    // Both callbacks are always deferred through HookLifecycle's hook-thread
    // message pump. Match() receives only the no-throw pass-through function
    // pointer/context used to hand native Quick Convert to an active TSF host.
    HWND trayWnd = trayIcon.GetMessageWindow();
    outToggleSlot = hotkeyManager.AddHotkey(toggleConfig, [trayWnd]() {
        if (trayWnd) PostMessageW(trayWnd, WM_HOTKEY, 0, 0);
    });
    // Refs to caller's globals — safe because globals outlive the callback
    outConvertSlot = hotkeyManager.AddHotkey(convertHotkeyCfg, [&hookEngine, &quickConvert]() {
        hookEngine.CommitPending();
        if (quickConvert) quickConvert->Execute();
    }, {&ShouldPassQuickConvertToTsf, &hookEngine});
}

}  // namespace NextKey
