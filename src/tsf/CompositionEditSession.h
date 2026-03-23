// NexusKey - Composition Edit Sessions
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "EditSession.h"
#include "CompositionManager.h"
#include "Define.h"
#include <string>

namespace NextKey {
namespace TSF {

/// Edit session to start a new composition
class StartCompositionEditSession : public EditSession {
public:
    StartCompositionEditSession(ITfContext* pContext, CompositionManager* pMgr, const std::wstring& text)
        : EditSession(pContext), pMgr_(pMgr), text_(text) {}

    IFACEMETHODIMP DoEditSession(TfEditCookie ec) override {
        if (pMgr_ == nullptr || pContext_ == nullptr) return E_FAIL;

        ITfComposition* pComposition = nullptr;
        if (!pMgr_->StartComposition(pContext_, ec, &pComposition)) {
            TSF_LOG(L"StartCompositionEditSession: Failed to start composition");
            return E_FAIL;
        }

        if (!text_.empty()) {
            pMgr_->SetCompositionText(ec, text_);
        }

        TSF_LOG(L"StartCompositionEditSession: Success");
        return S_OK;
    }

private:
    CompositionManager* pMgr_;
    std::wstring text_;
};

/// Edit session to update composition text
class UpdateCompositionEditSession : public EditSession {
public:
    UpdateCompositionEditSession(ITfContext* pContext, CompositionManager* pMgr, const std::wstring& text)
        : EditSession(pContext), pMgr_(pMgr), text_(text) {}

    IFACEMETHODIMP DoEditSession(TfEditCookie ec) override {
        if (pMgr_ == nullptr) return E_FAIL;

        if (!pMgr_->IsComposing()) {
            TSF_LOG(L"UpdateCompositionEditSession: Not composing, starting new");
            ITfComposition* pComposition = nullptr;
            if (!pMgr_->StartComposition(pContext_, ec, &pComposition)) {
                return E_FAIL;
            }
        }

        // Replace composition text
        pMgr_->SetCompositionText(ec, text_);

        TSF_LOG(L"UpdateCompositionEditSession: Success");
        return S_OK;
    }

private:
    CompositionManager* pMgr_;
    std::wstring text_;
};

/// Edit session to end composition
class EndCompositionEditSession : public EditSession {
public:
    EndCompositionEditSession(ITfContext* pContext, CompositionManager* pMgr)
        : EditSession(pContext), pMgr_(pMgr) {}

    IFACEMETHODIMP DoEditSession(TfEditCookie ec) override {
        if (pMgr_ == nullptr) return E_FAIL;

        pMgr_->EndComposition(ec);
        TSF_LOG(L"EndCompositionEditSession: Success");
        return S_OK;
    }

private:
    CompositionManager* pMgr_;
};

/// Edit session to commit with final text (update + end in one operation)
class CommitEditSession : public EditSession {
public:
    CommitEditSession(ITfContext* pContext, CompositionManager* pMgr, const std::wstring& finalText)
        : EditSession(pContext), pMgr_(pMgr), finalText_(finalText) {}

    IFACEMETHODIMP DoEditSession(TfEditCookie ec) override {
        if (pMgr_ == nullptr) return E_FAIL;

        if (pMgr_->IsComposing()) {
            pMgr_->SetCompositionText(ec, finalText_);
            pMgr_->EndComposition(ec);
        } else {
            TSF_LOG(L"CommitEditSession: composition externally terminated, text dropped: %s",
                    finalText_.c_str());
        }

        return S_OK;
    }

private:
    CompositionManager* pMgr_;
    std::wstring finalText_;
};

}  // namespace TSF
}  // namespace NextKey
