// NexusKey - Composition Manager Implementation
// SPDX-License-Identifier: GPL-3.0-only

#include "stdafx.h"
#include "CompositionManager.h"
#include "EngineController.h"
#include "DisplayAttribute.h"
#include "Define.h"

namespace NextKey {
namespace TSF {

// IUnknown
IFACEMETHODIMP CompositionManager::QueryInterface(REFIID riid, void** ppvObj) {
    if (ppvObj == nullptr) return E_INVALIDARG;
    *ppvObj = nullptr;

    if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, IID_ITfCompositionSink)) {
        *ppvObj = static_cast<ITfCompositionSink*>(this);
        AddRef();
        return S_OK;
    }
    return E_NOINTERFACE;
}

// ITfCompositionSink - called when app or system terminates our composition
IFACEMETHODIMP CompositionManager::OnCompositionTerminated(TfEditCookie /*ec*/, ITfComposition* pComposition) {
    TSF_LOG(L"OnCompositionTerminated called by system");
    if (pComposition == pComposition_) {
        pComposition_->Release();
        pComposition_ = nullptr;
    }

    // Always reset engine to stay in sync whenever a composition ends
    if (pEngineController_) {
        pEngineController_->Reset();
    }
    return S_OK;
}

bool CompositionManager::StartComposition(ITfContext* pContext, TfEditCookie ec, ITfComposition** ppComposition) {
    if (pContext == nullptr || ppComposition == nullptr) {
        TSF_LOG(L"StartComposition: ERROR - null context or pointer");
        return false;
    }

    // End any existing composition first
    if (pComposition_) {
        TSF_LOG(L"StartComposition: Ending existing composition first");
        EndComposition(ec);
    }

    // Get context composition interface
    ITfContextComposition* pContextComposition = nullptr;
    HRESULT hr = pContext->QueryInterface(IID_ITfContextComposition, (void**)&pContextComposition);
    if (FAILED(hr) || pContextComposition == nullptr) {
        TSF_LOG(L"StartComposition: Failed ITfContextComposition, hr=0x%08X", hr);
        return false;
    }

    // Get insertion point
    ITfInsertAtSelection* pInsertAtSelection = nullptr;
    hr = pContext->QueryInterface(IID_ITfInsertAtSelection, (void**)&pInsertAtSelection);
    if (FAILED(hr) || pInsertAtSelection == nullptr) {
        pContextComposition->Release();
        TSF_LOG(L"StartComposition: Failed ITfInsertAtSelection, hr=0x%08X", hr);
        return false;
    }

    // Insert empty range at selection
    ITfRange* pRange = nullptr;
    hr = pInsertAtSelection->InsertTextAtSelection(ec, TF_IAS_QUERYONLY, nullptr, 0, &pRange);
    pInsertAtSelection->Release();

    if (FAILED(hr) || pRange == nullptr) {
        pContextComposition->Release();
        TSF_LOG(L"StartComposition: Failed InsertTextAtSelection, hr=0x%08X", hr);
        return false;
    }

    // Start composition at this range (pass this as sink to receive termination events)
    ITfComposition* pComposition = nullptr;
    hr = pContextComposition->StartComposition(ec, pRange, this, &pComposition);
    pContextComposition->Release();

    if (FAILED(hr) || pComposition == nullptr) {
        pRange->Release();
        TSF_LOG(L"StartComposition: StartComposition FAILED, hr=0x%08X", hr);
        return false;
    }

    // Store composition (range is managed by composition)
    pComposition_ = pComposition;
    pContext_ = pContext;  // Store context for cursor manipulation
    pRange->Release();  // We'll get range from composition when needed
    *ppComposition = pComposition;

    TSF_LOG(L"StartComposition: SUCCESS");
    return true;
}

bool CompositionManager::SetCompositionText(TfEditCookie ec, const std::wstring& text) {
    if (pComposition_ == nullptr) {
        TSF_LOG(L"SetCompositionText: ERROR - No active composition");
        return false;
    }

    // Get the composition's range (this is the authoritative range)
    ITfRange* pRange = nullptr;
    HRESULT hr = pComposition_->GetRange(&pRange);
    if (FAILED(hr) || pRange == nullptr) {
        TSF_LOG(L"SetCompositionText: Failed to get range, hr=0x%08X", hr);
        return false;
    }

    // Replace all text in the composition range
    hr = pRange->SetText(ec, TF_ST_CORRECTION, text.c_str(), static_cast<LONG>(text.length()));

    if (FAILED(hr)) {
        pRange->Release();
        TSF_LOG(L"SetCompositionText: SetText FAILED, hr=0x%08X", hr);
        return false;
    }

    // Apply invisible display attribute
    ApplyDisplayAttribute(ec, pRange);

    // Sync cursor
    MoveCaretToEnd(ec);

    pRange->Release();

    TSF_LOG(L"SetCompositionText: text='%ls'", text.c_str());
    return true;
}

