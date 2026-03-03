// NexusKey - Keyboard Hook Engine Implementation
// SPDX-License-Identifier: GPL-3.0-only

#include "HookEngine.h"
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
    g_hookLog = _wfopen(logPath.c_str(), L"w");
    if (g_hookLog) setvbuf(g_hookLog, nullptr, _IOFBF, 8192);  // 8KB buffer
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

HookEngine* HookEngine::s_instance = nullptr;

HookEngine::HookEngine() = default;

HookEngine::~HookEngine() {
    Stop();
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
    beepOnSwitch_ = config.beepOnSwitch;
    smartSwitch_ = config.smartSwitch;
    excludeApps_ = config.excludeApps;
    autoCaps_ = config.autoCaps;
    tempOffSpellByCtrl_ = config.tempOffSpellByCtrl;
    tempOffByAlt_ = config.tempOffByAlt;
    macroEnabled_ = config.macroEnabled;
    macroInEnglish_ = config.macroInEnglish;
    tempOffMacroByEsc_ = config.tempOffMacroByEsc;
    if (macroEnabled_) {
        macroTable_ = ConfigManager::LoadMacros(ConfigManager::GetConfigPath());
    }
    autoCapState_ = 0;
    engine_ = EngineFactory::Create(config);

    // Load per-app mode data if smart switch enabled
    if (smartSwitch_) {
        appModeMap_ = ConfigManager::LoadSmartSwitchData(ConfigManager::GetConfigPath());
        // Create shared memory and populate from TOML data
        if (smartSwitchMgr_.Create()) {
            smartSwitchMgr_.LoadFromMap(appModeMap_);
        }
    }

    // Load per-app code table data
    rememberCodeTable_ = config.rememberCodeTable;
    currentCodeTable_ = config.codeTable;
    if (rememberCodeTable_) {
        appCodeTableMap_ = ConfigManager::LoadPerAppCodeTable(ConfigManager::GetConfigPath());
    }

    // Load excluded apps list
    if (excludeApps_) {
        auto apps = ConfigManager::LoadExcludedApps(ConfigManager::GetConfigPath());
        excludedAppSet_.clear();
        for (auto& app : apps) {
            excludedAppSet_.insert(std::move(app));
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
    // Persist smart switch data to disk on exit
    if (smartSwitch_ && !appModeMap_.empty()) {
        (void)ConfigManager::SaveSmartSwitchData(ConfigManager::GetConfigPath(), appModeMap_);
    }
    // Persist per-app code table data
    if (rememberCodeTable_ && !appCodeTableMap_.empty()) {
        (void)ConfigManager::SavePerAppCodeTable(ConfigManager::GetConfigPath(), appCodeTableMap_);
    }
    if (keyboardHook_) {
        UnhookWindowsHookEx(keyboardHook_);
        keyboardHook_ = nullptr;
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

    // Commit any pending composition before switching
    if (engine_->Count() > 0) {
        CommitComposition();
    }

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

    // Always update per-app map — the map is authoritative when rememberCodeTable_ is on.
    // Use the target app: when called from the tray menu, currentExe_ may point to
    // explorer.exe (shell gets focus on tray click). Fall back to previousExe_ which
    // is the app the user was actually working in.
    if (rememberCodeTable_) {
        const std::wstring& targetExe = !previousExe_.empty() ? previousExe_ : currentExe_;
        if (!targetExe.empty()) {
            appCodeTableMap_[targetExe] = static_cast<uint8_t>(ct);
            HOOK_LOG(L"  SetCodeTable: %d for '%s'", static_cast<int>(ct), targetExe.c_str());
            // Persist immediately so the change survives restart
            (void)ConfigManager::SavePerAppCodeTable(
                ConfigManager::GetConfigPath(), appCodeTableMap_);
        }
    }
}

CodeTable HookEngine::GetCodeTable() const noexcept {
    // When per-app code table is enabled, the map is authoritative.
    // Prefer previousExe_ — when the user right-clicks the tray, currentExe_ changes
    // to explorer.exe (shell), but previousExe_ is the app they were actually using.
    if (rememberCodeTable_) {
        auto lookup = [&](const std::wstring& exe) -> const uint8_t* {
            if (exe.empty()) return nullptr;
            auto it = appCodeTableMap_.find(exe);
            return it != appCodeTableMap_.end() ? &it->second : nullptr;
        };
        if (auto* v = lookup(previousExe_)) return static_cast<CodeTable>(*v);
        if (auto* v = lookup(currentExe_)) return static_cast<CodeTable>(*v);
    }
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
    engine_ = EngineFactory::Create(config);
    NEXTKEY_LOG(L"HookEngine: engine recreated (%s, modernOrtho=%d, allowZwjf=%d)",
                currentMethod_ == InputMethod::VNI ? L"VNI" : L"Telex",
                config.modernOrtho ? 1 : 0, config.allowZwjf ? 1 : 0);
    beepOnSwitch_ = config.beepOnSwitch;
    smartSwitch_ = config.smartSwitch;
    excludeApps_ = config.excludeApps;
    autoCaps_ = config.autoCaps;
    tempOffSpellByCtrl_ = config.tempOffSpellByCtrl;
    tempOffByAlt_ = config.tempOffByAlt;
    macroEnabled_ = config.macroEnabled;
    macroInEnglish_ = config.macroInEnglish;
    tempOffMacroByEsc_ = config.tempOffMacroByEsc;
    if (macroEnabled_) {
        macroTable_ = ConfigManager::LoadMacros(ConfigManager::GetConfigPath());
    } else {
        macroTable_.clear();
    }

    // Update per-app code table: explicit change in Settings applies globally
    rememberCodeTable_ = config.rememberCodeTable;
    if (config.codeTable != currentCodeTable_) {
        // User explicitly changed code table — clear all per-app overrides
        // so every app uses the new setting going forward
        appCodeTableMap_.clear();
    }
    currentCodeTable_ = config.codeTable;

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

    // Reload hotkey config
    auto hotkeyOpt = ConfigManager::LoadHotkeyConfig(ConfigManager::GetConfigPath());
    if (hotkeyOpt) {
        hotkeyConfig_ = *hotkeyOpt;
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
    if (nCode == HC_ACTION && s_instance) {
        auto* pKey = reinterpret_cast<KBDLLHOOKSTRUCT*>(lParam);

        // Skip our own injected events (dwExtraInfo magic number — primary method)
        if (pKey->dwExtraInfo == NEXUSKEY_EXTRA_INFO) {
            HOOK_LOG(L"  PASSTHRU (dwExtraInfo=NK): vk=0x%02X scan=0x%04X flags=0x%08X",
                     pKey->vkCode, pKey->scanCode, pKey->flags);
            return CallNextHookEx(nullptr, nCode, wParam, lParam);
        }

        // Skip events while we're sending (safety backup)
        if (s_instance->sending_) {
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
            if (s_instance->ProcessKeyDown(pKey->vkCode, pKey->scanCode, pKey->flags)) {
                HOOK_LOG(L"  → EATEN (key-down vk=0x%02X)", pKey->vkCode);
                return 1;  // Eat the keystroke
            }
        } else if (isUp) {
            if (s_instance->ProcessKeyUp(pKey->vkCode, pKey->flags)) {
                return 1;  // Eat the keystroke
            }
        }
    }

    return CallNextHookEx(nullptr, nCode, wParam, lParam);
}

void CALLBACK HookEngine::WinEventProc(HWINEVENTHOOK, DWORD, HWND, LONG, LONG, DWORD, DWORD) {
    if (s_instance) {
        HOOK_LOG(L"FOCUS changed — resetting composition (engine count=%zu, prev='%s')",
                 s_instance->engine_->Count(), s_instance->previousComposition_.c_str());
        s_instance->autoCapState_ = 0;
        s_instance->OnFocusChanged();
    }
}

// Forward declarations for file-scope helpers used in ProcessKeyDown
static bool UsePostMessage(HWND hwnd);
static HWND GetInputTarget();

// ═══════════════════════════════════════════════════════════
// Core Processing
// ═══════════════════════════════════════════════════════════

bool HookEngine::ProcessKeyDown(DWORD vkCode, DWORD /*scanCode*/, DWORD /*flags*/) {
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

    // 3. English mode fast path — skip Vietnamese-only processing
    if (!vietnameseMode_) {
        if (macroEnabled_ && macroInEnglish_) {
            // Track macro keys and handle expansion in English mode
            if (vkCode >= 0x41 && vkCode <= 0x5A) {
                bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
                rawMacroBuffer_ += shift ? static_cast<wchar_t>(vkCode)
                                         : towlower(static_cast<wchar_t>(vkCode));
            } else if (tempOffMacroByEsc_ && vkCode == VK_ESCAPE && rawMacroBuffer_.empty()) {
                tempMacroOff_ = true;
                return false;
            } else if (IsCommitTrigger(vkCode) && !tempMacroOff_ && !rawMacroBuffer_.empty()) {
                std::wstring lowerKey = rawMacroBuffer_;
                for (auto& c : lowerKey) c = towlower(c);
                auto it = macroTable_.find(lowerKey);
                if (it != macroTable_.end()) {
                    SendBackspaces(rawMacroBuffer_.size());
                    std::wstring expansion = it->second;
                    if (autoCaps_ && !rawMacroBuffer_.empty()) {
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
                    HWND target = GetInputTarget();
                    bool usePost = target && UsePostMessage(target);
                    SendCharEvents(target, expansion, usePost);
                    rawMacroBuffer_.clear();
                    return false;
                }
            } else if (vkCode == VK_BACK && !rawMacroBuffer_.empty()) {
                rawMacroBuffer_.pop_back();
            } else if (!(vkCode >= 0x41 && vkCode <= 0x5A)) {
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

    // 3b. Macro: track raw alpha keys
    if (macroEnabled_ && vkCode >= 0x41 && vkCode <= 0x5A) {
        bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
        rawMacroBuffer_ += shift ? static_cast<wchar_t>(vkCode)
                                 : towlower(static_cast<wchar_t>(vkCode));
    }

    // 3c. Temp off macro by Esc: press Esc with no pending text → skip macro for next word
    if (tempOffMacroByEsc_ && macroEnabled_ && vkCode == VK_ESCAPE
        && engine_->Count() == 0 && rawMacroBuffer_.empty()) {
        tempMacroOff_ = true;
        HOOK_LOG(L"  tempMacroOff: enabled by Esc");
        return false;  // Let Esc pass through
    }

    // 3d. Macro expansion on commit trigger
    if (macroEnabled_ && !tempMacroOff_ && IsCommitTrigger(vkCode) && !rawMacroBuffer_.empty()) {
        // Lookup with lowercase key (macros are stored lowercase)
        std::wstring lowerKey = rawMacroBuffer_;
        for (auto& c : lowerKey) c = towlower(c);
        auto it = macroTable_.find(lowerKey);
        if (it != macroTable_.end()) {
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
            }
            SendBackspaces(bsCount);

            // Auto-capitalize expansion to match typed case pattern
            std::wstring expansion = it->second;
            if (autoCaps_ && !rawMacroBuffer_.empty()) {
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

            HWND target = GetInputTarget();
            bool usePost = target && UsePostMessage(target);
            SendCharEvents(target, expansion, usePost);
            engine_->Reset();
            previousComposition_.clear();
            previousEncodedWidths_.clear();
            rawMacroBuffer_.clear();
            return false;  // Let trigger key pass through
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
        // Reset (don't auto-restore) — shortcuts like Ctrl+A/C/Z change text state
        // in unpredictable ways; sending replacement backspaces would interfere.
        if (engine_->Count() > 0) {
            ResetComposition();
        }
        return false;
    }

    // 6. A-Z keys → process with engine
    if (vkCode >= 0x41 && vkCode <= 0x5A) {
        HOOK_LOG(L"  alpha key '%c' → HandleAlphaKey", static_cast<char>(vkCode));
        HandleAlphaKey(vkCode);
        return true;  // Eat the original keystroke
    }

    // 6b. Bracket keys [ ] → engine modifier for Full Telex ([ → ơ, ] → ư)
    if (currentMethod_ == InputMethod::Telex &&
        (vkCode == VK_OEM_4 || vkCode == VK_OEM_6)) {
        bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
        if (!shift) {
            wchar_t ch = (vkCode == VK_OEM_4) ? L'[' : L']';
            engine_->PushChar(ch);
            std::wstring composition = engine_->Peek();
            HOOK_LOG(L"  bracket '%c' → Peek()='%s'", ch, composition.c_str());
            ReplaceComposition(composition);
            return true;  // Eat the original keystroke
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
        if (restored) {
            // Auto-restore changed text — re-inject trigger key after replacement
            // to guarantee correct ordering (replacement before trigger)
            HOOK_LOG(L"  re-inject trigger vk=0x%02X after auto-restore", vkCode);
            InjectKey(vkCode);
            return true;  // Eat original trigger
        }
        return false;  // Normal commit, let trigger pass through
    }

    // 9. Any other key with pending composition → commit and pass through
    if (engine_->Count() > 0) {
        HOOK_LOG(L"  other key vk=0x%02X with pending composition → commit", vkCode);
        bool restored = CommitComposition();
        if (restored) {
            InjectKey(vkCode);
            return true;
        }
    }

    return false;
}

bool HookEngine::ProcessKeyUp(DWORD vkCode, DWORD /*flags*/) {
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
                altTapCount_ = 0;
                HOOK_LOG(L"  DOUBLE-ALT: tempEngineOff_ = %d", tempEngineOff_ ? 1 : 0);
            } else {
                altTapCount_ = 1;
                lastAltReleaseTime_ = now;
            }
        } else if (vkCode == VK_LMENU || vkCode == VK_RMENU) {
            altTapCount_ = 0;  // Contaminated Alt release
        }

        TrackModifier(vkCode, false);
    }

    return false;  // Never eat key-up
}

// ═══════════════════════════════════════════════════════════
// Input Engine Interaction
// ═══════════════════════════════════════════════════════════

void HookEngine::HandleAlphaKey(DWORD vkCode) {
    bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
    wchar_t ch = static_cast<wchar_t>(vkCode);
    if (!shift) ch = towlower(ch);

    // Auto-capitalize first letter after sentence-ending punctuation
    if (autoCaps_ && autoCapState_ == 2 && engine_->Count() == 0) {
        ch = towupper(ch);
        autoCapState_ = 0;
    }

    engine_->PushChar(ch);
    std::wstring composition = engine_->Peek();

    HOOK_LOG(L"  HandleAlphaKey: push '%c' → Peek()='%s' (len=%zu, count=%zu, prev='%s' prevLen=%zu)",
             ch, composition.c_str(), composition.size(), engine_->Count(),
             previousComposition_.c_str(), previousComposition_.size());

    ReplaceComposition(composition);
}

void HookEngine::HandleBackspace() {
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
    }
}

bool HookEngine::CommitComposition() {
    HOOK_LOG(L"  CommitComposition (count=%zu, prev='%s')", engine_->Count(), previousComposition_.c_str());
    std::wstring committed = engine_->Commit();

    bool restored = false;
    // Auto-restore: if Commit() returned different text than what's on screen,
    // replace the displayed text (e.g., "gôgle" → "google")
    if (!previousComposition_.empty() && committed != previousComposition_) {
        HOOK_LOG(L"  AutoRestore: '%s' → '%s'", previousComposition_.c_str(), committed.c_str());
        ReplaceComposition(committed);
        restored = true;
    }

    engine_->Reset();
    previousComposition_.clear();
    previousEncodedWidths_.clear();
    rawMacroBuffer_.clear();
    tempMacroOff_ = false;
    return restored;
}

void HookEngine::ResetComposition() {
    HOOK_LOG(L"  ResetComposition (count=%zu, prev='%s')", engine_->Count(), previousComposition_.c_str());
    if (engine_->Count() > 0) {
        engine_->Reset();
    }
    previousComposition_.clear();
    previousEncodedWidths_.clear();
    rawMacroBuffer_.clear();
    tempMacroOff_ = false;
}

// ═══════════════════════════════════════════════════════════
// Output — Multi-method: PostMessage for Win32, SendInput for others
// ═══════════════════════════════════════════════════════════

/// Detect if a window accepts PostMessage WM_CHAR (standard Win32 edit controls)
static bool UsePostMessage(HWND hwnd) {
    wchar_t className[128] = {};
    GetClassNameW(hwnd, className, 128);

    // Standard Win32 edit controls that accept WM_CHAR
    if (_wcsicmp(className, L"Edit") == 0) return true;
    if (_wcsnicmp(className, L"RichEdit", 8) == 0) return true;
    if (_wcsicmp(className, L"RICHEDIT50W") == 0) return true;
    if (_wcsicmp(className, L"Scintilla") == 0) return true;
    if (_wcsicmp(className, L"Notepad") == 0) return true;

    return false;
}

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

void HookEngine::SendBackspaceEvents(HWND target, size_t count, bool usePost) {
    if (usePost) {
        WORD bsScan = static_cast<WORD>(MapVirtualKeyW(VK_BACK, MAPVK_VK_TO_VSC));
        for (size_t i = 0; i < count; ++i) {
            LPARAM downParam = 1 | (static_cast<LPARAM>(bsScan) << 16);
            LPARAM upParam   = 1 | (static_cast<LPARAM>(bsScan) << 16) | (1L << 30) | (1L << 31);
            PostMessageW(target, WM_KEYDOWN, VK_BACK, downParam);
            PostMessageW(target, WM_KEYUP, VK_BACK, upParam);
        }
    } else {
        // Batch all backspace events into a single SendInput call
        WORD bsScan = static_cast<WORD>(MapVirtualKeyW(VK_BACK, MAPVK_VK_TO_VSC));
        std::vector<INPUT> events(count * 2);
        for (size_t i = 0; i < count; ++i) {
            events[i * 2].type = INPUT_KEYBOARD;
            events[i * 2].ki.wVk = VK_BACK;
            events[i * 2].ki.wScan = bsScan;
            events[i * 2].ki.dwExtraInfo = NEXUSKEY_EXTRA_INFO;
            events[i * 2 + 1].type = INPUT_KEYBOARD;
            events[i * 2 + 1].ki.wVk = VK_BACK;
            events[i * 2 + 1].ki.wScan = bsScan;
            events[i * 2 + 1].ki.dwFlags = KEYEVENTF_KEYUP;
            events[i * 2 + 1].ki.dwExtraInfo = NEXUSKEY_EXTRA_INFO;
        }
        sending_ = true;
        SendInput(static_cast<UINT>(events.size()), events.data(), sizeof(INPUT));
        sending_ = false;
    }
}

void HookEngine::SendCharEvents(HWND target, const std::wstring& text, bool usePost) {
    if (usePost) {
        for (wchar_t ch : text) {
            PostMessageW(target, WM_CHAR, static_cast<WPARAM>(ch), 1);
        }
    } else {
        // Batch all character events into a single SendInput call
        std::vector<INPUT> events(text.size() * 2);
        for (size_t i = 0; i < text.size(); ++i) {
            events[i * 2].type = INPUT_KEYBOARD;
            events[i * 2].ki.wScan = text[i];
            events[i * 2].ki.dwFlags = KEYEVENTF_UNICODE;
            events[i * 2].ki.dwExtraInfo = NEXUSKEY_EXTRA_INFO;
            events[i * 2 + 1].type = INPUT_KEYBOARD;
            events[i * 2 + 1].ki.wScan = text[i];
            events[i * 2 + 1].ki.dwFlags = KEYEVENTF_UNICODE | KEYEVENTF_KEYUP;
            events[i * 2 + 1].ki.dwExtraInfo = NEXUSKEY_EXTRA_INFO;
        }
        sending_ = true;
        SendInput(static_cast<UINT>(events.size()), events.data(), sizeof(INPUT));
        sending_ = false;
    }
}

bool HookEngine::IsBrowserLike(HWND hwnd) {
    HWND fg = hwnd;
    // Walk up to the top-level window if needed
    HWND parent = GetAncestor(fg, GA_ROOT);
    if (parent) fg = parent;

    DWORD pid = 0;
    GetWindowThreadProcessId(fg, &pid);
    if (!pid) return false;

    HANDLE hProc = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!hProc) return false;

    wchar_t exePath[MAX_PATH] = {};
    DWORD size = MAX_PATH;
    bool result = false;
    if (QueryFullProcessImageNameW(hProc, 0, exePath, &size)) {
        // Extract filename from path
        const wchar_t* filename = wcsrchr(exePath, L'\\');
        filename = filename ? filename + 1 : exePath;

        if (_wcsnicmp(filename, L"chrome", 6) == 0 ||
            _wcsnicmp(filename, L"msedge", 6) == 0 ||
            _wcsnicmp(filename, L"firefox", 7) == 0 ||
            _wcsnicmp(filename, L"brave", 5) == 0 ||
            _wcsnicmp(filename, L"opera", 5) == 0 ||
            _wcsnicmp(filename, L"vivaldi", 7) == 0) {
            result = true;
        }
    }
    CloseHandle(hProc);
    return result;
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

            if (_wcsnicmp(filename, L"chrome", 6) == 0 ||
                _wcsnicmp(filename, L"msedge", 6) == 0 ||
                _wcsnicmp(filename, L"firefox", 7) == 0 ||
                _wcsnicmp(filename, L"brave", 5) == 0 ||
                _wcsnicmp(filename, L"opera", 5) == 0 ||
                _wcsnicmp(filename, L"vivaldi", 7) == 0) {
                isElectron = false;
            }
        }
        CloseHandle(hProc);
        return isElectron;
    }

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
        result = filename ? filename + 1 : exePath;
        // Lowercase for consistent map keys
        for (auto& c : result) c = towlower(c);
    }
    CloseHandle(hProc);
    return result;
}

void HookEngine::OnFocusChanged() {
    ResetComposition();
    tempEngineOff_ = false;
    CheckConfigEvent();

    // Detect Qt/Electron apps — skip U+202F to avoid first-word delay
    HWND fg = GetForegroundWindow();
    skipEmptyChar_ = fg && IsQtElectronApp(fg);

    // Skip focus tracking entirely if no feature needs it
    if (!smartSwitch_ && !excludeApps_ && !rememberCodeTable_) return;

    bool wasExcluded = isExcludedApp_;

    // Save mode for previous app (smart switch, skip excluded apps)
    if (smartSwitch_ && !currentExe_.empty() && !wasExcluded) {
        appModeMap_[currentExe_] = vietnameseMode_;
        smartSwitchMgr_.SetAppMode(currentExe_, vietnameseMode_);
    }

    // Save previous app's code table
    if (rememberCodeTable_ && !currentExe_.empty() && !wasExcluded) {
        appCodeTableMap_[currentExe_] = static_cast<uint8_t>(currentCodeTable_);
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

    // Restore code table for new app
    if (rememberCodeTable_) {
        auto it = appCodeTableMap_.find(currentExe_);
        if (it != appCodeTableMap_.end()) {
            auto restored = static_cast<CodeTable>(it->second);
            if (restored != currentCodeTable_) {
                currentCodeTable_ = restored;
                HOOK_LOG(L"  RememberCode: restored codeTable=%d for '%s'",
                         static_cast<int>(currentCodeTable_), currentExe_.c_str());
            }
        } else {
            // First time seeing this app — record current code table
            appCodeTableMap_[currentExe_] = static_cast<uint8_t>(currentCodeTable_);
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

    bool usePost = UsePostMessage(target);

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

        HOOK_LOG(L"  ReplaceComposition[encoded]: prev='%s' new='%s' common=%zu BS=%zu encodedLen=%zu method=%s",
                 previousComposition_.c_str(), newText.c_str(), commonLen, backspaceCount,
                 encodedToSend.size(), usePost ? L"PostMessage" : L"SendInput");

        if (usePost) {
            if (backspaceCount > 0) {
                SendBackspaceEvents(target, backspaceCount, true);
            }
            if (!encodedToSend.empty()) {
                SendCharEvents(target, encodedToSend, true);
            }
        } else {
            bool needEmpty = (backspaceCount > 0 && !skipEmptyChar_);
            size_t bsTotal = backspaceCount + (needEmpty ? 1 : 0);  // +1 for U+202F

            size_t totalEvents = (needEmpty ? 2 : 0) + bsTotal * 2 + encodedToSend.size() * 2;
            if (totalEvents == 0) {
                previousComposition_ = newText;
                return;
            }

            std::vector<INPUT> events(totalEvents);
            size_t idx = 0;
            WORD bsScan = static_cast<WORD>(MapVirtualKeyW(VK_BACK, MAPVK_VK_TO_VSC));

            // 1. U+202F word-boundary breaker
            if (needEmpty) {
                events[idx].type = INPUT_KEYBOARD;
                events[idx].ki.wScan = 0x202F;
                events[idx].ki.dwFlags = KEYEVENTF_UNICODE;
                events[idx].ki.dwExtraInfo = NEXUSKEY_EXTRA_INFO;
                idx++;
                events[idx].type = INPUT_KEYBOARD;
                events[idx].ki.wScan = 0x202F;
                events[idx].ki.dwFlags = KEYEVENTF_UNICODE | KEYEVENTF_KEYUP;
                events[idx].ki.dwExtraInfo = NEXUSKEY_EXTRA_INFO;
                idx++;
            }

            // 2. Backspaces
            for (size_t i = 0; i < bsTotal; ++i) {
                events[idx].type = INPUT_KEYBOARD;
                events[idx].ki.wVk = VK_BACK;
                events[idx].ki.wScan = bsScan;
                events[idx].ki.dwExtraInfo = NEXUSKEY_EXTRA_INFO;
                idx++;
                events[idx].type = INPUT_KEYBOARD;
                events[idx].ki.wVk = VK_BACK;
                events[idx].ki.wScan = bsScan;
                events[idx].ki.dwFlags = KEYEVENTF_KEYUP;
                events[idx].ki.dwExtraInfo = NEXUSKEY_EXTRA_INFO;
                idx++;
            }

            // 3. Encoded characters
            for (wchar_t ch : encodedToSend) {
                events[idx].type = INPUT_KEYBOARD;
                events[idx].ki.wScan = ch;
                events[idx].ki.dwFlags = KEYEVENTF_UNICODE;
                events[idx].ki.dwExtraInfo = NEXUSKEY_EXTRA_INFO;
                idx++;
                events[idx].type = INPUT_KEYBOARD;
                events[idx].ki.wScan = ch;
                events[idx].ki.dwFlags = KEYEVENTF_UNICODE | KEYEVENTF_KEYUP;
                events[idx].ki.dwExtraInfo = NEXUSKEY_EXTRA_INFO;
                idx++;
            }

            sending_ = true;
            SendInput(static_cast<UINT>(events.size()), events.data(), sizeof(INPUT));
            sending_ = false;
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

    HOOK_LOG(L"  ReplaceComposition: prev='%s' new='%s' common=%zu BS=%zu send='%s' method=%s",
             previousComposition_.c_str(), newText.c_str(), commonLen, backspaceCount,
             toSend.c_str(), usePost ? L"PostMessage" : L"SendInput");

    if (usePost) {
        // PostMessage path: send separately (no batching needed, PostMessage is async)
        if (backspaceCount > 0) {
            SendBackspaceEvents(target, backspaceCount, true);
        }
        if (!toSend.empty()) {
            SendCharEvents(target, toSend, true);
        }
    } else {
        // SendInput path: batch everything into ONE SendInput call for atomicity + speed
        bool needEmpty = (backspaceCount > 0 && !skipEmptyChar_);
        if (needEmpty) backspaceCount++;  // +1 to also delete the U+202F

        size_t totalEvents = (needEmpty ? 2 : 0) + backspaceCount * 2 + toSend.size() * 2;
        if (totalEvents == 0) {
            previousComposition_ = newText;
            return;
        }

        std::vector<INPUT> events(totalEvents);
        size_t idx = 0;
        WORD bsScan = static_cast<WORD>(MapVirtualKeyW(VK_BACK, MAPVK_VK_TO_VSC));

        // 1. U+202F to break autocomplete word boundary
        if (needEmpty) {
            events[idx].type = INPUT_KEYBOARD;
            events[idx].ki.wScan = 0x202F;
            events[idx].ki.dwFlags = KEYEVENTF_UNICODE;
            events[idx].ki.dwExtraInfo = NEXUSKEY_EXTRA_INFO;
            idx++;
            events[idx].type = INPUT_KEYBOARD;
            events[idx].ki.wScan = 0x202F;
            events[idx].ki.dwFlags = KEYEVENTF_UNICODE | KEYEVENTF_KEYUP;
            events[idx].ki.dwExtraInfo = NEXUSKEY_EXTRA_INFO;
            idx++;
        }

        // 2. Backspaces (including one for U+202F)
        for (size_t i = 0; i < backspaceCount; ++i) {
            events[idx].type = INPUT_KEYBOARD;
            events[idx].ki.wVk = VK_BACK;
            events[idx].ki.wScan = bsScan;
            events[idx].ki.dwExtraInfo = NEXUSKEY_EXTRA_INFO;
            idx++;
            events[idx].type = INPUT_KEYBOARD;
            events[idx].ki.wVk = VK_BACK;
            events[idx].ki.wScan = bsScan;
            events[idx].ki.dwFlags = KEYEVENTF_KEYUP;
            events[idx].ki.dwExtraInfo = NEXUSKEY_EXTRA_INFO;
            idx++;
        }

        // 3. New characters
        for (wchar_t ch : toSend) {
            events[idx].type = INPUT_KEYBOARD;
            events[idx].ki.wScan = ch;
            events[idx].ki.dwFlags = KEYEVENTF_UNICODE;
            events[idx].ki.dwExtraInfo = NEXUSKEY_EXTRA_INFO;
            idx++;
            events[idx].type = INPUT_KEYBOARD;
            events[idx].ki.wScan = ch;
            events[idx].ki.dwFlags = KEYEVENTF_UNICODE | KEYEVENTF_KEYUP;
            events[idx].ki.dwExtraInfo = NEXUSKEY_EXTRA_INFO;
            idx++;
        }

        sending_ = true;
        SendInput(static_cast<UINT>(events.size()), events.data(), sizeof(INPUT));
        sending_ = false;
    }

    previousComposition_ = newText;
}

void HookEngine::SendBackspaces(size_t count) {
    HOOK_LOG(L"  SendBackspaces: %zu", count);

    HWND target = GetInputTarget();
    if (!target) return;

    bool usePost = UsePostMessage(target);
    SendBackspaceEvents(target, count, usePost);
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
    SendInput(2, inputs, sizeof(INPUT));
    sending_ = false;
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

}  // namespace NextKey
