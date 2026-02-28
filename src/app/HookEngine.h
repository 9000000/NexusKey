// NexusKey - Keyboard Hook Engine
// SPDX-License-Identifier: GPL-3.0-only
//
// Single-process Vietnamese input using WH_KEYBOARD_LL.
// Replaces TSF DLL for MVP — no COM registration, no admin elevation.

#pragma once

#include "core/engine/IInputEngine.h"
#include "core/TypingConfig.h"
#include "core/ConfigEvent.h"
#include "core/SmartSwitchManager.h"
#include <Windows.h>
#include <functional>
#include <memory>
#include <set>
#include <string>
#include <unordered_map>

namespace NextKey {

/// Callback when Vietnamese/English mode changes
using ModeChangeCallback = std::function<void(bool vietnamese)>;

/// Keyboard hook engine — intercepts keystrokes, processes Vietnamese input,
/// outputs via SendInput backspace+retype. Absorbs HotkeyManager logic.
class HookEngine {
public:
    HookEngine();
    ~HookEngine();

    HookEngine(const HookEngine&) = delete;
    HookEngine& operator=(const HookEngine&) = delete;

    /// Start the hook engine (installs keyboard hook + focus hook)
    bool Start(HINSTANCE hInstance, const TypingConfig& config, const HotkeyConfig& hotkey);

    /// Stop and unhook everything
    void Stop();

    /// Toggle Vietnamese/English mode
    void ToggleVietnameseMode();

    /// Set callback for mode changes (to update tray icon)
    void SetModeChangeCallback(ModeChangeCallback callback) { modeChangeCallback_ = std::move(callback); }

    /// Check for config changes and reload if needed
    bool CheckConfigEvent();

    [[nodiscard]] bool IsVietnameseMode() const noexcept { return vietnameseMode_; }
    [[nodiscard]] bool IsRunning() const noexcept { return keyboardHook_ != nullptr; }

    // Magic number to mark our own SendInput events (prevents other hooks from processing them)
    static constexpr ULONG_PTR NEXUSKEY_EXTRA_INFO = 0x4E4B;  // "NK"

private:
    // Hook callbacks (static → instance dispatch)
    static LRESULT CALLBACK LowLevelKeyboardProc(int nCode, WPARAM wParam, LPARAM lParam);
    static void CALLBACK WinEventProc(HWINEVENTHOOK hHook, DWORD event, HWND hwnd,
                                       LONG idObject, LONG idChild,
                                       DWORD dwEventThread, DWORD dwmsEventTime);

    // Core processing
    bool ProcessKeyDown(DWORD vkCode, DWORD scanCode, DWORD flags);
    bool ProcessKeyUp(DWORD vkCode, DWORD flags);

    // Input engine interaction
    void HandleAlphaKey(DWORD vkCode);
    void HandleBackspace();
    void CommitComposition();
    void ResetComposition();

    // Output — multi-method: PostMessage for Win32 controls, SendInput for others
    void ReplaceComposition(const std::wstring& newText);
    void SendBackspaces(size_t count);
    void SendBackspaceEvents(HWND target, size_t count, bool usePost);
    void SendCharEvents(HWND target, const std::wstring& text, bool usePost);

    // Hotkey detection (absorbed from HotkeyManager)
    void TrackModifier(DWORD vkCode, bool isDown);
    bool CheckHotkeyMatch() const;

    // Commit trigger check
    static bool IsCommitTrigger(DWORD vkCode);

    // Browser detection for Chrome autocomplete fix
    static bool IsBrowserLike(HWND hwnd);

    // Smart switch: get foreground app exe name
    static std::wstring GetForegroundExeName();
    void OnFocusChanged();

    // Engine state
    std::unique_ptr<IInputEngine> engine_;
    InputMethod currentMethod_ = InputMethod::Telex;
    std::wstring previousComposition_;  // What's currently displayed in the app
    bool vietnameseMode_ = true;
    bool sending_ = false;  // True while SendInput is in progress (skip re-entrant hook calls)
    bool beepOnSwitch_ = false;
    bool smartSwitch_ = false;
    bool excludeApps_ = false;
    bool autoCaps_ = false;
    int autoCapState_ = 0;  // 0=normal, 1=after punct, 2=after punct+space
    std::set<std::wstring> excludedAppSet_;  // sorted, O(log n) lookup
    bool isExcludedApp_ = false;  // cached: is current foreground app excluded?
    bool modeBeforeExclude_ = true;  // Vietnamese mode before entering excluded app
    std::unordered_map<std::wstring, bool> appModeMap_;  // exe name → vietnamese mode (for TOML save)
    SmartSwitchManager smartSwitchMgr_;  // Shared memory for per-app mode
    std::wstring currentExe_;  // Currently focused app

    // Hooks
    HHOOK keyboardHook_ = nullptr;
    HWINEVENTHOOK focusHook_ = nullptr;

    // Hotkey state
    HotkeyConfig hotkeyConfig_{};
    BYTE hotkeyVk_ = 0;  // Pre-computed VK code for hotkeyConfig_.key
    bool modCtrlDown_ = false;
    bool modShiftDown_ = false;
    bool modAltDown_ = false;
    bool modWinDown_ = false;
    bool otherKeyPressed_ = false;

    // Config reload
    ConfigEvent configEvent_;

    // Callback
    ModeChangeCallback modeChangeCallback_;

    // Singleton for static callback dispatch
    static HookEngine* s_instance;
};

}  // namespace NextKey
