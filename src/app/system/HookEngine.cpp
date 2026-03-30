// NexusKey - Keyboard Hook Engine Implementation
// SPDX-License-Identifier: GPL-3.0-only

#include "HookEngine.h"
#include "helpers/AppHelpers.h"
#include "core/engine/CodeTableConverter.h"
#include "core/engine/EngineFactory.h"
#include "core/config/ConfigManager.h"
#include "core/ipc/SharedStateManager.h"
#include "core/Debug.h"
#include <algorithm>
#include <cstdio>
#include <vector>

namespace NextKey {

// ═══════════════════════════════════════════════════════════
// Debug file logger (writes to NexusKey_hook.log next to EXE)
// ═══════════════════════════════════════════════════════════
#if defined(_DEBUG) || defined(NEXTKEY_DEBUG)
static FILE* g_hookLog = nullptr;
static LARGE_INTEGER g_hookLogFreq = {};  // Cached — doesn't change

static void OpenHookLog() {
    if (g_hookLog) return;
    QueryPerformanceFrequency(&g_hookLogFreq);
    wchar_t exePath[MAX_PATH];
    GetModuleFileNameW(nullptr, exePath, MAX_PATH);
    std::wstring logPath(exePath);
    auto pos = logPath.find_last_of(L"\\/");
    if (pos != std::wstring::npos) logPath = logPath.substr(0, pos + 1);
    logPath += L"NexusKey_hook.log";
    (void)_wfopen_s(&g_hookLog, logPath.c_str(), L"w");
    if (g_hookLog) setvbuf(g_hookLog, nullptr, _IONBF, 0);  // Unbuffered — flush every line (debug builds only)
}

static void CloseHookLog() {
    if (g_hookLog) { fflush(g_hookLog); fclose(g_hookLog); g_hookLog = nullptr; }
}

static void HookLog(const wchar_t* format, ...) {
    if (!g_hookLog) return;
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    double ms = (now.QuadPart * 1000.0) / g_hookLogFreq.QuadPart;
    fwprintf(g_hookLog, L"[%012.3f] ", ms);
    va_list args;
    va_start(args, format);
    vfwprintf(g_hookLog, format, args);
    va_end(args);
    fputwc(L'\n', g_hookLog);
    // No fflush here — uses 8KB buffer, flushed on close
}
#define HOOK_LOG(fmt, ...) HookLog(fmt, ##__VA_ARGS__)
#else
#define HOOK_LOG(...) ((void)0)
#endif

std::atomic<HookEngine*> HookEngine::s_instance{nullptr};

HookEngine::HookEngine() = default;

HookEngine::~HookEngine() {
    Stop();
}

void HookEngine::ApplyConfig(const TypingConfig& config) {
    beepOnSwitch_ = config.beepOnSwitch;
    smartSwitch_ = config.smartSwitch;
    excludeApps_ = config.excludeApps;
    tsfApps_ = config.tsfApps;
    autoCaps_ = config.autoCaps;
    tempOffSpellByCtrl_ = config.tempOffSpellByCtrl;
    tempOffByAlt_ = config.tempOffByAlt;
    macroEnabled_ = config.macroEnabled;
    macroInEnglish_ = config.macroInEnglish;
    tempOffMacroByEsc_ = config.tempOffMacroByEsc;
    autoCapsMacro_ = config.autoCapsMacro;
}

bool HookEngine::Start(HINSTANCE hInstance, const TypingConfig& config, const HotkeyConfig& hotkey) {
    if (keyboardHook_) return false;  // Already running

#if defined(_DEBUG) || defined(NEXTKEY_DEBUG)
    OpenHookLog();
    HOOK_LOG(L"=== HookEngine::Start ===");
#endif

    s_instance = this;
    hotkeyConfig_ = hotkey;
    currentMethod_ = config.inputMethod;
    config_ = config;
    ApplyConfig(config);
    if (macroEnabled_) {
        macroTable_ = ConfigManager::LoadMacros(ConfigManager::GetConfigPath());
    }
    autoCapState_ = 0;
    engine_ = EngineFactory::Create(config);

    // Create shared memory for smart switch (RAM-only, no TOML persistence)
    if (smartSwitch_) {
        (void)smartSwitchMgr_.Create();
    }

    currentCodeTable_ = config.codeTable;
    globalCodeTable_ = config.codeTable;
    globalInputMethod_ = config.inputMethod;

    // Load manual per-app overrides (encoding + input method)
    ReloadAppOverrides();

    // Load excluded apps list
    if (excludeApps_) {
        auto apps = ConfigManager::LoadExcludedApps(ConfigManager::GetConfigPath());
        excludedAppSet_.clear();
        for (auto& app : apps) {
            excludedAppSet_.insert(std::move(app));
        }
    }

    // Load TSF apps list (apps that use TSF engine instead of hook)
    if (tsfApps_) {
        auto apps = ConfigManager::LoadTsfApps(ConfigManager::GetConfigPath());
        tsfAppSet_.clear();
        for (auto& app : apps) {
            tsfAppSet_.insert(std::move(app));
        }
    }

    // Pre-compute VK code for hotkey character (layout-aware)
    hotkeyVk_ = 0;
    if (hotkeyConfig_.key != 0) {
        SHORT vkResult = VkKeyScanW(hotkeyConfig_.key);
        if (vkResult != -1) {
            hotkeyVk_ = LOBYTE(vkResult);
        }
    }

    // Initialize config event for reload detection
    configEvent_.Initialize();

    // Install low-level keyboard hook (global, all threads)
    keyboardHook_ = SetWindowsHookExW(WH_KEYBOARD_LL, LowLevelKeyboardProc, hInstance, 0);
    if (!keyboardHook_) {
        NEXTKEY_LOG(L"HookEngine: Failed to install keyboard hook (error: %lu)", GetLastError());
        HOOK_LOG(L"FAILED to install keyboard hook (error: %lu)", GetLastError());
        return false;
    }

    // Install mouse hook to reset composition on left click
    // (handles focus changes within same window, e.g. YouTube video → comment box)
    mouseHook_ = SetWindowsHookExW(WH_MOUSE_LL, LowLevelMouseProc, hInstance, 0);

    // Install focus change hook to reset composition on window switch
    focusHook_ = SetWinEventHook(
        EVENT_SYSTEM_FOREGROUND, EVENT_SYSTEM_FOREGROUND,
        nullptr, WinEventProc,
        0, 0, WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS);

    NEXTKEY_LOG(L"HookEngine started (method=%d, vietnamese=%d)",
                static_cast<int>(currentMethod_), vietnameseMode_);
    HOOK_LOG(L"Hook installed OK (method=%d, vietnamese=%d)",
             static_cast<int>(currentMethod_), vietnameseMode_);
    return true;
}

void HookEngine::Stop() {
    HOOK_LOG(L"=== HookEngine::Stop ===");
    // Persist per-app code table data (only entries differing from global)
    if (keyboardHook_) {
        UnhookWindowsHookEx(keyboardHook_);
        keyboardHook_ = nullptr;
    }
    if (mouseHook_) {
        UnhookWindowsHookEx(mouseHook_);
        mouseHook_ = nullptr;
    }
    if (focusHook_) {
        UnhookWinEvent(focusHook_);
        focusHook_ = nullptr;
    }
    if (s_instance == this) {
        s_instance = nullptr;
    }
    NEXTKEY_LOG(L"HookEngine stopped");
#if defined(_DEBUG) || defined(NEXTKEY_DEBUG)
    CloseHookLog();
#endif
}

void HookEngine::ToggleVietnameseMode() {
    // Block toggling in excluded apps
    if (excludeApps_ && isExcludedApp_) {
        HOOK_LOG(L"  ToggleVietnameseMode: BLOCKED (excluded app '%s')", currentExe_.c_str());
        return;
    }

    layoutForcedEnglish_ = false;   // User overriding auto-detection — clear forced state
    preLayoutSwitchMode_ = false;   // Stale saved-mode is no longer meaningful

    // Commit any pending composition before switching
    if (engine_->Count() > 0) {
        CommitComposition();
    }

    // Cancel backspace-into-committed-word (replay in wrong mode would be wrong)
    CancelCommitUndo();

    vietnameseMode_ = !vietnameseMode_;
    NEXTKEY_LOG(L"HookEngine: mode = %s", vietnameseMode_ ? L"Vietnamese" : L"English");

    // Save per-app mode
    if (smartSwitch_) {
        if (currentExe_.empty()) {
            currentExe_ = GetForegroundExeName();
        }
        if (!currentExe_.empty()) {
            appModeMap_[currentExe_] = vietnameseMode_;
            smartSwitchMgr_.SetAppMode(currentExe_, vietnameseMode_);
        }
    }

    if (beepOnSwitch_) {
        MessageBeep(vietnameseMode_ ? MB_OK : MB_ICONASTERISK);
    }

    if (modeChangeCallback_) {
        modeChangeCallback_(vietnameseMode_);
    }
}

void HookEngine::SetCodeTable(CodeTable ct) {
    // Commit any pending composition before switching
    if (ct != currentCodeTable_ && engine_->Count() > 0) {
        CommitComposition();
    }

    currentCodeTable_ = ct;

}

CodeTable HookEngine::GetCodeTable() const noexcept {
    // Priority 1: Manual per-app override (set explicitly by user)
    // Prefer previousExe_ — when the user right-clicks the tray, currentExe_ changes
    // to explorer.exe (shell), but previousExe_ is the app they were actually using.
    auto lookupOverride = [&](const std::wstring& exe) -> const int8_t* {
        if (exe.empty()) return nullptr;
        auto it = appEncodingOverrides_.find(exe);
        return (it != appEncodingOverrides_.end() && it->second >= 0) ? &it->second : nullptr;
    };
    if (auto* v = lookupOverride(previousExe_)) return static_cast<CodeTable>(*v);
    if (auto* v = lookupOverride(currentExe_)) return static_cast<CodeTable>(*v);

    return currentCodeTable_;
}

bool HookEngine::CheckConfigEvent() {
    if (!configEvent_.IsValid()) {
        configEvent_.Initialize();
    }

    if (!configEvent_.Wait(0)) {
        return false;  // No signal
    }

    NEXTKEY_LOG(L"HookEngine: config event received, reloading");

    // Read TOML for fields not in SharedState (beep, smartSwitch, excludeApps, hotkey)
    auto config = ConfigManager::LoadOrDefault();

    // Override with SharedState for fields that Settings updates immediately
    // (TOML may be stale due to deferred save)
    SharedStateManager sharedState;
    if (sharedState.Open()) {
        SharedState state = sharedState.Read();
        if (state.IsValid()) {
            config.inputMethod = static_cast<InputMethod>(state.inputMethod);
            config.spellCheckEnabled = state.spellCheck != 0;
            DecodeFeatureFlags(state.GetFeatureFlags(), config);
            NEXTKEY_LOG(L"HookEngine: read SharedState (epoch=%u, featureFlags=0x%04X)",
                        state.epoch, state.GetFeatureFlags());
        }
    }

    // Recreate engine with updated config (engine stores a copy of TypingConfig)
    if (engine_->Count() > 0) {
        CommitComposition();
    }
    currentMethod_ = config.inputMethod;
    config_ = config;
    engine_ = EngineFactory::Create(config);
    NEXTKEY_LOG(L"HookEngine: engine recreated (%s, modernOrtho=%d, allowZwjf=%d)",
                currentMethod_ == InputMethod::VNI ? L"VNI" : L"Telex",
                config.modernOrtho ? 1 : 0, config.allowZwjf ? 1 : 0);
    ApplyConfig(config);
    if (macroEnabled_) {
        macroTable_ = ConfigManager::LoadMacros(ConfigManager::GetConfigPath());
    } else {
        macroTable_.clear();
    }

    currentCodeTable_ = config.codeTable;
    globalCodeTable_ = config.codeTable;
    globalInputMethod_ = config.inputMethod;

    // Reload manual per-app overrides (encoding + input method)
    ReloadAppOverrides();

    // Reload excluded apps list
    if (excludeApps_) {
        auto apps = ConfigManager::LoadExcludedApps(ConfigManager::GetConfigPath());
        excludedAppSet_.clear();
        for (auto& app : apps) {
            excludedAppSet_.insert(std::move(app));
        }
    } else {
        excludedAppSet_.clear();
        isExcludedApp_ = false;
    }

    // Reload TSF apps list
    if (tsfApps_) {
        auto apps = ConfigManager::LoadTsfApps(ConfigManager::GetConfigPath());
        tsfAppSet_.clear();
        for (auto& app : apps) {
            tsfAppSet_.insert(std::move(app));
        }
    } else {
        tsfAppSet_.clear();
    }
    // Re-evaluate TSF app status for current foreground app
    bool wasTsfApp = isTsfApp_;
    if (tsfApps_ && !isExcludedApp_ && !tsfAppSet_.empty() && !currentExe_.empty()) {
        isTsfApp_ = tsfAppSet_.count(currentExe_) > 0;
    } else {
        isTsfApp_ = false;
    }
    if (isTsfApp_ != wasTsfApp && tsfActiveCallback_) {
        tsfActiveCallback_(isTsfApp_);
    }

    // Re-apply per-app overrides for current app (OnFocusChanged may have run with stale maps)
    if (!currentExe_.empty() && !isExcludedApp_ && !isTsfApp_) {
        // Encoding
        {
            auto it = appEncodingOverrides_.find(currentExe_);
            currentCodeTable_ = (it != appEncodingOverrides_.end())
                ? static_cast<CodeTable>(it->second) : globalCodeTable_;
        }
        // Input method — recreate engine only if method changed
        {
            auto it = appInputMethodOverrides_.find(currentExe_);
            InputMethod targetMethod = (it != appInputMethodOverrides_.end())
                ? static_cast<InputMethod>(it->second) : globalInputMethod_;
            if (targetMethod != currentMethod_) {
                currentMethod_ = targetMethod;
                TypingConfig engineConfig = config_;
                engineConfig.inputMethod = targetMethod;
                engine_ = EngineFactory::Create(engineConfig);
                NEXTKEY_LOG(L"HookEngine: re-applied inputMethod=%d for '%s'",
                            static_cast<int>(currentMethod_), currentExe_.c_str());
            }
        }
    }

    // Reload hotkey config — prefer SharedState (instant), fallback to TOML
    {
        HotkeyConfig newHotkey{};
        bool fromSharedState = false;
        SharedStateManager hkState;
        if (hkState.Open()) {
            SharedState st = hkState.Read();
            if (st.IsValid()) {
                newHotkey = st.GetHotkey();
                fromSharedState = true;
            }
        }
        if (!fromSharedState) {
            auto hotkeyOpt = ConfigManager::LoadHotkeyConfig(ConfigManager::GetConfigPath());
            if (hotkeyOpt) newHotkey = *hotkeyOpt;
        }
        hotkeyConfig_ = newHotkey;
        hotkeyVk_ = 0;
        if (hotkeyConfig_.key != 0) {
            SHORT vkResult = VkKeyScanW(hotkeyConfig_.key);
            if (vkResult != -1) {
                hotkeyVk_ = LOBYTE(vkResult);
            }
        }
    }

    // Reload convert hotkey config
    auto convertConfigOpt = ConfigManager::LoadConvertConfig(ConfigManager::GetConfigPath());
    if (convertConfigOpt) {
        SetConvertHotkey(convertConfigOpt->hotkey);
    }

    // Notify main process to update QuickConvert config etc.
    if (configReloadCallback_) {
        configReloadCallback_();
    }

    return true;
}

// ═══════════════════════════════════════════════════════════
// Static Hook Callbacks → Instance Dispatch
// ═══════════════════════════════════════════════════════════

LRESULT CALLBACK HookEngine::LowLevelKeyboardProc(int nCode, WPARAM wParam, LPARAM lParam) {
    HookEngine* self = s_instance.load(std::memory_order_relaxed);
    if (nCode == HC_ACTION && self) {
        auto* pKey = reinterpret_cast<KBDLLHOOKSTRUCT*>(lParam);

        // Skip our own injected events (dwExtraInfo magic number — primary method)
        if (pKey->dwExtraInfo == NEXUSKEY_EXTRA_INFO) {
            HOOK_LOG(L"  PASSTHRU (dwExtraInfo=NK): vk=0x%02X scan=0x%04X flags=0x%08X",
                     pKey->vkCode, pKey->scanCode, pKey->flags);
            if (self->synthEventsPending_ > 0) --self->synthEventsPending_;
            return CallNextHookEx(nullptr, nCode, wParam, lParam);
        }

        // Skip events while we're sending (safety backup)
        if (self->sending_) {
            HOOK_LOG(L"  PASSTHRU (sending_): vk=0x%02X scan=0x%04X flags=0x%08X",
                     pKey->vkCode, pKey->scanCode, pKey->flags);
            return CallNextHookEx(nullptr, nCode, wParam, lParam);
        }

        bool isDown = (wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN);
        bool isUp = (wParam == WM_KEYUP || wParam == WM_SYSKEYUP);

        HOOK_LOG(L"KEY vk=0x%02X scan=0x%04X flags=0x%08X %s",
                 pKey->vkCode, pKey->scanCode, pKey->flags,
                 isDown ? L"DOWN" : (isUp ? L"UP" : L"OTHER"));

        if (isDown) {
            if (self->ProcessKeyDown(pKey->vkCode, pKey->scanCode, pKey->flags)) {
                HOOK_LOG(L"  → EATEN (key-down vk=0x%02X)", pKey->vkCode);
                return 1;  // Eat the keystroke
            }
        } else if (isUp) {
            if (self->ProcessKeyUp(pKey->vkCode, pKey->flags)) {
                return 1;  // Eat the keystroke
            }
        }
    }

    return CallNextHookEx(nullptr, nCode, wParam, lParam);
}

void CALLBACK HookEngine::WinEventProc(HWINEVENTHOOK, DWORD, HWND, LONG, LONG, DWORD, DWORD) {
    HookEngine* self = s_instance.load(std::memory_order_relaxed);
    if (self) {
        HOOK_LOG(L"FOCUS changed — resetting composition (engine count=%zu, prev='%s')",
                 self->engine_->Count(), self->previousComposition_.c_str());
        self->autoCapState_ = 0;
        self->OnFocusChanged();
    }
}

LRESULT CALLBACK HookEngine::LowLevelMouseProc(int nCode, WPARAM wParam, LPARAM lParam) {
    if (nCode == HC_ACTION && wParam == WM_LBUTTONDOWN) {
        HookEngine* self = s_instance.load(std::memory_order_relaxed);
        if (self) {
            HOOK_LOG(L"MOUSE click — resetting composition (engine count=%zu, prev='%s')",
                     self->engine_->Count(), self->previousComposition_.c_str());
            // Always reset, even when engine is idle: commitUndoState_ and commitStack_
            // may hold a previously committed word. If not cleared here, a click elsewhere
            // followed by Backspace triggers ReplayCommittedChars() at the new cursor
            // position — identical to the Ctrl+A bug.
            self->ResetComposition();
        }
    }
    return CallNextHookEx(nullptr, nCode, wParam, lParam);
}

// Forward declaration for file-scope helper used in ProcessKeyDown
static HWND GetInputTarget();

// ═══════════════════════════════════════════════════════════
// Core Processing
// ═══════════════════════════════════════════════════════════

bool HookEngine::ProcessKeyDown(DWORD vkCode, DWORD /*scanCode*/, DWORD /*flags*/) {
    // 0. Check for config changes from Settings subprocess
    CheckConfigEvent();

    // 0b. TSF app — let TSF DLL handle all input, hook does nothing
    if (isTsfApp_) return false;

    // 1. Track modifiers for hotkey detection
    bool isModifier = (vkCode == VK_LCONTROL || vkCode == VK_RCONTROL ||
                       vkCode == VK_LSHIFT || vkCode == VK_RSHIFT ||
                       vkCode == VK_LMENU || vkCode == VK_RMENU ||
                       vkCode == VK_LWIN || vkCode == VK_RWIN);

    if (isModifier) {
        TrackModifier(vkCode, true);
        return false;  // Don't eat modifier keys
    }

    // 2. Check modifier+key hotkey (e.g., Alt+~) BEFORE invalidating modifier-only combo
    if (hotkeyVk_ != 0 && vkCode == hotkeyVk_ && CheckHotkeyMatch()) {
        HOOK_LOG(L"  HOTKEY match (modifier+key): vk=0x%02X", vkCode);
        if (engine_->Count() > 0) {
            CommitComposition();
        }
        ToggleVietnameseMode();
        return true;  // Eat the hotkey
    }

    // 2b. Check convert hotkey (modifier+key variant)
    if (convertHotkeyVk_ != 0 && vkCode == convertHotkeyVk_ && CheckConvertHotkeyMatch()) {
        HOOK_LOG(L"  CONVERT HOTKEY match (modifier+key): vk=0x%02X", vkCode);
        if (engine_->Count() > 0) {
            CommitComposition();
        }
        if (convertCallback_) {
            convertCallback_();
        }
        return true;  // Eat the hotkey
    }

    // Non-modifier key pressed — invalidate modifier-only hotkey combo
    otherKeyPressed_ = true;
    altTapCount_ = 0;  // Break double-Alt tap chain

    // Watchdog: reset synthEventsPending_ if stuck > 500ms.
    // Covers event loss in Electron/Console multi-process apps where synthetic
    // events can be dropped under heavy CPU load, causing cascading re-injection
    // and ghost characters.
    if (synthEventsPending_ > 0) {
        DWORD elapsed = GetTickCount() - lastSynthSendTime_;
        if (elapsed > 500) {
            HOOK_LOG(L"  watchdog: synthEventsPending_ reset from %d (stuck %ums)",
                     synthEventsPending_, elapsed);
            synthEventsPending_ = 0;
        }
    }

    // 2c. Backspace-into-committed-word state machine
    // Supports multi-word backward: stack holds up to kMaxCommitStack committed words.
    // Ready:  set after commit with space/enter, or when engine empties after BS with stack non-empty.
    // Primed: BS in Ready deletes the space; next alpha/BS triggers replay.
    //
    // Auto-expire Ready after kCommitUndoTimeoutMs: cheap insurance against any cursor-movement
    // event that bypasses ResetComposition (e.g. external text change, rare edge cases).
    if (commitUndoState_ == CommitUndoState::Ready) {
        DWORD elapsed = GetTickCount() - commitReadyTime_;
        if (elapsed > kCommitUndoTimeoutMs) {
            HOOK_LOG(L"  commit-undo: Ready state expired after %u ms → Idle", elapsed);
            CancelCommitUndo();
        }
    }
    if (commitUndoState_ == CommitUndoState::Ready && vkCode == VK_BACK && engine_->Count() == 0) {
        // Backspace deletes the commit trigger (space/etc.)
        commitUndoState_ = CommitUndoState::Primed;
        if (synthEventsPending_ > 0) {
            // Synthetic events still in flight (word corrections, injected commit trigger).
            // If we pass BS through now it arrives at the app BEFORE those synthetics,
            // deleting the wrong character and permanently desynchronising previousComposition_.
            // Re-inject so BS is placed AFTER the pending synthetics in the queue.
            HOOK_LOG(L"  commit-undo: BS after commit → Primed, re-inject after synthetics (pending=%d)", synthEventsPending_);
            InjectKey(VK_BACK);
            return true;
        }
        HOOK_LOG(L"  commit-undo: BS after commit → Primed (ready to replay)");
        return false;  // Let backspace pass through to delete the space
    }
    if (commitUndoState_ == CommitUndoState::Primed && engine_->Count() == 0 && vietnameseMode_) {
        if (vkCode >= 0x41 && vkCode <= 0x5A) {
            // Alpha key → replay saved chars, then process the new key.
            // MUST return HandleAlphaKey's value: if it triggers passthrough (return false),
            // the original key must reach the app — ignoring it would swallow the keystroke.
            HOOK_LOG(L"  commit-undo: replaying + alpha '%c' (stack_top='%s' stackSize=%zu prevComp='%s' synthPending=%d)",
                     static_cast<char>(vkCode),
                     commitStack_.empty() ? L"<empty>" : commitStack_.back().text.c_str(),
                     commitStack_.size(),
                     previousComposition_.c_str(),
                     synthEventsPending_);
            ReplayCommittedChars();
            return HandleAlphaKey(vkCode);
        }
        if (vkCode == VK_BACK) {
            // Backspace → replay saved chars, then backspace into the word
            HOOK_LOG(L"  commit-undo: replaying + backspace (stack_top='%s' stackSize=%zu prevComp='%s' synthPending=%d)",
                     commitStack_.empty() ? L"<empty>" : commitStack_.back().text.c_str(),
                     commitStack_.size(),
                     previousComposition_.c_str(),
                     synthEventsPending_);
            ReplayCommittedChars();
            HandleBackspace();
            return true;
        }
        // Any other key → cancel commit-undo
        commitUndoState_ = CommitUndoState::Idle;
    }
    if (commitUndoState_ == CommitUndoState::Ready) {
        // Non-backspace key after commit → cancel undo opportunity
        // (stack preserved — HandleBackspace re-enters state 1 if engine becomes empty)
        commitUndoState_ = CommitUndoState::Idle;
    }

    // 3. English mode fast path — skip Vietnamese-only processing
    if (!vietnameseMode_) {
        if (macroEnabled_ && macroInEnglish_) {
            // Track macro keys (all printable chars) in English mode
            if (vkCode >= 0x41 && vkCode <= 0x5A) {
                bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
                bool capsLock = (GetKeyState(VK_CAPITAL) & 0x0001) != 0;
                bool upper = shift != capsLock;  // XOR: Shift inverts Caps Lock
                rawMacroBuffer_ += upper ? static_cast<wchar_t>(vkCode)
                                         : towlower(static_cast<wchar_t>(vkCode));
            } else if (tempOffMacroByEsc_ && vkCode == VK_ESCAPE && rawMacroBuffer_.empty()) {
                tempMacroOff_ = true;
                return false;
            } else if (IsCommitTrigger(vkCode) && !tempMacroOff_) {
                wchar_t triggerChar = VkToMacroChar(vkCode);
                if (triggerChar > L' ') rawMacroBuffer_ += triggerChar;
                if (!rawMacroBuffer_.empty()) {
                    auto result = TryExpandMacro(triggerChar);
                    if (result == MacroResult::ExpandedEatTrigger) return true;
                    if (result == MacroResult::ExpandedPassTrigger) {
                        if (synthEventsPending_ > 0) { InjectKey(vkCode); return true; }
                        return false;
                    }
                }
            } else if (vkCode == VK_BACK && !rawMacroBuffer_.empty()) {
                rawMacroBuffer_.pop_back();
            } else if (!(vkCode >= 0x41 && vkCode <= 0x5A) && !IsCommitTrigger(vkCode)) {
                rawMacroBuffer_.clear();
                tempMacroOff_ = false;
            }
        }
        HOOK_LOG(L"  skip: Vietnamese mode OFF");
        return false;
    }

    // 3a. Auto-caps state machine (Vietnamese mode only)
    if (autoCaps_) {
        bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
        // '.', '?', '!'
        if (vkCode == VK_OEM_PERIOD || (vkCode == 0xBF && shift) || (vkCode == '1' && shift)) {
            autoCapState_ = 1;
        } else if (vkCode == VK_SPACE && autoCapState_ == 1) {
            autoCapState_ = 2;
        } else if (vkCode == VK_RETURN) {
            autoCapState_ = 2;
        } else if (vkCode >= 0x41 && vkCode <= 0x5A) {
            // Letter key — don't reset, HandleAlphaKey will consume it
        } else {
            autoCapState_ = 0;
        }
    }

    // 3b. Macro: track ALL typed characters (OpenKey approach).
    // Alpha keys AND printable special chars are accumulated so macros with
    // special characters in their key (e.g., "url\" → "URL") can be matched.
    if (macroEnabled_) {
        if (vkCode >= 0x41 && vkCode <= 0x5A) {
            bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
            bool capsLock = (GetKeyState(VK_CAPITAL) & 0x0001) != 0;
            bool upper = shift != capsLock;  // XOR: Shift inverts Caps Lock
            rawMacroBuffer_ += upper ? static_cast<wchar_t>(vkCode)
                                     : towlower(static_cast<wchar_t>(vkCode));
        } else if (IsCommitTrigger(vkCode)) {
            wchar_t ch = VkToMacroChar(vkCode);
            if (ch > L' ') rawMacroBuffer_ += ch;  // Printable non-space chars
        }
    }

    // 3c. Temp off macro by Esc: press Esc with no pending text → skip macro for next word
    if (tempOffMacroByEsc_ && macroEnabled_ && vkCode == VK_ESCAPE
        && engine_->Count() == 0 && rawMacroBuffer_.empty()) {
        tempMacroOff_ = true;
        HOOK_LOG(L"  tempMacroOff: enabled by Esc");
        return false;  // Let Esc pass through
    }

    // 3d. Macro expansion on commit trigger (uses shared TryExpandMacro helper)
    if (macroEnabled_ && !tempMacroOff_ && IsCommitTrigger(vkCode) && !rawMacroBuffer_.empty()) {
        wchar_t triggerChar = VkToMacroChar(vkCode);
        auto result = TryExpandMacro(triggerChar);
        if (result == MacroResult::ExpandedEatTrigger) return true;
        if (result == MacroResult::ExpandedPassTrigger) {
            if (synthEventsPending_ > 0) { InjectKey(vkCode); return true; }
            return false;
        }
    }

    // 4b. Temp-off bypass: Vietnamese mode is ON but temporarily disabled for current word
    if (tempEngineOff_) {
        if (IsCommitTrigger(vkCode)) {
            tempEngineOff_ = false;
            HOOK_LOG(L"  tempEngineOff: reset on commit trigger vk=0x%02X", vkCode);
        } else if (vkCode == VK_BACK && engine_->Count() == 0) {
            tempEngineOff_ = false;
            HOOK_LOG(L"  tempEngineOff: reset on backspace (engine empty)");
        }
        HOOK_LOG(L"  skip: tempEngineOff_ active=%d", tempEngineOff_ ? 1 : 0);
        return false;  // Pass through as English
    }

    // 5. Skip if Ctrl/Alt/Win is down (allow shortcuts to pass through)
    bool ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
    bool alt = (GetKeyState(VK_MENU) & 0x8000) != 0;
    bool win = (GetKeyState(VK_LWIN) & 0x8000) != 0 || (GetKeyState(VK_RWIN) & 0x8000) != 0;

    if (ctrl || alt || win) {
        HOOK_LOG(L"  skip: modifier held (ctrl=%d alt=%d win=%d)", ctrl, alt, win);
        // Always reset — shortcuts like Ctrl+A/C/Z change text state in unpredictable ways.
        // Must also reset when engine is idle (count==0): commitUndoState_ and commitStack_
        // may hold a previously committed word. If not cleared, a Backspace after Ctrl+A
        // triggers ReplayCommittedChars() into the wrong cursor position → garbage output.
        // ResetComposition() guards engine_->Reset() internally on count==0, so this is safe.
        ResetComposition();
        return false;
    }

    // 6. A-Z keys → process with engine
    if (vkCode >= 0x41 && vkCode <= 0x5A) {
        HOOK_LOG(L"  alpha key '%c' → HandleAlphaKey", static_cast<char>(vkCode));
        return HandleAlphaKey(vkCode);
    }

    // 6b. Bracket keys [ ] → engine modifier for Full Telex ([ → ơ, ] → ư)
    if (currentMethod_ == InputMethod::Telex &&
        (vkCode == VK_OEM_4 || vkCode == VK_OEM_6)) {
        bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
        if (!shift) {
            wchar_t ch = (vkCode == VK_OEM_4) ? L'[' : L']';
            inputHistory_.push_back(ch);
            engine_->PushChar(ch);
            std::wstring composition = engine_->Peek();
            HOOK_LOG(L"  bracket '%c' → Peek()='%s'", ch, composition.c_str());
            ReplaceComposition(composition);
            return true;  // Eat the original keystroke
        }
    }

    // 6c. VNI: digit keys 1-9 → tone/modifier input (only with pending composition)
    if (currentMethod_ == InputMethod::VNI &&
        vkCode >= 0x31 && vkCode <= 0x39 &&
        engine_->Count() > 0) {
        bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
        if (!shift) {
            wchar_t ch = static_cast<wchar_t>(vkCode);  // '1'–'9'
            inputHistory_.push_back(ch);
            engine_->PushChar(ch);
            std::wstring composition = engine_->Peek();
            HOOK_LOG(L"  VNI digit '%c' → Peek()='%s'", ch, composition.c_str());
            ReplaceComposition(composition);
            return true;  // Eat the digit
        }
    }

    // 7. Backspace → engine backspace if we have content
    if (vkCode == VK_BACK && engine_->Count() > 0) {
        if (macroEnabled_ && !rawMacroBuffer_.empty()) rawMacroBuffer_.pop_back();
        HOOK_LOG(L"  backspace (engine count=%zu)", engine_->Count());
        HandleBackspace();
        return true;  // Eat backspace
    }

    // 8. Commit triggers: space, enter, tab, punctuation, numbers, escape, arrows
    if (IsCommitTrigger(vkCode) && engine_->Count() > 0) {
        HOOK_LOG(L"  commit trigger vk=0x%02X", vkCode);
        bool restored = CommitComposition();
        // Enable backspace-into-word only for space/enter (natural word boundaries),
        // and ONLY if a new entry was just pushed to the stack (implies: not auto-restored,
        // not quick consonant, not empty history).
        if (pushedToStack_ && (vkCode == VK_SPACE || vkCode == VK_RETURN)) {
            SetCommitUndoReady();
        }
        if (restored || synthEventsPending_ > 0) {
            // Re-inject trigger AFTER all pending synthetic events so that:
            //   (a) auto-restore replacement arrives before the trigger, and
            //   (b) in-flight correction synthetics (e.g. from ee→ê mid-word) arrive
            //       before the trigger — preventing the trigger from slipping ahead of
            //       those backspaces/chars and causing corrupt output ("lỗiêhiênr").
            HOOK_LOG(L"  re-inject trigger vk=0x%02X (restored=%d synthPending=%d)",
                     vkCode, restored ? 1 : 0, synthEventsPending_);
            InjectKey(vkCode);
            return true;  // Eat original trigger
        }
        return false;  // No pending synthetics, safe to pass through
    }

    // 9. Any other key with pending composition → commit and pass through
    if (engine_->Count() > 0) {
        HOOK_LOG(L"  other key vk=0x%02X with pending composition → commit", vkCode);
        bool restored = CommitComposition();
        if (restored || synthEventsPending_ > 0) {
            InjectKey(vkCode);
            return true;
        }
    }

    // 10. BS with engine empty but synthetic events pending: re-inject to preserve ordering.
    // Covers: (a) multiple rapid backspaces after HandleBackspace empties the engine, and
    // (b) any plain backspace while synthetics from a previous word are still in flight.
    // Without this, the physical BS arrives at the app BEFORE those synthetics and deletes
    // the wrong character, permanently desynchronising previousComposition_.
    if (vkCode == VK_BACK && synthEventsPending_ > 0) {
        HOOK_LOG(L"  re-inject BS (engine empty, synthPending=%d)", synthEventsPending_);
        InjectKey(VK_BACK);
        return true;
    }

    return false;
}

/// Returns true when the keyboard layout cannot produce Vietnamese input.
/// Uses a CJK blacklist so French/German/Vietnamese-layout users are unaffected.
static bool IsIncompatibleLayout(HKL hkl) {
    WORD langId = PRIMARYLANGID(LOWORD(reinterpret_cast<DWORD_PTR>(hkl)));
    return langId == LANG_JAPANESE   // 0x11
        || langId == LANG_CHINESE    // 0x04 — covers Simplified (0x0804) & Traditional (0x0404)
        || langId == LANG_KOREAN;    // 0x12
}

bool HookEngine::ProcessKeyUp(DWORD vkCode, DWORD /*flags*/) {
    // TSF app — let TSF DLL handle all input
    if (isTsfApp_) return false;

    bool isModifier = (vkCode == VK_LCONTROL || vkCode == VK_RCONTROL ||
                       vkCode == VK_LSHIFT || vkCode == VK_RSHIFT ||
                       vkCode == VK_LMENU || vkCode == VK_RMENU ||
                       vkCode == VK_LWIN || vkCode == VK_RWIN);

    if (isModifier) {
        // Modifier-only hotkey (e.g., Ctrl+Shift): trigger on release,
        // but only if no other key was pressed and no key is configured
        if (hotkeyVk_ == 0 && !otherKeyPressed_ && CheckHotkeyMatch()) {
            HOOK_LOG(L"  HOTKEY match (modifier-only release): vk=0x%02X", vkCode);
            if (engine_->Count() > 0) {
                CommitComposition();
            }
            ToggleVietnameseMode();
        }

        // Modifier-only convert hotkey
        if (convertHotkeyVk_ == 0 && !otherKeyPressed_ && CheckConvertHotkeyMatch()) {
            HOOK_LOG(L"  CONVERT HOTKEY match (modifier-only release): vk=0x%02X", vkCode);
            if (engine_->Count() > 0) {
                CommitComposition();
            }
            if (convertCallback_) {
                convertCallback_();
            }
        }

        // Temp off spell check: solo Ctrl tap (press + release, no other key)
        if (tempOffSpellByCtrl_ &&
            (vkCode == VK_LCONTROL || vkCode == VK_RCONTROL) &&
            !otherKeyPressed_ && !modShiftDown_ && !modAltDown_ && !modWinDown_) {
            engine_->ToggleTempSpellOff();
            HOOK_LOG(L"  temp off spell check toggled");
        }

        // Double-Alt tap: temporarily disable Vietnamese for current word
        if (tempOffByAlt_ &&
            (vkCode == VK_LMENU || vkCode == VK_RMENU) &&
            !otherKeyPressed_ && !modCtrlDown_ && !modShiftDown_ && !modWinDown_) {
            DWORD now = GetTickCount();
            if (altTapCount_ == 1 && (now - lastAltReleaseTime_) < DOUBLE_ALT_TIMEOUT_MS) {
                if (engine_->Count() > 0) {
                    CommitComposition();
                }
                tempEngineOff_ = !tempEngineOff_;
                // Clear commit-undo state on both enable and disable: modifier-only
                // key sequences (Alt presses) bypass the state machine at line 515-543
                // and bypass otherKeyPressed_, so commitUndoState_ can remain at 1
                // from the last committed word. If not cleared, Backspace after
                // double-Alt → ReplayCommittedChars() at the wrong cursor position.
                CancelCommitUndo();
                altTapCount_ = 0;
                HOOK_LOG(L"  DOUBLE-ALT: tempEngineOff_ = %d", tempEngineOff_ ? 1 : 0);
            } else {
                altTapCount_ = 1;
                lastAltReleaseTime_ = now;
            }
        } else if (vkCode == VK_LMENU || vkCode == VK_RMENU) {
            altTapCount_ = 0;  // Contaminated Alt release
        }

        // Layout auto-disable: re-check on Win+Space / Ctrl+Shift / Alt+Shift key-up.
        // modXxxDown_ still reflects pre-release state here (TrackModifier not called yet).
        {
            bool wasWin   = (vkCode == VK_LWIN    || vkCode == VK_RWIN);
            bool wasShift = (vkCode == VK_LSHIFT   || vkCode == VK_RSHIFT);
            bool wasAlt   = (vkCode == VK_LMENU    || vkCode == VK_RMENU);
            bool wasCtrl  = (vkCode == VK_LCONTROL || vkCode == VK_RCONTROL);
            bool triggerCheck = wasWin
                || (wasShift && modCtrlDown_)   // Ctrl+Shift release
                || (wasShift && modAltDown_)    // Alt+Shift release
                || (wasCtrl  && modShiftDown_)  // Ctrl+Shift release (ctrl side)
                || (wasAlt   && modShiftDown_); // Alt+Shift release (alt side)
            if (triggerCheck) {
                HWND fg = GetForegroundWindow();
                if (fg) {
                    DWORD tid = GetWindowThreadProcessId(fg, nullptr);
                    HKL hkl = GetKeyboardLayout(tid);
                    bool compatible = !IsIncompatibleLayout(hkl);
                    if (compatible != cachedIsCompatLayout_) {
                        cachedIsCompatLayout_ = compatible;
                        OnLayoutChanged(compatible);
                    }
                }
            }
        }

        TrackModifier(vkCode, false);
    }

    return false;  // Never eat key-up
}

// ═══════════════════════════════════════════════════════════
// Input Engine Interaction
// ═══════════════════════════════════════════════════════════

bool HookEngine::HandleAlphaKey(DWORD vkCode) {
    bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
    bool capsLock = (GetKeyState(VK_CAPITAL) & 0x0001) != 0;
    bool upper = shift != capsLock;  // XOR: Shift inverts Caps Lock
    wchar_t originalCh = static_cast<wchar_t>(vkCode);
    if (!upper) originalCh = towlower(originalCh);
    wchar_t ch = originalCh;

    // Auto-capitalize first letter after sentence-ending punctuation
    bool autoCapped = false;
    if (autoCaps_ && autoCapState_ == 2 && engine_->Count() == 0) {
        ch = towupper(ch);
        autoCapped = (ch != originalCh);
        autoCapState_ = 0;
    }

    // Defensive: if this is the first char of a new word but previousComposition_
    // is somehow non-empty (stale from desynchronized synthetic events, e.g. Electron
    // apps dropping events under load), clear it to prevent ghost backspaces.
    if (engine_->Count() == 0 && !previousComposition_.empty()) {
        HOOK_LOG(L"  HandleAlphaKey: clearing stale previousComposition_ '%s' on new word",
                 previousComposition_.c_str());
        previousComposition_.clear();
        previousEncodedWidths_.clear();
    }

    inputHistory_.push_back(ch);
    engine_->PushChar(ch);
    std::wstring composition = engine_->Peek();

    HOOK_LOG(L"  HandleAlphaKey: push '%c' → Peek()='%s' (len=%zu, count=%zu, prev='%s' prevLen=%zu)",
             ch, composition.c_str(), composition.size(), engine_->Count(),
             previousComposition_.c_str(), previousComposition_.size());

    // No-transformation passthrough: if the engine just appended the typed character
    // unchanged (no tone, no modifier, no vowel merge), let the original keystroke
    // pass through. Preserves browser hotkeys (F=fullscreen, M=mute on YouTube, etc.)
    // and reduces SendInput overhead for plain consonant sequences.
    // Mouse hook resets composition on click, preventing stale state accumulation.
    // Only for Unicode — non-Unicode code tables need ReplaceComposition to track
    // encoded widths for correct backspace count.
    // Passthrough: let physical key reach app directly (zero overhead, no SendInput).
    // Blocked when BOTH conditions are true:
    //   - hadSynthInWord_: synth already sent in this word, AND
    //   - isElectronApp_: Electron/Qt multi-process architecture where physical
    //     WM_KEYDOWN and synthetic VK_PACKET can arrive out of order.
    // Win32 native apps have single FIFO message queue — mixing is safe.
    // Console apps are NOT Electron (isElectronApp_=false) so passthrough is safe.
    if (!autoCapped && currentCodeTable_ == CodeTable::Unicode &&
        !(hadSynthInWord_ && isElectronApp_) &&
        composition.size() == previousComposition_.size() + 1 &&
        composition.back() == originalCh &&
        composition.compare(0, previousComposition_.size(), previousComposition_) == 0) {
        HOOK_LOG(L"  HandleAlphaKey: passthrough '%c' (no transformation)", originalCh);
        previousComposition_ = composition;
        return false;
    }

    ReplaceComposition(composition);
    return true;
}

void HookEngine::HandleBackspace() {
    inputHistory_.push_back(kBackspaceMarker);
    engine_->Backspace();

    if (engine_->Count() > 0) {
        std::wstring composition = engine_->Peek();
        ReplaceComposition(composition);
    } else {
        // Engine empty — delete all displayed characters
        if (!previousComposition_.empty()) {
            size_t bsCount = previousComposition_.size();
            if (currentCodeTable_ != CodeTable::Unicode) {
                bsCount = 0;
                for (auto w : previousEncodedWidths_) bsCount += w;
            }
            SendBackspaces(bsCount);
            previousComposition_.clear();
            previousEncodedWidths_.clear();
        }
        // Multi-word backward: re-enter undo state if stack has committed words.
        // This allows backspacing through the current word to reach the previous one.
        if (!commitStack_.empty()) {
            SetCommitUndoReady();
            HOOK_LOG(L"  HandleBackspace: engine empty, stack has %zu entries → state 1",
                     commitStack_.size());
        }
    }
}

bool HookEngine::CommitComposition() {
    HOOK_LOG(L"  CommitComposition (count=%zu, prev='%s')", engine_->Count(), previousComposition_.c_str());

    // Check quick consonant BEFORE Commit() resets the engine.
    // Words ending in active quick consonant (e.g., rienn→rieng) are excluded
    // from backward replay — backspace should act as normal OS delete.
    bool wasQuickConsonant = engine_->HasActiveQuickConsonant();

    std::wstring committed = engine_->Commit();

    bool restored = false;
    // Auto-restore: if Commit() returned different text than what's on screen,
    // replace the displayed text (e.g., "gôgle" → "google")
    if (!previousComposition_.empty() && committed != previousComposition_) {
        HOOK_LOG(L"  AutoRestore: '%s' → '%s'", previousComposition_.c_str(), committed.c_str());
        ReplaceComposition(committed);
        restored = true;
    }

    // Push to commit stack for multi-word backward replay.
    // Skip if: auto-restored (word was English), quick consonant active, or empty history.
    pushedToStack_ = false;
    if (!restored && !wasQuickConsonant && !inputHistory_.empty()) {
        CommitEntry entry;
        entry.history = inputHistory_;
        entry.text = previousComposition_;
        entry.widths = previousEncodedWidths_;
        commitStack_.push_back(std::move(entry));
        // Cap stack size
        if (commitStack_.size() > kMaxCommitStack) {
            commitStack_.erase(commitStack_.begin());
        }
        pushedToStack_ = true;
        HOOK_LOG(L"  CommitComposition: pushed to stack (size=%zu)", commitStack_.size());
    }

    engine_->Reset();
    previousComposition_.clear();
    previousEncodedWidths_.clear();
    inputHistory_.clear();
    rawMacroBuffer_.clear();
    tempMacroOff_ = false;
    hadSynthInWord_ = false;
    return restored;
}

void HookEngine::ResetComposition() {
    HOOK_LOG(L"  ResetComposition (count=%zu, prev='%s')", engine_->Count(), previousComposition_.c_str());
    if (engine_->Count() > 0) {
        engine_->Reset();
    }
    previousComposition_.clear();
    previousEncodedWidths_.clear();
    // Secure-erase keystroke history before releasing the buffer to prevent
    // heap forensics from recovering typed content (including passwords).
    SecureZeroMemory(inputHistory_.data(), inputHistory_.size() * sizeof(wchar_t));
    inputHistory_.clear();
    SecureZeroMemory(rawMacroBuffer_.data(), rawMacroBuffer_.size() * sizeof(wchar_t));
    rawMacroBuffer_.clear();
    tempMacroOff_ = false;
    CancelCommitUndo();
    synthEventsPending_ = 0;  // Pending synthetics from old context are irrelevant after reset
    hadSynthInWord_ = false;
}

void HookEngine::CancelCommitUndo() {
    commitUndoState_ = CommitUndoState::Idle;
    commitStack_.clear();
}

void HookEngine::SetCommitUndoReady() {
    commitUndoState_ = CommitUndoState::Ready;
    commitReadyTime_ = GetTickCount();
}

// ═══════════════════════════════════════════════════════════
// Backspace-into-committed-word: replay saved chars from stack
// ═══════════════════════════════════════════════════════════

void HookEngine::ReplayCommittedChars() {
    if (commitStack_.empty()) {
        HOOK_LOG(L"  ReplayCommittedChars: stack empty, nothing to replay");
        commitUndoState_ = CommitUndoState::Idle;
        return;
    }

    // Pop the most recently committed word from the stack
    CommitEntry entry = std::move(commitStack_.back());
    commitStack_.pop_back();

    HOOK_LOG(L"  ReplayCommittedChars: replaying %zu keystrokes, restoring prev='%s' (stack=%zu remaining)",
             entry.history.size(), entry.text.c_str(), commitStack_.size());

    // Replay exact user keystrokes (including backspaces) to reproduce engine state
    for (wchar_t ch : entry.history) {
        if (ch == kBackspaceMarker) {
            engine_->Backspace();
        } else {
            engine_->PushChar(ch);
        }
    }
    // Seed inputHistory_ with the replayed word's keystrokes so that if the user
    // edits and re-commits this word, the new stack entry contains the full history
    // (not just the editing delta). Otherwise a second replay attempt would be wrong.
    inputHistory_ = std::move(entry.history);

    // Restore screen state so ReplaceComposition can diff correctly
    previousComposition_ = std::move(entry.text);
    previousEncodedWidths_ = std::move(entry.widths);

    // Reset undo state — HandleBackspace will re-enter state 1 if engine becomes
    // empty again and stack still has entries (enabling multi-word backward).
    commitUndoState_ = CommitUndoState::Idle;
}

// ═══════════════════════════════════════════════════════════
// Output — Universal SendInput with KEYEVENTF_UNICODE
// ═══════════════════════════════════════════════════════════

/// Get the focused child window that actually receives input
static HWND GetInputTarget() {
    HWND fg = GetForegroundWindow();
    if (!fg) return nullptr;
    DWORD tid = GetWindowThreadProcessId(fg, nullptr);
    GUITHREADINFO gti = { sizeof(gti) };
    if (GetGUIThreadInfo(tid, &gti) && gti.hwndFocus) {
        return gti.hwndFocus;
    }
    return fg;
}

// ═══════════════════════════════════════════════════════════
// SendInput Event Helpers
// ═══════════════════════════════════════════════════════════

static void AppendUnicodeEvent(std::vector<INPUT>& events, WORD wScan) {
    INPUT inDown = {};
    inDown.type = INPUT_KEYBOARD;
    inDown.ki.wScan = wScan;
    inDown.ki.dwFlags = KEYEVENTF_UNICODE;
    inDown.ki.dwExtraInfo = HookEngine::NEXUSKEY_EXTRA_INFO;
    events.push_back(inDown);

    INPUT inUp = inDown;
    inUp.ki.dwFlags |= KEYEVENTF_KEYUP;
    events.push_back(inUp);
}

static void AppendVkEvent(std::vector<INPUT>& events, WORD wVk, WORD wScan) {
    INPUT inDown = {};
    inDown.type = INPUT_KEYBOARD;
    inDown.ki.wVk = wVk;
    inDown.ki.wScan = wScan;
    inDown.ki.dwExtraInfo = HookEngine::NEXUSKEY_EXTRA_INFO;
    events.push_back(inDown);

    INPUT inUp = inDown;
    inUp.ki.dwFlags |= KEYEVENTF_KEYUP;
    events.push_back(inUp);
}

void HookEngine::SendBackspaceEvents(size_t count) {
    WORD bsScan = static_cast<WORD>(MapVirtualKeyW(VK_BACK, MAPVK_VK_TO_VSC));
    std::vector<INPUT> events;
    events.reserve(count * 2);
    for (size_t i = 0; i < count; ++i) {
        AppendVkEvent(events, VK_BACK, bsScan);
    }
    sending_ = true;
    UINT sent = SendInput(static_cast<UINT>(events.size()), events.data(), sizeof(INPUT));
    synthEventsPending_ += static_cast<int>(sent);
    sending_ = false;
    lastSynthSendTime_ = GetTickCount();
}

void HookEngine::SendCharEvents(const std::wstring& text) {
    std::vector<INPUT> events;
    events.reserve(text.size() * 2);
    for (wchar_t ch : text) {
        AppendUnicodeEvent(events, ch);
    }
    sending_ = true;
    UINT sent = SendInput(static_cast<UINT>(events.size()), events.data(), sizeof(INPUT));
    synthEventsPending_ += static_cast<int>(sent);
    sending_ = false;
    lastSynthSendTime_ = GetTickCount();
}

/// Dispatch backspace + character events via SendInput.
/// Handles split (Electron/Console) vs batch (Win32) strategy in one place.
/// NOTE: batch path appends charEvents into bsEvents — callers must not reuse after calling.
void HookEngine::DispatchSendInput(std::vector<INPUT>& bsEvents, std::vector<INPUT>& charEvents) {
    sending_ = true;
    if (skipEmptyChar_) {
        // Split: VK_BACK and VK_PACKET travel on separate internal paths in
        // Electron/Console apps — batching risks out-of-order processing ("nuốt chữ").
        if (!bsEvents.empty()) {
            UINT sent = SendInput(static_cast<UINT>(bsEvents.size()), bsEvents.data(), sizeof(INPUT));
            synthEventsPending_ += static_cast<int>(sent);
            // Gap so app finishes processing BS before receiving chars.
            // timeBeginPeriod(1) in main.cpp makes Sleep(N) actually ~N ms.
            // Base: 10ms Electron (multi-process IPC), 8ms Console (Node.js apps like Claude CLI).
            // +1ms per extra BS pair: more deletions = more processing time.
            // Cap at 20ms — imperceptible to user but enough for slowest apps.
            int baseMs = isElectronApp_ ? 10 : 8;
            int bsCount = static_cast<int>(bsEvents.size()) / 2;  // each BS = down+up pair
            int delayMs = (std::min)(baseMs + (bsCount > 1 ? bsCount - 1 : 0), 20);
            Sleep(delayMs);
        }
        if (!charEvents.empty()) {
            UINT sent = SendInput(static_cast<UINT>(charEvents.size()), charEvents.data(), sizeof(INPUT));
            synthEventsPending_ += static_cast<int>(sent);
        }
    } else {
        // Batch: standard Win32 GUI apps have single message queue (FIFO).
        bsEvents.insert(bsEvents.end(), charEvents.begin(), charEvents.end());
        if (!bsEvents.empty()) {
            UINT sent = SendInput(static_cast<UINT>(bsEvents.size()), bsEvents.data(), sizeof(INPUT));
            synthEventsPending_ += static_cast<int>(sent);
        }
    }
    sending_ = false;
    lastSynthSendTime_ = GetTickCount();
}

/// Check if a filename (without path) is a known browser executable.
static bool IsBrowserExeName(const wchar_t* filename) {
    return _wcsnicmp(filename, L"chrome", 6) == 0 ||
           _wcsnicmp(filename, L"msedge", 6) == 0 ||
           _wcsnicmp(filename, L"firefox", 7) == 0 ||
           _wcsnicmp(filename, L"brave", 5) == 0 ||
           _wcsnicmp(filename, L"opera", 5) == 0 ||
           _wcsnicmp(filename, L"vivaldi", 7) == 0;
}

bool HookEngine::IsQtElectronApp(HWND hwnd) {
    HWND root = GetAncestor(hwnd, GA_ROOT);
    if (root) hwnd = root;

    wchar_t className[64] = {};
    GetClassNameW(hwnd, className, 64);

    // Qt apps: Qt5QWindowIcon, Qt6QWindowIcon, QWidget, etc.
    if (wcsstr(className, L"Qt5") || wcsstr(className, L"Qt6") ||
        wcsstr(className, L"QWidget")) {
        return true;
    }

    // Electron apps use Chrome_WidgetWin but are NOT actual browsers
    if (wcsstr(className, L"Chrome_WidgetWin")) {
        // Exclude real browsers — they NEED U+202F for autocomplete fix
        DWORD pid = 0;
        GetWindowThreadProcessId(hwnd, &pid);
        if (!pid) return false;

        HANDLE hProc = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
        if (!hProc) return false;

        wchar_t exePath[MAX_PATH] = {};
        DWORD size = MAX_PATH;
        bool isElectron = true;  // Assume Electron unless proven browser
        if (QueryFullProcessImageNameW(hProc, 0, exePath, &size)) {
            const wchar_t* filename = wcsrchr(exePath, L'\\');
            filename = filename ? filename + 1 : exePath;
            if (IsBrowserExeName(filename)) {
                isElectron = false;
            }
        }
        CloseHandle(hProc);
        return isElectron;
    }

    return false;
}

bool HookEngine::IsConsoleApp(HWND hwnd) {
    HWND root = GetAncestor(hwnd, GA_ROOT);
    if (root) hwnd = root;

    wchar_t className[64] = {};
    GetClassNameW(hwnd, className, 64);

    if (_wcsicmp(className, L"ConsoleWindowClass") == 0) return true;
    if (_wcsicmp(className, L"CASCADIA_HOSTING_WINDOW_CLASS") == 0) return true; // Windows Terminal
    if (_wcsicmp(className, L"tty") == 0) return true; // Cygwin/MSYS
    if (_wcsicmp(className, L"mintty") == 0) return true; // Git Bash
    if (_wcsicmp(className, L"PuTTY") == 0) return true; // PuTTY

    // Electron apps (e.g. VS Code terminal) are handled by IsQtElectronApp.
    return false;
}

std::wstring HookEngine::GetForegroundExeName() {
    HWND fg = GetForegroundWindow();
    if (!fg) return {};

    DWORD pid = 0;
    GetWindowThreadProcessId(fg, &pid);
    if (!pid) return {};

    HANDLE hProc = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!hProc) return {};

