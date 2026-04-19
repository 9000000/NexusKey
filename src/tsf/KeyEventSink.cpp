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

// Convert VK code + lParam to the Unicode character the active layout would produce.
// Uses ToUnicode so it respects US QWERTY, shift state, etc. Returns 0 if not printable.
static wchar_t VkToChar(UINT vk, LPARAM lParam) {
    BYTE keyState[256] = {};
    if (!GetKeyboardState(keyState)) return 0;
    UINT scanCode = (static_cast<UINT>(lParam) >> 16) & 0xFF;
    wchar_t buf[4] = {};
    int result = ToUnicode(vk, scanCode, keyState, buf, 4, 0);
    return (result == 1 && buf[0] != 0) ? buf[0] : 0;
}

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

IFACEMETHODIMP KeyEventSink::OnTestKeyDown(ITfContext* pContext, WPARAM wParam, LPARAM lParam, BOOL* pfEaten) {
    if (pfEaten == nullptr) return E_INVALIDARG;

    // Defensive: drop any punct char cached by a previous OnTestKeyDown whose
    // OnKeyDown pair never fired (rare TSF anomaly). Fresh keystroke = fresh cache.
    lastPunctChar_ = 0;

    // Check if this context blocks input (password, PIN, email fields)
    pEngineController_->CheckContextBlocked(pContext);
    if (pEngineController_->IsContextBlocked()) {
        *pfEaten = FALSE;
        return S_OK;
    }

    // Safety check: recover from engine/composition desync.
    //
    // State A: engine has buffer but TSF composition was externally ended.
    //          TSF side is already gone — Reset() just drops our orphan
    //          composition pointer. No doc mutation.
    // State B: TSF composition active but our engine is empty. TSF host still
    //          has visible pending composition text. We must properly end the
    //          TSF composition (SetCompositionText + EndComposition via edit
    //          session) or the stale composition lingers until the host ends
    //          it. This does mutate the doc in the test phase — accepted
    //          tradeoff because TerminateComposition-only leaks the composition.
    bool isComposing = pEngineController_->IsComposing();
    bool hasBuffer = pEngineController_->HasEngineBuffer();
    if (!isComposing && hasBuffer) {
        TSF_LOG(L"OnTestKeyDown: desync A (composition gone, buffer=%d) → Reset",
                static_cast<int>(pEngineController_->HasEngineBuffer()));
        pEngineController_->Reset();
    } else if (isComposing && !hasBuffer) {
        TSF_LOG(L"OnTestKeyDown: desync B (composition live, buffer empty) → Commit");
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

    // Enter -> commit composition. Eat Enter if we have buffer so the app
    // doesn't also insert a newline after the commit. Defer the actual commit
    // (doc mutation) to OnKeyDown — spec forbids mutation in the test phase.
    if (wParam == VK_RETURN) {
        if (pEngineController_->HasEngineBuffer()) {
            *pfEaten = TRUE;
            lastTestedVk_ = static_cast<UINT>(wParam);
            lastWantKeyResult_ = false;  // marker: eaten-for-deferred-commit
            lastPunctChar_ = 0;
            return S_OK;
        }
        *pfEaten = FALSE;  // No composition, pass Enter through normally
        return S_OK;
    }

    // Punctuation + active composition → eat the key; OnKeyDown will commit with
    // this char appended (atomic "abc," — no race between EndComposition and the
    // host's default key handling).
    //
    // We only eat when VkToChar succeeds (can reproduce the printable char). For
    // the rare case where it fails (dead key, non-printable punct mapping), fall
    // through without eating — TSF spec forbids mutating the document in the test
    // phase, so no commit here. Composition will resolve via normal flow in
    // OnKeyDown, which may not fire for an un-eaten key; worst case the user sees
    // a momentarily stale composition with an uneaten punct arriving at caret.
    // Acceptable for an edge case essentially never hit on US QWERTY.
    bool isPunctuation = IsPunctuationKey(static_cast<UINT>(wParam));
    if (isPunctuation && pEngineController_->HasEngineBuffer()) {
        bool isVniDigit = pEngineController_->IsVniDigitKey(static_cast<UINT>(wParam));
        if (!isVniDigit) {
            wchar_t ch = VkToChar(static_cast<UINT>(wParam), lParam);
            if (ch != 0) {
                *pfEaten = TRUE;
                lastTestedVk_ = static_cast<UINT>(wParam);
                lastWantKeyResult_ = true;
                lastPunctChar_ = ch;  // OnKeyDown reads this — no second ToUnicode call.
                return S_OK;
            }
            TSF_LOG(L"OnTestKeyDown: VkToChar failed vk=0x%02X, passthrough (no eat)",
                    (UINT)wParam);
            // Fall through to WantKey / normal flow — no doc mutation in test phase.
        }
    }

    bool wantKey = pEngineController_->WantKey(static_cast<UINT>(wParam), true);

    // Backspace revive: if engine is empty and cursor is right after a Vietnamese
    // word, claim the BS and re-enter composition in HandleKey. Pre-read the word
    // here (sync edit session) so we can decide whether to eat the key.
    if (!wantKey && wParam == VK_BACK && !pEngineController_->HasEngineBuffer()) {
        if (pEngineController_->PrepareBackspaceRevive(pContext)) {
            wantKey = true;
        }
    }

    // Cache result so OnKeyDown can reuse without calling WantKey again
    lastTestedVk_ = static_cast<UINT>(wParam);
    lastWantKeyResult_ = wantKey;

    // Non-handled keys with buffer → eat + defer commit to OnKeyDown (no doc
    // mutation in test phase). OnKeyDown will commit and drop the key.
    // Tradeoff: non-text keys (F1, arrows, Escape) pressed mid-composition
    // are consumed by the commit — user presses them again after commit to
    // navigate/activate. This matches ab4497b's principle and avoids the
    // "Chrome cursor race" that appears as extra whitespace between commit
    // text and the passthrough key.
    if (!wantKey && pEngineController_->HasEngineBuffer()) {
        *pfEaten = TRUE;
        lastPunctChar_ = 0;  // not a punct-with-char path; just commit-and-drop
        return S_OK;
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

    bool ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
    bool alt = (GetKeyState(VK_MENU) & 0x8000) != 0;
    bool win = (GetKeyState(VK_LWIN) & 0x8000) != 0 || (GetKeyState(VK_RWIN) & 0x8000) != 0;

    if (ctrl || alt || win) {
        *pfEaten = FALSE;
        return S_OK;
    }

    UINT vk = static_cast<UINT>(wParam);

    // Punctuation: commit composition with this char appended (atomic, no race).
    // Use the char cached by OnTestKeyDown — avoids a second ToUnicode call that
    // could mutate kernel dead-key state on some layouts.
    if (IsPunctuationKey(vk) && pEngineController_->HasEngineBuffer()
        && !pEngineController_->IsVniDigitKey(vk)
        && vk == lastTestedVk_ && lastPunctChar_ != 0) {
        pEngineController_->CommitWithChar(pContext, lastPunctChar_);
        lastTestedVk_ = 0;
        lastPunctChar_ = 0;
        *pfEaten = TRUE;
        return S_OK;
    }

    // Deferred commit from OnTestKeyDown: we ate the key in the test phase
    // because we had a pending buffer but the key wasn't one we wanted (Enter,
    // arrow, F-key, etc. with composition active). Commit now (mutation is
    // legal in OnKeyDown), then drop the key — user re-presses if they need it.
    if (vk == lastTestedVk_ && !lastWantKeyResult_ && lastPunctChar_ == 0
        && pEngineController_->HasEngineBuffer()) {
        pEngineController_->Commit(pContext);
        lastTestedVk_ = 0;
        *pfEaten = TRUE;
        return S_OK;
    }

    bool wantKey = (vk == lastTestedVk_) ? lastWantKeyResult_
                                          : pEngineController_->WantKey(vk, true);
    lastTestedVk_ = 0;  // Invalidate cache
    lastPunctChar_ = 0;

    if (!wantKey) {
        *pfEaten = FALSE;
        return S_OK;
    }

    *pfEaten = pEngineController_->HandleKey(pContext, vk) ? TRUE : FALSE;
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
