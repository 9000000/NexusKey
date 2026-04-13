// NexusKey - Engine Controller Implementation
// SPDX-License-Identifier: GPL-3.0-only

#include "stdafx.h"
#include "EngineController.h"
#include "CompositionEditSession.h"
#include "InputScopeChecker.h"
#include "Define.h"
#include "core/engine/EngineFactory.h"

namespace NextKey {
namespace TSF {

EngineController::EngineController() {
    // Try to open SharedState from main app (read-write for flag toggling)
    if (sharedState_.OpenReadWrite()) {
        SharedState state = sharedState_.Read();
        if (state.IsValid()) {
            // Apply config from SharedState
            ApplySharedState(state);
            lastEpoch_ = state.epoch;
            TSF_LOG(L"EngineController initialized from SharedState (epoch=%u, method=%d)",
                    state.epoch, state.inputMethod);
        } else {
            // SharedState invalid, use defaults
            config_.inputMethod = InputMethod::Telex;
            config_.spellCheckEnabled = false;
            config_.optimizeLevel = 0;
            currentMethod_ = InputMethod::Telex;
            engine_ = EngineFactory::Create(config_);
            TSF_LOG(L"EngineController: SharedState invalid, using defaults");
        }
    } else {
        // SharedState not available = EXE not running → disabled
        config_.inputMethod = InputMethod::Telex;
        config_.spellCheckEnabled = false;
        config_.optimizeLevel = 0;
        currentMethod_ = InputMethod::Telex;
        engine_ = EngineFactory::Create(config_);
        engineEnabled_ = false;
        TSF_LOG(L"EngineController: SharedState not available, engine disabled");
    }

    compositionMgr_.SetEngineController(this);
}

EngineController::~EngineController() {
    if (lastContext_) {
        lastContext_->Release();
        lastContext_ = nullptr;
    }
    TSF_LOG(L"EngineController destroyed");
}

void EngineController::CheckContextBlocked(ITfContext* pContext) {
    if (pContext == lastContext_) return;  // Same context, use cached result

    // Release old context, AddRef new one (safe identity comparison)
    if (lastContext_) lastContext_->Release();
    lastContext_ = pContext;
    if (lastContext_) lastContext_->AddRef();
    contextBlocked_ = false;

    if (!pContext) return;

    auto* pSession = new InputScopeCheckSession(pContext, &contextBlocked_);
    HRESULT hrSession = S_OK;
    HRESULT hr = pContext->RequestEditSession(
        clientId_, pSession, TF_ES_SYNC | TF_ES_READ, &hrSession);
    pSession->Release();

    if (FAILED(hr) || FAILED(hrSession)) {
        // If we can't check, assume not blocked
        contextBlocked_ = false;
    }

    if (contextBlocked_) {
        TSF_LOG(L"Context blocked (password/PIN/email field)");
    }
}

bool EngineController::WantKey(UINT vkCode, bool /*isKeyDown*/) {
    // 0. Check if engine should process keys
    if (sharedState_.IsConnected()) {
        // Read flags directly from shared memory (live, zero-copy)
        uint32_t flags = sharedState_.ReadFlags();
        if (!(flags & SharedFlags::ENGINE_ENABLED)) return false;

        // [Checkpoint: TSF_ACTIVE] Only process keys when foreground app is in TSF list
        // EXE sets this flag on foreground change — prevents double-processing with hook
        bool newTsfActive = (flags & SharedFlags::TSF_ACTIVE) != 0;
        if (newTsfActive != tsfActive_) {
            tsfActive_ = newTsfActive;
            TSF_LOG(L"[Checkpoint] TSF_ACTIVE: %s", tsfActive_ ? L"ON (processing keys)" : L"OFF (passthrough)");
        }
        if (!tsfActive_) return false;

        // Detect V/E mode changes (e.g. from EXE hotkey) and refresh icon
        bool newVietnameseMode = (flags & SharedFlags::VIETNAMESE_MODE) != 0;
        if (newVietnameseMode != vietnameseMode_) {
            vietnameseMode_ = newVietnameseMode;
            if (langBarButton_) langBarButton_->Refresh();
        }

        if (!vietnameseMode_) return false;
    } else {
        // SharedState not available = EXE not running → pass all keys through
        return false;
    }

    // Auto-caps state machine: track sentence-ending punctuation
    if (config_.autoCaps) {
        bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
        if (vkCode == VK_OEM_PERIOD || (vkCode == 0xBF && shift) || (vkCode == '1' && shift)) {
            // '.', '?', '!'
            autoCapState_ = 1;
        } else if (vkCode == VK_SPACE && autoCapState_ == 1) {
            autoCapState_ = 2;
        } else if (vkCode == VK_RETURN) {
            autoCapState_ = 2;
        } else if (vkCode >= 0x41 && vkCode <= 0x5A) {
            // Letter key — don't reset, HandleKey will consume it
        } else {
            autoCapState_ = 0;
        }
    }

    // 1. NEVER intercept if any modifier (except Shift) is down.
    bool ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
    bool alt = (GetKeyState(VK_MENU) & 0x8000) != 0;
    bool win = (GetKeyState(VK_LWIN) & 0x8000) != 0 || (GetKeyState(VK_RWIN) & 0x8000) != 0;

    if (ctrl || alt || win) {
        return false;
    }

    bool engineHasComp = engine_->Count() > 0;

    // 2. We want A-Z keys for Telex processing
    if (vkCode >= 0x41 && vkCode <= 0x5A) {
        return true;
    }

    // 3. We handle Backspace ONLY if we have internal content.
    if (vkCode == VK_BACK) {
        return engineHasComp;
    }

    // 4. Space handling depends on the app
    if (vkCode == VK_SPACE) {
        if (isScintillaApp_) {
            // For Scintilla apps: don't claim space, let it trigger commit via "non-handled key" path
            // and pass through naturally
            return false;
        }
        // For other apps: claim space if we have composition
        return engineHasComp;
    }

    // 4. For Enter and all others, let the app handle it (we'll commit in OnTestKeyDown)
    return false;
}

void EngineController::RequestEditSession(ITfContext* pContext, EditSession* pEditSession) {
    if (pContext == nullptr || pEditSession == nullptr) return;

    HRESULT hrSession = S_OK;
    HRESULT hr = pContext->RequestEditSession(
        clientId_,
        pEditSession,
        TF_ES_SYNC | TF_ES_READWRITE,
        &hrSession
    );

    if (FAILED(hr)) {
        TSF_LOG(L"RequestEditSession request failed");
    } else if (FAILED(hrSession)) {
        TSF_LOG(L"Edit session execution failed");
    }
}

bool EngineController::HandleKey(ITfContext* pContext, UINT vkCode) {
    // 1. Handle Backspace (only if we have content, as decided by WantKey)
    if (vkCode == VK_BACK) {
        ProcessBackspace(pContext);
        return true;
    }

    // 2. Handle Space (only reaches here for non-Scintilla apps, as decided by WantKey)
    if (vkCode == VK_SPACE) {
        // For non-Scintilla apps: append space to committed text
        CommitWithChar(pContext, L' ');
        return true;  // Eat space
    }

    // 3. Check if character key (A-Z)
    if (vkCode >= 0x41 && vkCode <= 0x5A) {
        bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
        bool capsLock = (GetKeyState(VK_CAPITAL) & 0x0001) != 0;
        bool upper = shift != capsLock;  // XOR: Shift inverts Caps Lock
        wchar_t ch = static_cast<wchar_t>(vkCode);
        if (!upper) ch = towlower(ch);

        // Auto-capitalize first letter after sentence-ending punctuation
        if (config_.autoCaps && autoCapState_ == 2 && engine_->Count() == 0) {
            ch = towupper(ch);
            autoCapState_ = 0;
        }

        TSF_LOG(L"HandleKey: pushing char '%c'", ch);
        engine_->PushChar(ch);
        
        std::wstring composition = engine_->Peek();
        TSF_LOG(L"HandleKey: got composition, starting/updating");

        if (!compositionMgr_.IsComposing()) {
            // Start new composition
            TSF_LOG(L"HandleKey: Starting new composition");
            auto* pSession = new StartCompositionEditSession(pContext, &compositionMgr_, composition);
            RequestEditSession(pContext, pSession);
            pSession->Release();

            // If composition failed to start, reset engine to stay in sync
            if (!compositionMgr_.IsComposing()) {
                TSF_LOG(L"HandleKey: Composition failed, resetting engine");
                engine_->Reset();
                return false;  // Let the key pass through
            }
        } else {
            // Update existing composition
            TSF_LOG(L"HandleKey: Updating composition");
            auto* pSession = new UpdateCompositionEditSession(pContext, &compositionMgr_, composition);
            RequestEditSession(pContext, pSession);
            pSession->Release();
        }

        TSF_LOG(L"Key processed, composition updated");
        return true;
    }

    return false;
}

void EngineController::ProcessBackspace(ITfContext* pContext) {
    engine_->Backspace();

    if (engine_->Count() > 0) {
        std::wstring composition = engine_->Peek();
        auto* pSession = new UpdateCompositionEditSession(pContext, &compositionMgr_, composition);
        RequestEditSession(pContext, pSession);
        pSession->Release();
    } else {
        // Composition empty - clear text and end composition (delete all chars)
        auto* pSession = new CommitEditSession(pContext, &compositionMgr_, L"");
        RequestEditSession(pContext, pSession);
        pSession->Release();
        TSF_LOG(L"Backspace: composition cleared");
    }
}

void EngineController::Commit(ITfContext* pContext) {
    // VietType pattern: Get committed text, then request edit session, then reset engine.
    // This ensures TSF state and engine state are synchronized atomically.
    std::wstring committed = engine_->Commit();

    // Commit: set final text and end composition in one atomic operation
    auto* pSession = new CommitEditSession(pContext, &compositionMgr_, committed);
    RequestEditSession(pContext, pSession);
    pSession->Release();

    // Reset engine state AFTER the edit session completes (synchronous)
    engine_->Reset();

    TSF_LOG(L"Commit called, text='%ls'", committed.c_str());
}

void EngineController::CommitWithChar(ITfContext* pContext, wchar_t appendChar) {
    // VietType pattern: Get committed text, then request edit session, then reset engine.
    std::wstring committed = engine_->Commit();

    // Append the commit character (e.g., space) if provided
    if (appendChar != L'\0') {
        committed += appendChar;
    }

    // Commit: set final text and end composition in one atomic operation
    auto* pSession = new CommitEditSession(pContext, &compositionMgr_, committed);
    RequestEditSession(pContext, pSession);
    pSession->Release();

    // Reset engine state AFTER the edit session completes (synchronous)
    engine_->Reset();

    TSF_LOG(L"CommitWithChar called, text='%ls'", committed.c_str());
}

void EngineController::Reset() {
    engine_->Reset();
    compositionMgr_.TerminateComposition();
    autoCapState_ = 0;
}

void EngineController::DetectScintillaApp() {
    // Cache Scintilla detection — called on context change, not per-keystroke.
    HWND hwnd = GetForegroundWindow();
    if (hwnd == nullptr) { isScintillaApp_ = false; return; }

    wchar_t className[256] = {0};
    if (GetClassNameW(hwnd, className, 256) > 0) {
        if (wcsstr(className, L"Notepad++") != nullptr) {
            isScintillaApp_ = true; return;
        }
    }

    HWND hwndFocus = GetFocus();
    if (hwndFocus != nullptr) {
        if (GetClassNameW(hwndFocus, className, 256) > 0) {
            if (wcsstr(className, L"Scintilla") != nullptr) {
                isScintillaApp_ = true; return;
            }
        }
    }

    isScintillaApp_ = false;
}

bool EngineController::CheckConfigEvent() {
    // Initialize event if not already done
    if (!configEvent_.IsValid()) {
        configEvent_.Initialize();
    }

    // Non-blocking check for signal
    if (!configEvent_.Wait(0)) {
        return false;  // No signal
    }

    // Config changed - read from SharedState
    TSF_LOG(L"Config event received, checking SharedState");

    if (!sharedState_.IsConnected()) {
        // Try to open SharedState if not connected
        if (!sharedState_.OpenReadWrite()) {
            TSF_LOG(L"CheckConfigEvent: SharedState not available");
            return false;
        }
    }

    SharedState state = sharedState_.Read();
    if (!state.IsValid()) {
        TSF_LOG(L"CheckConfigEvent: SharedState invalid");
        return false;
    }

    // Check if epoch changed (config actually updated)
    if (state.epoch == lastEpoch_) {
        TSF_LOG(L"CheckConfigEvent: epoch unchanged, skipping reload");
        return false;
    }

    // Apply new config
    TSF_LOG(L"Config changed: epoch %u -> %u", lastEpoch_, state.epoch);
    lastEpoch_ = state.epoch;
    ApplySharedState(state);

    return true;
}

void EngineController::RefreshFlags() {
    DetectScintillaApp();
    if (!sharedState_.IsConnected()) {
        // Try to reconnect (EXE may have restarted)
        if (!sharedState_.OpenReadWrite()) {
            engineEnabled_ = false;
            return;
        }
        TSF_LOG(L"Reconnected to SharedState");
    }

    SharedState state = sharedState_.Read();
    if (state.IsValid()) {
        bool wasEnabled = engineEnabled_;
        bool wasVietnamese = vietnameseMode_;
        engineEnabled_ = (state.flags & SharedFlags::ENGINE_ENABLED) != 0;
        vietnameseMode_ = (state.flags & SharedFlags::VIETNAMESE_MODE) != 0;

        if (!wasEnabled && engineEnabled_) {
            TSF_LOG(L"Engine re-enabled (app started)");
            ApplySharedState(state);
        } else if (wasEnabled && !engineEnabled_) {
            TSF_LOG(L"Engine disabled (app exited)");
        }

        // Refresh icon if Vietnamese mode changed (e.g. EXE hotkey toggled while bg)
        if (wasVietnamese != vietnameseMode_ && langBarButton_) {
            langBarButton_->Refresh();
        }
    } else {
        engineEnabled_ = false;
    }
}

void EngineController::ApplySharedState(const SharedState& state) {
    // Update runtime flags
    engineEnabled_ = (state.flags & SharedFlags::ENGINE_ENABLED) != 0;
    vietnameseMode_ = (state.flags & SharedFlags::VIETNAMESE_MODE) != 0;

    // Validate inputMethod (valid range: 0–2) before casting to enum
    InputMethod newMethod = InputMethod::Telex;  // safe default
    if (state.inputMethod <= 2) {
        newMethod = static_cast<InputMethod>(state.inputMethod);
    } else {
        TSF_LOG(L"[EngineController] Invalid inputMethod %d from shared state, using Telex",
                state.inputMethod);
    }

    // Validate optimizeLevel (valid range: 0–2)
    uint8_t optimizeLevel = 0;
    if (state.optimizeLevel <= 2) {
        optimizeLevel = state.optimizeLevel;
    }

    config_.inputMethod = newMethod;
    config_.spellCheckEnabled = state.spellCheck != 0;
    config_.optimizeLevel = optimizeLevel;
    DecodeFeatureFlags(state.GetFeatureFlags(), config_);

    // Recreate engine with updated config (engine stores a copy of TypingConfig,
    // so we must recreate it whenever any config field changes)
    // Commit any pending composition before recreating
    if (engine_ && engine_->Count() > 0) {
        (void)engine_->Commit();
    }

    currentMethod_ = newMethod;
    engine_ = EngineFactory::Create(config_);
    TSF_LOG(L"Engine recreated (%s, modernOrtho=%d, allowZwjf=%d)",
            newMethod == InputMethod::VNI ? L"VNI" : L"Telex",
            config_.modernOrtho ? 1 : 0, config_.allowZwjf ? 1 : 0);
}

void EngineController::ToggleVietnameseMode() {
    sharedState_.ToggleFlag(SharedFlags::VIETNAMESE_MODE);
    // Read back actual flag to stay in sync (avoids TOCTOU with EXE toggling)
    vietnameseMode_ = (sharedState_.ReadFlags() & SharedFlags::VIETNAMESE_MODE) != 0;
    if (langBarButton_) {
        langBarButton_->Refresh();
    }
    TSF_LOG(L"ToggleVietnameseMode: now %s", vietnameseMode_ ? L"Vietnamese" : L"English");
}

bool EngineController::InitLanguageBar(ITfThreadMgr* pThreadMgr) {
    if (!pThreadMgr) return false;

    ITfLangBarItemMgr* pLangBarItemMgr = nullptr;
    HRESULT hr = pThreadMgr->QueryInterface(
        IID_ITfLangBarItemMgr, reinterpret_cast<void**>(&pLangBarItemMgr));
    if (FAILED(hr) || !pLangBarItemMgr) {
        TSF_LOG(L"InitLanguageBar: failed to get ITfLangBarItemMgr");
        return false;
    }

    langBarButton_ = new LanguageBarButton();
    if (!langBarButton_->Initialize(this, pLangBarItemMgr)) {
        TSF_LOG(L"InitLanguageBar: button initialization failed");
        langBarButton_->Release();
        langBarButton_ = nullptr;
        pLangBarItemMgr->Release();
        return false;
    }

    pLangBarItemMgr->Release();
    TSF_LOG(L"InitLanguageBar: success");
    return true;
}

void EngineController::UninitLanguageBar() {
    if (langBarButton_) {
        langBarButton_->Uninitialize();
        langBarButton_->Release();
        langBarButton_ = nullptr;
    }
}

}  // namespace TSF
}  // namespace NextKey