    wchar_t exePath[MAX_PATH] = {};
    DWORD size = MAX_PATH;
    std::wstring result;
    if (QueryFullProcessImageNameW(hProc, 0, exePath, &size)) {
        const wchar_t* filename = wcsrchr(exePath, L'\\');
        result = ToLowerAscii(filename ? filename + 1 : exePath);
    }
    CloseHandle(hProc);
    return result;
}

void HookEngine::ReloadAppOverrides() {
    auto overrides = ConfigManager::LoadAppOverrides(ConfigManager::GetConfigPath());
    appEncodingOverrides_.clear();
    appInputMethodOverrides_.clear();
    for (auto& [exe, entry] : overrides) {
        if (entry.encodingOverride >= 0)
            appEncodingOverrides_[exe] = entry.encodingOverride;
        if (entry.inputMethod >= 0)
            appInputMethodOverrides_[exe] = entry.inputMethod;
    }
}

void HookEngine::OnLayoutChanged(bool isCompatibleNow) {
    if (!isCompatibleNow && !layoutForcedEnglish_) {
        // Compatible → incompatible (CJK): save mode, force English
        if (engine_->Count() > 0) CommitComposition();
        CancelCommitUndo();
        preLayoutSwitchMode_ = vietnameseMode_;
        layoutForcedEnglish_ = true;
        vietnameseMode_ = false;
        HOOK_LOG(L"  LayoutAutoDisable: CJK layout detected, forcing English (saved mode=%d)",
                 preLayoutSwitchMode_ ? 1 : 0);
        if (modeChangeCallback_) modeChangeCallback_(false);
    } else if (isCompatibleNow && layoutForcedEnglish_) {
        // Incompatible → compatible: restore saved mode if smart switch is on
        layoutForcedEnglish_ = false;
        if (smartSwitch_) {
            vietnameseMode_ = preLayoutSwitchMode_;
            HOOK_LOG(L"  LayoutAutoDisable: compatible layout restored, mode=%d",
                     vietnameseMode_ ? 1 : 0);
            if (modeChangeCallback_) modeChangeCallback_(vietnameseMode_);
        }
        // smartSwitch OFF: stay in English, user must manually toggle back
    }
}

