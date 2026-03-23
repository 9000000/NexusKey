// NexusKey - Key Event Sink Implementation
// SPDX-License-Identifier: GPL-3.0-only

#include "stdafx.h"
#include "KeyEventSink.h"
#include "TextService.h"
#include "EngineController.h"
#include "ComUtils.h"
#include "Define.h"

namespace NextKey {
namespace TSF {

// Helper function to check if a key is punctuation/number that should trigger commit
static bool IsPunctuationKey(UINT vkCode) {
    // Number keys (0-9)
    if (vkCode >= 0x30 && vkCode <= 0x39) return true;

    // Numpad keys
    if (vkCode >= VK_NUMPAD0 && vkCode <= VK_DIVIDE) return true;

    // OEM keys (punctuation on US keyboard)
    // VK_OEM_1 (;:), VK_OEM_PLUS (=+), VK_OEM_COMMA (,<), VK_OEM_MINUS (-_)
    // VK_OEM_PERIOD (.>), VK_OEM_2 (/?), VK_OEM_3 (`~), VK_OEM_4 ([{)
    // VK_OEM_5 (\|), VK_OEM_6 (]}), VK_OEM_7 ('")
    if (vkCode >= VK_OEM_1 && vkCode <= VK_OEM_3) return true;
    if (vkCode >= VK_OEM_4 && vkCode <= VK_OEM_8) return true;
    if (vkCode == VK_OEM_PLUS || vkCode == VK_OEM_COMMA ||
        vkCode == VK_OEM_MINUS || vkCode == VK_OEM_PERIOD) return true;

    // Tab key
    if (vkCode == VK_TAB) return true;

    return false;
}

KeyEventSink::KeyEventSink(TextService* pTextService, EngineController* pEngineController)
    : pTextService_(pTextService), pEngineController_(pEngineController) {
}

KeyEventSink::~KeyEventSink() {
    Unadvise();
}

bool KeyEventSink::Advise(ITfThreadMgr* pThreadMgr) {
    if (pThreadMgr == nullptr) return false;

    HRESULT hr = pThreadMgr->QueryInterface(IID_ITfKeystrokeMgr, (void**)&pKeystrokeMgr_);
    if (FAILED(hr)) return false;

    hr = pKeystrokeMgr_->AdviseKeyEventSink(
        pTextService_->GetClientId(),
        static_cast<ITfKeyEventSink*>(this),
        TRUE  // Foreground
    );

    if (FAILED(hr)) {
        SafeRelease(pKeystrokeMgr_);
        return false;
    }

    TSF_LOG(L"KeyEventSink advised");
    return true;
}

void KeyEventSink::Unadvise() {
    if (pKeystrokeMgr_) {
        pKeystrokeMgr_->UnadviseKeyEventSink(pTextService_->GetClientId());
        SafeRelease(pKeystrokeMgr_);
    }
    TSF_LOG(L"KeyEventSink unadvised");
}

IFACEMETHODIMP KeyEventSink::QueryInterface(REFIID riid, void** ppvObj) {
    if (ppvObj == nullptr) return E_INVALIDARG;
    *ppvObj = nullptr;

    if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, IID_ITfKeyEventSink)) {
        *ppvObj = static_cast<ITfKeyEventSink*>(this);
    } else {
        return E_NOINTERFACE;
    }

    AddRef();
    return S_OK;
}

IFACEMETHODIMP_(ULONG) KeyEventSink::AddRef() {
    return InterlockedIncrement(&refCount_);
}

IFACEMETHODIMP_(ULONG) KeyEventSink::Release() {
    ULONG count = InterlockedDecrement(&refCount_);
    // Note: Do NOT delete this here. Lifetime is managed by unique_ptr in TextService.
    // COM ref counting is maintained for contract compliance only.
    return count;
}

IFACEMETHODIMP KeyEventSink::OnSetFocus(BOOL fForeground) {
    if (fForeground) {
        TSF_LOG(L"OnSetFocus: foreground");
        // Re-read SharedState on focus to pick up ENGINE_ENABLED/VIETNAMESE_MODE changes
        if (pEngineController_) {
            pEngineController_->CheckConfigEvent();
            pEngineController_->RefreshFlags();
        }
    } else {
        TSF_LOG(L"OnSetFocus: background");
    }
    return S_OK;
}

