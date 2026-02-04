// NexusKey - Engine Controller Implementation
// SPDX-License-Identifier: GPL-3.0-only

#include "stdafx.h"
#include "EngineController.h"
#include "CompositionEditSession.h"
#include "Define.h"

namespace NextKey {
namespace TSF {

EngineController::EngineController() {
    // Use compiled defaults (FR8 - engine autonomy)
    config_.inputMethod = InputMethod::Telex;
    config_.spellCheckEnabled = false;
    config_.optimizeLevel = 0;
    
    engine_ = std::make_unique<TelexEngineAdapter>(config_);
    compositionMgr_.SetEngineController(this);
    TSF_LOG(L"EngineController initialized");
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

    bool engineHasComp = engine_->HasComposition();

    // 2. We want A-Z keys for Telex processing
    if (vkCode >= 0x41 && vkCode <= 0x5A) {
        return true;
    }

    // 3. We handle Space and Backspace ONLY if we have internal content.
    if (vkCode == VK_SPACE || vkCode == VK_BACK) {
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

    // 2. Handle Space (only if we have content, as decided by WantKey)
    if (vkCode == VK_SPACE) {
        CommitWithChar(pContext, L' ');
        return true;
    }
    
    // 3. Check if character key (A-Z)
    if (vkCode >= 0x41 && vkCode <= 0x5A) {
        bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;

        TSF_LOG(L"HandleKey: A-Z key detected");

        if (engine_->ProcessKeyDown(vkCode, shift)) {
            std::wstring composition = engine_->GetComposition();
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
        } else {
            TSF_LOG(L"HandleKey: ProcessKeyDown returned false");
        }
    }

    return false;
}

void EngineController::ProcessBackspace(ITfContext* pContext) {
    engine_->ProcessBackspace();

    if (engine_->HasComposition()) {
        std::wstring composition = engine_->GetComposition();
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

}  // namespace TSF
}  // namespace NextKey