void HookEngine::OnFocusChanged() {
    ResetComposition();
    tempEngineOff_ = false;
    CheckConfigEvent();

    // Detect Qt/Electron apps and Console apps — skip U+202F to avoid delay/corruption
    HWND fg = GetForegroundWindow();
    isConsoleApp_ = fg && IsConsoleApp(fg);
    skipEmptyChar_ = fg && (IsQtElectronApp(fg) || isConsoleApp_);
    isElectronApp_ = skipEmptyChar_ && !isConsoleApp_;
    HOOK_LOG(L"  AppDetect: console=%d skipEmpty=%d electron=%d",
             isConsoleApp_ ? 1 : 0, skipEmptyChar_ ? 1 : 0, isElectronApp_ ? 1 : 0);

    // Layout auto-disable: check CJK layout on every focus change
    if (fg) {
        DWORD tid = GetWindowThreadProcessId(fg, nullptr);
        HKL hkl = GetKeyboardLayout(tid);
        bool compatible = !IsIncompatibleLayout(hkl);
        if (compatible != cachedIsCompatLayout_) {
            cachedIsCompatLayout_ = compatible;
            OnLayoutChanged(compatible);
        }
    }

    // Skip focus tracking entirely if no feature needs it
    if (!smartSwitch_ && !excludeApps_ && !tsfApps_
        && appEncodingOverrides_.empty() && appInputMethodOverrides_.empty()) return;

    bool wasExcluded = isExcludedApp_;
    bool wasTsfApp = isTsfApp_;

    // Save mode for previous app (smart switch, skip excluded/TSF apps)
    if (smartSwitch_ && !layoutForcedEnglish_ && !currentExe_.empty() && !wasExcluded && !wasTsfApp) {
        appModeMap_[currentExe_] = vietnameseMode_;
        smartSwitchMgr_.SetAppMode(currentExe_, vietnameseMode_);
    }

    // Get new app (save previous for tray menu context)
    if (!currentExe_.empty()) {
        previousExe_ = currentExe_;
    }
    currentExe_ = GetForegroundExeName();
    if (currentExe_.empty()) return;

    // Check excluded apps
    if (excludeApps_ && !excludedAppSet_.empty()) {
        isExcludedApp_ = excludedAppSet_.count(currentExe_) > 0;
    } else {
        isExcludedApp_ = false;
    }

    // Check TSF apps (hook passthrough — let TSF DLL handle input)
    // Excluded apps take priority — if both, treat as excluded (force English)
    if (!isExcludedApp_ && tsfApps_ && !tsfAppSet_.empty()) {
        isTsfApp_ = tsfAppSet_.count(currentExe_) > 0;
    } else {
        isTsfApp_ = false;
    }

    // Notify SharedState when TSF active state changes (DLL reads this flag)
    if (isTsfApp_ != wasTsfApp && tsfActiveCallback_) {
        HOOK_LOG(L"  TSF_ACTIVE flag: %s → %s", wasTsfApp ? L"true" : L"false", isTsfApp_ ? L"true" : L"false");
        tsfActiveCallback_(isTsfApp_);
    }

    if (isExcludedApp_) {
        // Entering excluded app — save mode before forcing English
        if (!wasExcluded) {
            modeBeforeExclude_ = vietnameseMode_;
        }
        HOOK_LOG(L"  ExcludeApps: '%s' is excluded, forcing English", currentExe_.c_str());
        if (vietnameseMode_) {
            vietnameseMode_ = false;
            if (modeChangeCallback_) {
                modeChangeCallback_(false);
            }
        }
        return;  // Skip smart switch restore and code table restore for excluded apps
    }

    // TSF app — hook is passive, skip smart switch/code table restore
    if (isTsfApp_) {
        HOOK_LOG(L"  TsfApps: '%s' uses TSF engine, hook passthrough", currentExe_.c_str());
        return;
    }

    // Manual encoding override per app (restore to global if no override)
    {
        auto it = appEncodingOverrides_.find(currentExe_);
        CodeTable targetTable = (it != appEncodingOverrides_.end() && it->second >= 0)
            ? static_cast<CodeTable>(it->second) : globalCodeTable_;
        if (targetTable != currentCodeTable_) {
            currentCodeTable_ = targetTable;
            HOOK_LOG(L"  AppOverride: encoding=%d for '%s'",
                     static_cast<int>(currentCodeTable_), currentExe_.c_str());
        }
    }

    // Manual input method override per app (restore to global if no override)
    {
        auto it = appInputMethodOverrides_.find(currentExe_);
        InputMethod targetMethod = (it != appInputMethodOverrides_.end() && it->second >= 0)
            ? static_cast<InputMethod>(it->second) : globalInputMethod_;
        if (targetMethod != currentMethod_) {
            currentMethod_ = targetMethod;
            TypingConfig engineConfig = config_;
            engineConfig.inputMethod = targetMethod;
            engine_ = EngineFactory::Create(engineConfig);
            HOOK_LOG(L"  AppOverride: inputMethod=%d for '%s'",
                     static_cast<int>(currentMethod_), currentExe_.c_str());
        }
    }

    // Smart switch: restore mode for new app
    if (smartSwitch_) {
        auto it = appModeMap_.find(currentExe_);
        if (it != appModeMap_.end()) {
            // Known app — restore its saved mode
            if (it->second != vietnameseMode_) {
                vietnameseMode_ = it->second;
                HOOK_LOG(L"  SmartSwitch: restored %s for '%s'",
                         vietnameseMode_ ? L"Vietnamese" : L"English", currentExe_.c_str());
                if (modeChangeCallback_) {
                    modeChangeCallback_(vietnameseMode_);
                }
            }
        } else if (wasExcluded && modeBeforeExclude_ != vietnameseMode_) {
            // Leaving excluded app to unknown app — restore pre-exclusion mode
            vietnameseMode_ = modeBeforeExclude_;
            HOOK_LOG(L"  SmartSwitch: restored pre-exclude %s for '%s'",
                     vietnameseMode_ ? L"Vietnamese" : L"English", currentExe_.c_str());
            if (modeChangeCallback_) {
                modeChangeCallback_(vietnameseMode_);
            }
        }
    } else if (wasExcluded && modeBeforeExclude_ != vietnameseMode_) {
        // No smart switch, but still restore pre-exclusion mode when leaving excluded app
        vietnameseMode_ = modeBeforeExclude_;
        HOOK_LOG(L"  ExcludeApps: restored pre-exclude %s for '%s'",
                 vietnameseMode_ ? L"Vietnamese" : L"English", currentExe_.c_str());
        if (modeChangeCallback_) {
            modeChangeCallback_(vietnameseMode_);
        }
    }
}