IFACEMETHODIMP KeyEventSink::OnTestKeyDown(ITfContext* pContext, WPARAM wParam, LPARAM /*lParam*/, BOOL* pfEaten) {
    if (pfEaten == nullptr) return E_INVALIDARG;

    // Check if this context blocks input (password, PIN, email fields)
    pEngineController_->CheckContextBlocked(pContext);
    if (pEngineController_->IsContextBlocked()) {
        *pfEaten = FALSE;
        return S_OK;
    }

    // Safety check: recover if composition terminated without us knowing
    bool isComposing = pEngineController_->IsComposing();
    bool hasBuffer = pEngineController_->HasEngineBuffer();

    if (!isComposing && hasBuffer) {
        pEngineController_->Reset();
    } else if (isComposing && !hasBuffer) {
        pEngineController_->Commit(pContext);
    }

    // Check modifiers
    bool ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
    bool alt = (GetKeyState(VK_MENU) & 0x8000) != 0;
    bool win = (GetKeyState(VK_LWIN) & 0x8000) != 0 || (GetKeyState(VK_RWIN) & 0x8000) != 0;

    // Modifiers -> commit and pass through
    if (ctrl || alt || win) {
        if (pEngineController_->HasEngineBuffer()) {
            pEngineController_->Commit(pContext);
        }
        *pfEaten = FALSE;
        return S_OK;
    }

    // Enter -> commit composition (eat Enter if we had composition)
    if (wParam == VK_RETURN) {
        if (pEngineController_->HasEngineBuffer()) {
            pEngineController_->Commit(pContext);
            *pfEaten = TRUE;  // Eat Enter - only commit, don't pass through
            return S_OK;
        }
        *pfEaten = FALSE;  // No composition, pass Enter through normally
        return S_OK;
    }

    // Check if this is a punctuation/number key that should trigger commit
    // These keys should commit composition and pass through to the app
    bool isPunctuation = IsPunctuationKey(static_cast<UINT>(wParam));
    if (isPunctuation && pEngineController_->HasEngineBuffer()) {
        pEngineController_->Commit(pContext);
        *pfEaten = FALSE;  // Let punctuation pass through
        return S_OK;
    }

    bool wantKey = pEngineController_->WantKey(static_cast<UINT>(wParam), true);

    // Cache result so OnKeyDown can reuse without calling WantKey again
    lastTestedVk_ = static_cast<UINT>(wParam);
    lastWantKeyResult_ = wantKey;

    // Non-handled keys -> commit and pass through
    if (!wantKey && pEngineController_->HasEngineBuffer()) {
        pEngineController_->Commit(pContext);
    }

    *pfEaten = wantKey ? TRUE : FALSE;
    return S_OK;
}

IFACEMETHODIMP KeyEventSink::OnTestKeyUp(ITfContext* /*pContext*/, WPARAM wParam, LPARAM /*lParam*/, BOOL* pfEaten) {
    if (pfEaten == nullptr) return E_INVALIDARG;
    // Eat keyup for A-Z and Backspace during active composition
    // (prevents apps from seeing keyup without corresponding keydown)
    // Do NOT call WantKey() here — it has side effects (auto-cap state machine)
    if (pEngineController_->IsComposing()) {
        UINT vk = static_cast<UINT>(wParam);
        *pfEaten = (vk >= 0x41 && vk <= 0x5A) || vk == VK_BACK ? TRUE : FALSE;
    } else {
        *pfEaten = FALSE;
    }
    return S_OK;
}

IFACEMETHODIMP KeyEventSink::OnKeyDown(ITfContext* pContext, WPARAM wParam, LPARAM /*lParam*/, BOOL* pfEaten) {
    if (pfEaten == nullptr) return E_INVALIDARG;
    
    // Check modifiers first (Ctrl, Alt, Win should never be handled)
    bool ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
    bool alt = (GetKeyState(VK_MENU) & 0x8000) != 0;
    bool win = (GetKeyState(VK_LWIN) & 0x8000) != 0 || (GetKeyState(VK_RWIN) & 0x8000) != 0;
    
    if (ctrl || alt || win) {
        *pfEaten = FALSE;
        return S_OK;
    }
    
    // Use cached WantKey result from OnTestKeyDown to avoid double state-machine advance
    UINT vk = static_cast<UINT>(wParam);
    bool wantKey = (vk == lastTestedVk_) ? lastWantKeyResult_
                                          : pEngineController_->WantKey(vk, true);
    lastTestedVk_ = 0;  // Invalidate cache

    if (!wantKey) {
        *pfEaten = FALSE;
        return S_OK;
    }
    
    *pfEaten = pEngineController_->HandleKey(pContext, static_cast<UINT>(wParam)) ? TRUE : FALSE;
    return S_OK;
}

IFACEMETHODIMP KeyEventSink::OnKeyUp(ITfContext* /*pContext*/, WPARAM wParam, LPARAM /*lParam*/, BOOL* pfEaten) {
    if (pfEaten == nullptr) return E_INVALIDARG;
    if (pEngineController_->IsComposing()) {
        UINT vk = static_cast<UINT>(wParam);
        *pfEaten = (vk >= 0x41 && vk <= 0x5A) || vk == VK_BACK ? TRUE : FALSE;
    } else {
        *pfEaten = FALSE;
    }
    return S_OK;
}

IFACEMETHODIMP KeyEventSink::OnPreservedKey(ITfContext* /*pContext*/, REFGUID /*rguid*/, BOOL* pfEaten) {
    if (pfEaten == nullptr) return E_INVALIDARG;
    *pfEaten = FALSE;
    return S_OK;
}

}  // namespace TSF
}  // namespace NextKey
