// VKey - Composition Manager Implementation
// SPDX-License-Identifier: AGPL-3.0-only

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
    const bool terminatedCurrent = pComposition == pComposition_;
    if (terminatedCurrent) {
        ITfComposition* ownedComposition = pComposition_;
        ITfContext* ownedContext = pContext_;
        pComposition_ = nullptr;
        pContext_ = nullptr;
        currentText_.clear();
        ownedComposition->Release();
        if (ownedContext != nullptr) ownedContext->Release();
    }

    if (terminatedCurrent && pEngineController_) {
        pEngineController_->Reset();
    }
    return S_OK;
}

bool CompositionManager::StartComposition(ITfContext* pContext, TfEditCookie ec,
                                          const std::wstring& initialText) {
    if (pContext == nullptr || initialText.empty()) {
        TSF_LOG(L"StartComposition: ERROR - null context or empty text");
        return false;
    }

    if (pComposition_) {
        TSF_LOG(L"StartComposition: Ending existing composition first");
        EndComposition(ec);
    }

    CComPtr<ITfInsertAtSelection> pInsertAtSelection;
    HRESULT hr = pContext->QueryInterface(IID_ITfInsertAtSelection,
                                          reinterpret_cast<void**>(&pInsertAtSelection));
    if (FAILED(hr) || !pInsertAtSelection) {
        TSF_LOG(L"StartComposition: Failed ITfInsertAtSelection, hr=0x%08X", hr);
        return false;
    }

    CComPtr<ITfRange> pRange;
    hr = pInsertAtSelection->InsertTextAtSelection(
        ec, TF_IAS_NO_DEFAULT_COMPOSITION, initialText.c_str(),
        static_cast<LONG>(initialText.size()), &pRange);
    if (FAILED(hr) || !pRange) {
        TSF_LOG(L"StartComposition: Failed InsertTextAtSelection, hr=0x%08X", hr);
        return false;
    }

    if (!BeginCompositionOnRange(pContext, ec, pRange, initialText)) {
        const HRESULT rollbackHr = pRange->SetText(ec, 0, L"", 0);
        TSF_LOG(L"StartComposition: composition rejected; rollback hr=0x%08X",
                rollbackHr);
        return false;
    }

    MoveCaretToEnd(ec);
    TSF_LOG(L"StartComposition: SUCCESS");
    return true;
}

bool CompositionManager::StartCompositionOnRange(
    ITfContext* pContext, TfEditCookie ec, ITfRange* pRange,
    const std::wstring& existingText) {
    if (pContext == nullptr || pRange == nullptr || existingText.empty()) {
        return false;
    }
    if (pComposition_) EndComposition(ec);
    return BeginCompositionOnRange(pContext, ec, pRange, existingText);
}

bool CompositionManager::BeginCompositionOnRange(
    ITfContext* pContext, TfEditCookie ec, ITfRange* pRange,
    const std::wstring& currentText) {
    CComPtr<ITfContextComposition> pContextComposition;
    HRESULT hr = pContext->QueryInterface(
        IID_ITfContextComposition, reinterpret_cast<void**>(&pContextComposition));
    if (FAILED(hr) || !pContextComposition) {
        TSF_LOG(L"BeginCompositionOnRange: Failed ITfContextComposition, hr=0x%08X", hr);
        return false;
    }

    ITfComposition* pComposition = nullptr;
    hr = pContextComposition->StartComposition(ec, pRange, this, &pComposition);
    if (FAILED(hr) || pComposition == nullptr) {
        TSF_LOG(L"BeginCompositionOnRange: StartComposition FAILED, hr=0x%08X", hr);
        return false;
    }

    pComposition_ = pComposition;
    pContext_ = pContext;
    pContext_->AddRef();
    currentText_ = currentText;
    return true;
}

bool CompositionManager::SetCompositionText(
    TfEditCookie ec, const std::wstring& text) {
    if (pComposition_ == nullptr) {
        TSF_LOG(L"SetCompositionText: ERROR - No active composition");
        return false;
    }

    CComPtr<ITfRange> pRange;
    HRESULT hr = pComposition_->GetRange(&pRange);
    if (FAILED(hr) || !pRange) {
        TSF_LOG(L"SetCompositionText: Failed to get range, hr=0x%08X", hr);
        return false;
    }

    hr = pRange->SetText(ec, 0, text.c_str(), static_cast<LONG>(text.length()));
    if (FAILED(hr)) {
        TSF_LOG(L"SetCompositionText: SetText FAILED, hr=0x%08X", hr);
        return false;
    }

    currentText_ = text;
    MoveCaretToEnd(ec);

    TSF_LOG(L"SetCompositionText: text='%ls'", text.c_str());
    return true;
}

void CompositionManager::EndComposition(TfEditCookie ec) {
    if (pComposition_ == nullptr) return;

    TSF_LOG(L"EndComposition: Finalizing and releasing to app");

    CComPtr<ITfRange> pRange;
    if (SUCCEEDED(pComposition_->GetRange(&pRange)) && pRange) {
        ClearDisplayAttribute(ec, pRange);
    }

    ITfComposition* ownedComposition = pComposition_;
    ITfContext* ownedContext = pContext_;
    pComposition_ = nullptr;
    pContext_ = nullptr;
    currentText_.clear();

    const HRESULT hr = ownedComposition->EndComposition(ec);
    ownedComposition->Release();
    if (ownedContext != nullptr) ownedContext->Release();

    TSF_LOG(L"EndComposition: COMPLETED hr=0x%08X", hr);
}

void CompositionManager::TerminateComposition() {
    // This is only a safety fallback. Normal termination should use EndComposition(ec).
    if (pComposition_) {
        TSF_LOG(L"TerminateComposition: Warning - local state cleared without EndComposition call");
        pComposition_->Release();
        pComposition_ = nullptr;
    }
    if (pContext_) {
        pContext_->Release();
        pContext_ = nullptr;
    }
    currentText_.clear();
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