void HookEngine::ReplaceComposition(const std::wstring& newText) {
    HWND target = GetInputTarget();
    if (!target) {
        previousComposition_ = newText;
        return;
    }

    // Find common prefix at Unicode level — only replace what actually changed
    size_t commonLen = 0;
    size_t minLen = (std::min)(previousComposition_.size(), newText.size());
    while (commonLen < minLen && previousComposition_[commonLen] == newText[commonLen]) {
        commonLen++;
    }

    // ── Non-Unicode code table path ──
    if (currentCodeTable_ != CodeTable::Unicode) {
        // Calculate backspace count from encoded widths of chars being replaced
        size_t backspaceCount = 0;
        for (size_t i = commonLen; i < previousEncodedWidths_.size(); ++i) {
            backspaceCount += previousEncodedWidths_[i];
        }

        // Convert new chars to encoded form
        std::wstring encodedToSend;
        std::vector<uint8_t> newWidths;
        for (size_t i = commonLen; i < newText.size(); ++i) {
            auto enc = CodeTableConverter::ConvertChar(newText[i], currentCodeTable_);
            encodedToSend += enc.units[0];
            if (enc.count == 2) encodedToSend += enc.units[1];
            newWidths.push_back(enc.count);
        }

        HOOK_LOG(L"  ReplaceComposition[encoded]: prev='%s' new='%s' common=%zu BS=%zu encodedLen=%zu",
                 previousComposition_.c_str(), newText.c_str(), commonLen, backspaceCount,
                 encodedToSend.size());

        {
            bool needEmpty = (backspaceCount > 0 && !skipEmptyChar_);
            size_t bsTotal = backspaceCount + (needEmpty ? 1 : 0);

            if (bsTotal > 0 || !encodedToSend.empty()) {
                std::vector<INPUT> bsEvents;
                std::vector<INPUT> charEvents;
                WORD bsScan = static_cast<WORD>(MapVirtualKeyW(VK_BACK, MAPVK_VK_TO_VSC));

                if (bsTotal > 0) {
                    bsEvents.reserve(bsTotal * 2 + (needEmpty ? 2 : 0));
                    if (needEmpty) {
                        AppendUnicodeEvent(bsEvents, 0x202F);
                    }
                    for (size_t i = 0; i < bsTotal; ++i) {
                        AppendVkEvent(bsEvents, VK_BACK, bsScan);
                    }
                }

                if (!encodedToSend.empty()) {
                    charEvents.reserve(encodedToSend.size() * 2);
                    for (wchar_t ch : encodedToSend) {
                        AppendUnicodeEvent(charEvents, ch);
                    }
                }

                DispatchSendInput(bsEvents, charEvents);
            }
        }

        // Update widths: keep [0..commonLen), append newWidths
        previousEncodedWidths_.resize(commonLen);
        previousEncodedWidths_.insert(previousEncodedWidths_.end(), newWidths.begin(), newWidths.end());
        previousComposition_ = newText;
        return;
    }

    // ── Unicode path (fast path, zero overhead) ──
    size_t backspaceCount = previousComposition_.size() - commonLen;
    std::wstring toSend = newText.substr(commonLen);

    HOOK_LOG(L"  ReplaceComposition: prev='%s' new='%s' common=%zu BS=%zu send='%s'",
             previousComposition_.c_str(), newText.c_str(), commonLen, backspaceCount,
             toSend.c_str());

    {
        bool needEmpty = (backspaceCount > 0 && !skipEmptyChar_);
        if (needEmpty) backspaceCount++;

        HOOK_LOG(L"  ReplaceComposition[send]: BS=%zu needEmpty=%d toSend='%s' skipEmpty=%d synthPending=%d",
                 backspaceCount, needEmpty ? 1 : 0, toSend.c_str(), skipEmptyChar_ ? 1 : 0, synthEventsPending_);

        if (backspaceCount > 0 || !toSend.empty()) {
            std::vector<INPUT> bsEvents;
            std::vector<INPUT> charEvents;
            WORD bsScan = static_cast<WORD>(MapVirtualKeyW(VK_BACK, MAPVK_VK_TO_VSC));

            if (backspaceCount > 0) {
                bsEvents.reserve((needEmpty ? 2 : 0) + backspaceCount * 2);
                if (needEmpty) {
                    AppendUnicodeEvent(bsEvents, 0x202F);
                }
                for (size_t i = 0; i < backspaceCount; ++i) {
                    AppendVkEvent(bsEvents, VK_BACK, bsScan);
                }
            }

            if (!toSend.empty()) {
                charEvents.reserve(toSend.size() * 2);
                for (wchar_t ch : toSend) {
                    AppendUnicodeEvent(charEvents, ch);
                }
            }

            DispatchSendInput(bsEvents, charEvents);
        }
    }

    previousComposition_ = newText;
    // Mark that at least one synthetic event was sent for this word.
    // Guards passthrough path in HandleAlphaKey from mixing physical+synthetic events mid-word.
    if (synthEventsPending_ > 0) hadSynthInWord_ = true;
}

