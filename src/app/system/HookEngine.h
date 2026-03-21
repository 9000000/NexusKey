// NexusKey - Keyboard Hook Engine
// SPDX-License-Identifier: GPL-3.0-only
//
// Single-process Vietnamese input using WH_KEYBOARD_LL.
// Replaces TSF DLL for MVP — no COM registration, no admin elevation.

#pragma once

#include "core/engine/IInputEngine.h"
#include "core/engine/CodeTableConverter.h"
#include "core/config/TypingConfig.h"
#include "core/config/ConfigEvent.h"
#include "core/SmartSwitchManager.h"
#include <Windows.h>
#include <functional>
#include <atomic>
#include <memory>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

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

    /// Set callback for quick-convert hotkey
    void SetConvertCallback(std::function<void()> callback) { convertCallback_ = std::move(callback); }

    /// Set callback for config reload (notifies main to update QuickConvert etc.)
    void SetConfigReloadCallback(std::function<void()> callback) { configReloadCallback_ = std::move(callback); }

    /// Set callback for TSF active state changes (foreground app is/isn't in TSF list)
    void SetTsfActiveCallback(std::function<void(bool)> callback) { tsfActiveCallback_ = std::move(callback); }

    /// Update the convert hotkey config (called on config reload)
    void SetConvertHotkey(const HotkeyConfig& hotkey);

    /// Check for config changes and reload if needed
    bool CheckConfigEvent();

    /// Change code table (commits pending composition, updates per-app map)
    void SetCodeTable(CodeTable ct);

    /// Get effective code table (checks per-app map when rememberCodeTable is on)
    [[nodiscard]] CodeTable GetCodeTable() const noexcept;

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
    static LRESULT CALLBACK LowLevelMouseProc(int nCode, WPARAM wParam, LPARAM lParam);

    // Core processing
    bool ProcessKeyDown(DWORD vkCode, DWORD scanCode, DWORD flags);
    bool ProcessKeyUp(DWORD vkCode, DWORD flags);

    // Input engine interaction
    [[nodiscard]] bool HandleAlphaKey(DWORD vkCode);  // Returns true if keystroke should be eaten
    void HandleBackspace();
    bool CommitComposition();  // Returns true if auto-restore changed text
    void ResetComposition();

    // Output — multi-method: PostMessage for Win32 controls, SendInput for others
    void ReplaceComposition(const std::wstring& newText);
    void SendBackspaces(size_t count);
    void SendBackspaceEvents(HWND target, size_t count, bool usePost);
    void SendCharEvents(HWND target, const std::wstring& text, bool usePost);

    // Hotkey detection (absorbed from HotkeyManager)
    void TrackModifier(DWORD vkCode, bool isDown);
    bool CheckHotkeyMatch() const;
    bool CheckConvertHotkeyMatch() const;

    // Backspace-into-committed-word: replay saved chars to restore engine state
    void ReplayCommittedChars();

    // Commit trigger check
    static bool IsCommitTrigger(DWORD vkCode);

    // Re-inject a key after auto-restore replacement
    void InjectKey(DWORD vkCode);

    // Qt/Electron detection — skip U+202F to avoid first-word delay
    static bool IsQtElectronApp(HWND hwnd);

    // Console detection — skip U+202F and add Sleep(2) before character injection
    static bool IsConsoleApp(HWND hwnd);

    // Smart switch: get foreground app exe name
    static std::wstring GetForegroundExeName();
    void OnFocusChanged();

    // Engine state
    std::unique_ptr<IInputEngine> engine_;
    InputMethod currentMethod_ = InputMethod::Telex;
    std::wstring previousComposition_;  // What's currently displayed in the app
    std::vector<uint8_t> previousEncodedWidths_;  // Output unit count per Unicode char (for non-Unicode code tables)
    bool vietnameseMode_ = true;
    std::atomic<bool> sending_{false};  // True while SendInput is in progress (skip re-entrant hook calls)
    int synthEventsPending_ = 0;  // Count of synthetic INPUT structs sent but not yet processed by hook
    bool beepOnSwitch_ = false;
    bool smartSwitch_ = false;
    bool excludeApps_ = false;
    bool tsfApps_ = false;
    bool autoCaps_ = false;
    bool tempOffSpellByCtrl_ = false;
    bool tempOffByAlt_ = false;
    bool tempEngineOff_ = false;       // True = Vietnamese bypassed for current word
    int altTapCount_ = 0;              // 0 or 1 (waiting for second tap)
    DWORD lastAltReleaseTime_ = 0;     // GetTickCount() of first Alt release
    static constexpr DWORD DOUBLE_ALT_TIMEOUT_MS = 400;
    int autoCapState_ = 0;  // 0=normal, 1=after punct, 2=after punct+space
    std::set<std::wstring> excludedAppSet_;  // sorted, O(log n) lookup
    bool isExcludedApp_ = false;  // cached: is current foreground app excluded?
    std::set<std::wstring> tsfAppSet_;      // apps that should use TSF engine instead of hook
    bool isTsfApp_ = false;       // cached: is current foreground app in TSF list?
    bool isConsoleApp_ = false;   // cached: is current foreground app a console emulator?
    bool skipEmptyChar_ = false;  // Skip U+202F for Qt/Electron and Console apps
    bool modeBeforeExclude_ = true;  // Vietnamese mode before entering excluded app
    std::unordered_map<std::wstring, bool> appModeMap_;  // exe name → vietnamese mode (for TOML save)
    SmartSwitchManager smartSwitchMgr_;  // Shared memory for per-app mode
    std::wstring currentExe_;  // Currently focused app
    std::wstring previousExe_;  // Previously focused app (for tray menu context)
    bool rememberCodeTable_ = false;
    CodeTable currentCodeTable_ = CodeTable::Unicode;
    std::unordered_map<std::wstring, uint8_t> appCodeTableMap_;  // exe → CodeTable value

    // Backspace-into-committed-word (re-enter composition after commit + backspace)
    // inputHistory_ records exact user keystrokes (including backspace as '\b')
    // so replay produces identical engine state. This differs from engine's rawInput_
    // which mutates on escape sequences (EraseConsumedRaw).
    static constexpr wchar_t kBackspaceMarker = L'\b';
    static constexpr size_t kMaxCommitStack = 3;  // Max words to remember for backward
    // Auto-expire the Ready state after this many ms — cheap insurance against any
    // cursor-movement event that bypasses ResetComposition (e.g. future edge cases).
    static constexpr DWORD kCommitUndoTimeoutMs = 4000;

    enum class CommitUndoState : uint8_t {
        Idle   = 0,  // No pending undo
        Ready  = 1,  // Just committed with Space/Enter — waiting for first BS
        Primed = 2,  // Space deleted — next Alpha/BS triggers replay
    };

    struct CommitEntry {
        std::vector<wchar_t> history;   // User keystrokes for replay
        std::wstring text;              // What was on screen when committed
        std::vector<uint8_t> widths;    // Encoded widths for non-Unicode code tables
    };

    std::vector<wchar_t> inputHistory_;         // User keystrokes for current composition
    std::vector<CommitEntry> commitStack_;       // Stack of committed words (LIFO, max kMaxCommitStack)
    bool pushedToStack_ = false;                 // True if last CommitComposition pushed to stack
    CommitUndoState commitUndoState_ = CommitUndoState::Idle;
    DWORD commitReadyTime_ = 0;                 // GetTickCount() when entering Ready state

    // Macro expansion
    bool macroEnabled_ = false;
    bool macroInEnglish_ = false;
    bool tempOffMacroByEsc_ = false;  // Config: Esc can temp-disable macro
    bool tempMacroOff_ = false;       // Runtime: macro disabled for current word
    std::unordered_map<std::wstring, std::wstring> macroTable_;
    std::wstring rawMacroBuffer_;

    // Hooks
    HHOOK keyboardHook_ = nullptr;
    HHOOK mouseHook_ = nullptr;
    HWINEVENTHOOK focusHook_ = nullptr;

    // Hotkey state
    HotkeyConfig hotkeyConfig_{};
    BYTE hotkeyVk_ = 0;  // Pre-computed VK code for hotkeyConfig_.key
    HotkeyConfig convertHotkeyConfig_{};
    BYTE convertHotkeyVk_ = 0;  // Pre-computed VK for convert hotkey
    bool modCtrlDown_ = false;
    bool modShiftDown_ = false;
    bool modAltDown_ = false;
    bool modWinDown_ = false;
    bool otherKeyPressed_ = false;

    // Config reload
    ConfigEvent configEvent_;

    // Callbacks
    ModeChangeCallback modeChangeCallback_;
    std::function<void()> convertCallback_;
    std::function<void()> configReloadCallback_;
    std::function<void(bool)> tsfActiveCallback_;

    // Singleton for static callback dispatch (read from hook callback thread)
    static std::atomic<HookEngine*> s_instance;
};

}  // namespace NextKey
