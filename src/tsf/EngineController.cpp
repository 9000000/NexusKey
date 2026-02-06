// NexusKey - Engine Controller Implementation
// SPDX-License-Identifier: GPL-3.0-only

#include "stdafx.h"
#include "EngineController.h"
#include "CompositionEditSession.h"
#include "Define.h"
#include "core/engine/EngineFactory.h"

namespace NextKey {
namespace TSF {

EngineController::EngineController() {
    // Use compiled defaults (FR8 - engine autonomy)
    config_.inputMethod = InputMethod::Telex;
    config_.spellCheckEnabled = false;
    config_.optimizeLevel = 0;
    currentMethod_ = InputMethod::Telex;
    
    engine_ = EngineFactory::Create(config_);
    compositionMgr_.SetEngineController(this);
    TSF_LOG(L"EngineController initialized with Telex engine");
}

EngineController::~EngineController() {
    TSF_LOG(L"EngineController destroyed");
}

bool EngineController::WantKey(UINT vkCode, bool /*isKeyDown*/) {
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
        if (IsScintillaApp()) {
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
        wchar_t ch = static_cast<wchar_t>(vkCode);
        if (!shift) ch = towlower(ch);
        
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
}

void EngineController::SwitchInputMethod(InputMethod method) {
    if (method == currentMethod_) return;
    
    // Commit any pending composition before switching (discard text)
    if (engine_->Count() > 0) {
        (void)engine_->Commit();
    }
    
    // Create new engine
    currentMethod_ = method;
    engine_ = EngineFactory::Create(method);
    
    TSF_LOG(L"Switched to %s engine", method == InputMethod::VNI ? L"VNI" : L"Telex");
}

bool EngineController::IsScintillaApp() const {
    // Get the foreground window and check if it's a Scintilla-based app
    HWND hwnd = GetForegroundWindow();
    if (hwnd == nullptr) return false;

    // Check window class name
    wchar_t className[256] = {0};
    if (GetClassNameW(hwnd, className, 256) > 0) {
        // Notepad++ main window class
        if (wcsstr(className, L"Notepad++") != nullptr) {
            return true;
        }
    }

    // Also check child windows for Scintilla class
    HWND hwndFocus = GetFocus();
    if (hwndFocus != nullptr) {
        if (GetClassNameW(hwndFocus, className, 256) > 0) {
            // Scintilla edit control class
            if (wcsstr(className, L"Scintilla") != nullptr) {
                return true;
            }
        }
    }

    return false;
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

    // Config changed - reload
    TSF_LOG(L"Config event received, reloading config");
    
    // TODO: Load from ConfigManager when Core writes to shared location
    // For now, just log - actual config sharing requires SharedState or file polling
    
    return true;
}

}  // namespace TSF
}  // namespace NextKey