void HookEngine::SendBackspaces(size_t count) {
    HOOK_LOG(L"  SendBackspaces: %zu", count);
    SendBackspaceEvents(count);
}

// ═══════════════════════════════════════════════════════════
// Hotkey Detection (absorbed from HotkeyManager)
// ═══════════════════════════════════════════════════════════

void HookEngine::TrackModifier(DWORD vkCode, bool isDown) {
    switch (vkCode) {
        case VK_LCONTROL: case VK_RCONTROL:
            if (isDown && !modCtrlDown_) { modCtrlDown_ = true; otherKeyPressed_ = false; }
            else if (!isDown) modCtrlDown_ = false;
            break;
        case VK_LSHIFT: case VK_RSHIFT:
            if (isDown && !modShiftDown_) { modShiftDown_ = true; otherKeyPressed_ = false; }
            else if (!isDown) modShiftDown_ = false;
            break;
        case VK_LMENU: case VK_RMENU:
            if (isDown && !modAltDown_) { modAltDown_ = true; otherKeyPressed_ = false; }
            else if (!isDown) modAltDown_ = false;
            break;
        case VK_LWIN: case VK_RWIN:
            if (isDown && !modWinDown_) { modWinDown_ = true; otherKeyPressed_ = false; }
            else if (!isDown) modWinDown_ = false;
            break;
    }
}