void CompositionManager::EndComposition(TfEditCookie ec) {
    if (pComposition_ == nullptr) return;

    TSF_LOG(L"EndComposition: Finalizing and releasing to app");
    
    ITfRange* pRange = nullptr;
    if (SUCCEEDED(pComposition_->GetRange(&pRange)) && pRange) {
        // 1. Clear display attributes BEFORE ending
        ClearDisplayAttribute(ec, pRange);
        
        // 2. Set selection to end (this explicitly "hands off" the caret to the app)
        MoveCaretToEnd(ec);
        
        pRange->Release();
    }

    // 3. Inform TSF that the composition is finished
    pComposition_->EndComposition(ec);
    pComposition_->Release();
    pComposition_ = nullptr;
    pContext_ = nullptr;

    TSF_LOG(L"EndComposition: COMPLETED");
}

void CompositionManager::TerminateComposition() {
    // This is only a safety fallback. Normal termination should use EndComposition(ec).
    if (pComposition_) {
        TSF_LOG(L"TerminateComposition: Warning - local state cleared without EndComposition call");
        pComposition_->Release();
        pComposition_ = nullptr;
    }
    pContext_ = nullptr;
}

void CompositionManager::ApplyDisplayAttribute(TfEditCookie ec, ITfRange* pRange) {
    if (pRange == nullptr || pContext_ == nullptr) return;

    // Get display attribute property
    ITfProperty* pProperty = nullptr;
    if (FAILED(pContext_->GetProperty(GUID_PROP_ATTRIBUTE, &pProperty)) || pProperty == nullptr) {
        return;
    }

    // Register GUID with CategoryMgr each time (VietType pattern)
    if (pCategoryMgr_ != nullptr) {
        TfGuidAtom atom = TF_INVALID_GUIDATOM;
        HRESULT hr = pCategoryMgr_->RegisterGUID(GUID_DisplayAttribute_Input, &atom);
        if (SUCCEEDED(hr) && atom != TF_INVALID_GUIDATOM) {
            // Set the display attribute on the range
            VARIANT var;
            var.vt = VT_I4;
            var.lVal = atom;
            pProperty->SetValue(ec, pRange, &var);
            TSF_LOG(L"Applied display attribute");
        }
    }

    pProperty->Release();
}

void CompositionManager::ClearDisplayAttribute(TfEditCookie ec, ITfRange* pRange) {
    if (pRange == nullptr || pContext_ == nullptr) return;

    ITfProperty* pProperty = nullptr;
    if (SUCCEEDED(pContext_->GetProperty(GUID_PROP_ATTRIBUTE, &pProperty)) && pProperty != nullptr) {
        pProperty->Clear(ec, pRange);
        pProperty->Release();
        TSF_LOG(L"Cleared display attribute");
    }
}

void CompositionManager::MoveCaretToEnd(TfEditCookie ec) {
    if (pComposition_ == nullptr || pContext_ == nullptr) return;

    ITfRange* pRange = nullptr;
    HRESULT hr = pComposition_->GetRange(&pRange);
    if (FAILED(hr) || pRange == nullptr) return;

    // Clone and collapse to end
    ITfRange* pRangeClone = nullptr;
    hr = pRange->Clone(&pRangeClone);
    if (SUCCEEDED(hr) && pRangeClone != nullptr) {
        pRangeClone->Collapse(ec, TF_ANCHOR_END);

        // Set selection to collapsed range
        TF_SELECTION sel;
        sel.range = pRangeClone;
        sel.style.ase = TF_AE_NONE;
        sel.style.fInterimChar = FALSE;
        pContext_->SetSelection(ec, 1, &sel);

        pRangeClone->Release();
    }
    pRange->Release();
}

}  // namespace TSF
}  // namespace NextKey