bool HookEngine::CheckHotkeyMatch() const {
    // No hotkey configured
    if (!hotkeyConfig_.ctrl && !hotkeyConfig_.shift &&
        !hotkeyConfig_.alt && !hotkeyConfig_.win && hotkeyConfig_.key == 0) {
        return false;
    }

    // Strict XOR matching: modifiers must match EXACTLY (like OpenKey)
    bool ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
    bool alt = (GetKeyState(VK_MENU) & 0x8000) != 0;
    bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
    bool win = (GetKeyState(VK_LWIN) & 0x8000) != 0 || (GetKeyState(VK_RWIN) & 0x8000) != 0;

    if (hotkeyConfig_.ctrl != ctrl) return false;
    if (hotkeyConfig_.alt != alt) return false;
    if (hotkeyConfig_.shift != shift) return false;
    if (hotkeyConfig_.win != win) return false;

    return true;
}

bool HookEngine::CheckConvertHotkeyMatch() const {
    // No convert hotkey configured
    if (!convertHotkeyConfig_.ctrl && !convertHotkeyConfig_.shift &&
        !convertHotkeyConfig_.alt && !convertHotkeyConfig_.win && convertHotkeyConfig_.key == 0) {
        return false;
    }

    bool ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
    bool alt = (GetKeyState(VK_MENU) & 0x8000) != 0;
    bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
    bool win = (GetKeyState(VK_LWIN) & 0x8000) != 0 || (GetKeyState(VK_RWIN) & 0x8000) != 0;

    if (convertHotkeyConfig_.ctrl != ctrl) return false;
    if (convertHotkeyConfig_.alt != alt) return false;
    if (convertHotkeyConfig_.shift != shift) return false;
    if (convertHotkeyConfig_.win != win) return false;

    return true;
}

void HookEngine::SetConvertHotkey(const HotkeyConfig& hotkey) {
    convertHotkeyConfig_ = hotkey;
    convertHotkeyVk_ = 0;
    if (convertHotkeyConfig_.key != 0) {
        SHORT vkResult = VkKeyScanW(convertHotkeyConfig_.key);
        if (vkResult != -1) {
            convertHotkeyVk_ = LOBYTE(vkResult);
        }
    }
}

// ═══════════════════════════════════════════════════════════
// Commit Trigger Check
// ═══════════════════════════════════════════════════════════

void HookEngine::InjectKey(DWORD vkCode) {
    WORD scan = static_cast<WORD>(MapVirtualKeyW(vkCode, MAPVK_VK_TO_VSC));
    INPUT inputs[2] = {};
    inputs[0].type = INPUT_KEYBOARD;
    inputs[0].ki.wVk = static_cast<WORD>(vkCode);
    inputs[0].ki.wScan = scan;
    inputs[0].ki.dwExtraInfo = NEXUSKEY_EXTRA_INFO;
    inputs[1].type = INPUT_KEYBOARD;
    inputs[1].ki.wVk = static_cast<WORD>(vkCode);
    inputs[1].ki.wScan = scan;
    inputs[1].ki.dwFlags = KEYEVENTF_KEYUP;
    inputs[1].ki.dwExtraInfo = NEXUSKEY_EXTRA_INFO;
    sending_ = true;
    UINT sent = SendInput(2, inputs, sizeof(INPUT));
    synthEventsPending_ += static_cast<int>(sent);
    sending_ = false;
    lastSynthSendTime_ = GetTickCount();
}

bool HookEngine::IsCommitTrigger(DWORD vkCode) {
    // Space, Enter, Escape
    if (vkCode == VK_SPACE || vkCode == VK_RETURN || vkCode == VK_ESCAPE) return true;

    // Tab
    if (vkCode == VK_TAB) return true;

    // Arrow keys
    if (vkCode >= VK_LEFT && vkCode <= VK_DOWN) return true;
    if (vkCode == VK_HOME || vkCode == VK_END ||
        vkCode == VK_PRIOR || vkCode == VK_NEXT) return true;

    // Number keys (0-9)
    if (vkCode >= 0x30 && vkCode <= 0x39) return true;

    // Numpad keys
    if (vkCode >= VK_NUMPAD0 && vkCode <= VK_DIVIDE) return true;

    // OEM keys (punctuation)
    if (vkCode >= VK_OEM_1 && vkCode <= VK_OEM_3) return true;
    if (vkCode >= VK_OEM_4 && vkCode <= VK_OEM_8) return true;
    if (vkCode == VK_OEM_PLUS || vkCode == VK_OEM_COMMA ||
        vkCode == VK_OEM_MINUS || vkCode == VK_OEM_PERIOD) return true;

    // Delete, Insert
    if (vkCode == VK_DELETE || vkCode == VK_INSERT) return true;

    return false;
}

HookEngine::MacroResult HookEngine::TryExpandMacro(wchar_t triggerChar) {
    std::wstring lowerKey = ToLowerAscii(rawMacroBuffer_);
    bool isPartOfMacro = false;

    // Priority 1: full buffer (includes accumulated trigger char)
    auto it = macroTable_.find(lowerKey);
    if (it != macroTable_.end() && triggerChar > L' ') {
        isPartOfMacro = true;
    }
    // Priority 2: buffer without trigger char (e.g., "btw" from "btw.")
    if (it == macroTable_.end() && triggerChar > L' ' &&
        lowerKey.size() > 1 && lowerKey.back() == triggerChar) {
        it = macroTable_.find(lowerKey.substr(0, lowerKey.size() - 1));
    }
    // Priority 3: composed Vietnamese output + trigger (handles tone escape: "urrl\" → "url\")
    if (it == macroTable_.end() && !previousComposition_.empty()) {
        std::wstring compKey = ToLowerAscii(previousComposition_);
        if (triggerChar > L' ') {
            it = macroTable_.find(compKey + triggerChar);
            if (it != macroTable_.end()) isPartOfMacro = true;
        }
        // Priority 4: composed Vietnamese output alone
        if (it == macroTable_.end()) {
            it = macroTable_.find(compKey);
        }
    }
    if (it == macroTable_.end()) return MacroResult::NoMatch;

    // Backspace count = screen content (trigger char is NOT on screen yet)
    size_t bsCount;
    if (!previousComposition_.empty()) {
        if (currentCodeTable_ != CodeTable::Unicode) {
            bsCount = 0;
            for (auto w : previousEncodedWidths_) bsCount += w;
        } else {
            bsCount = previousComposition_.size();
        }
    } else {
        bsCount = rawMacroBuffer_.size();
        if (triggerChar > L' ' && bsCount > 0) --bsCount;
    }
    // Auto-capitalize expansion to match typed case pattern
    std::wstring expansion = it->second;
    if (autoCapsMacro_ && !rawMacroBuffer_.empty()) {
        bool allUpper = true, firstUpper = iswupper(rawMacroBuffer_[0]);
        for (auto c : rawMacroBuffer_) {
            if (!iswupper(c)) { allUpper = false; break; }
        }
        if (allUpper && rawMacroBuffer_.size() > 1) {
            for (auto& c : expansion) c = towupper(c);
        } else if (firstUpper && !expansion.empty()) {
            expansion[0] = towupper(expansion[0]);
        }
    }

    {
        WORD bsScan = static_cast<WORD>(MapVirtualKeyW(VK_BACK, MAPVK_VK_TO_VSC));
        std::vector<INPUT> bsEvents;
        std::vector<INPUT> charEvents;

        bsEvents.reserve(bsCount * 2);
        for (size_t i = 0; i < bsCount; ++i) {
            AppendVkEvent(bsEvents, VK_BACK, bsScan);
        }
        if (currentCodeTable_ != CodeTable::Unicode) {
            for (wchar_t ch : expansion) {
                auto enc = CodeTableConverter::ConvertChar(ch, currentCodeTable_);
                AppendUnicodeEvent(charEvents, enc.units[0]);
                if (enc.count == 2) AppendUnicodeEvent(charEvents, enc.units[1]);
            }
        } else {
            charEvents.reserve(expansion.size() * 2);
            for (wchar_t ch : expansion) {
                AppendUnicodeEvent(charEvents, ch);
            }
        }
        DispatchSendInput(bsEvents, charEvents);
    }
    // Full state cleanup — must match CommitComposition's cleanup to prevent
    // stale inputHistory_/commitUndoState_ from leaking into the next word.
    engine_->Reset();
    previousComposition_.clear();
    previousEncodedWidths_.clear();
    rawMacroBuffer_.clear();
    inputHistory_.clear();
    CancelCommitUndo();
    hadSynthInWord_ = false;
    return isPartOfMacro ? MacroResult::ExpandedEatTrigger : MacroResult::ExpandedPassTrigger;
}

wchar_t HookEngine::VkToMacroChar(DWORD vkCode) noexcept {
    // Use MapVirtualKeyW to get the actual character for this VK code,
    // respecting the current keyboard layout (not hardcoded to US QWERTY).
    UINT ch = MapVirtualKeyW(vkCode, MAPVK_VK_TO_CHAR);
    return ch ? static_cast<wchar_t>(towlower(static_cast<wchar_t>(ch))) : 0;
}

}  // namespace NextKey
