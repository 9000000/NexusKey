// NexusKey - Engine Controller Header
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "stdafx.h"
#include "core/engine/TelexEngineAdapter.h"
#include "CompositionManager.h"
#include "EditSession.h"
#include <memory>

namespace NextKey {
namespace TSF {

/// Controller bridging TSF events and the Telex engine
class EngineController {
public:
    EngineController();
    ~EngineController();

    /// Set the TSF client ID for edit sessions
    void SetClientId(TfClientId clientId) { clientId_ = clientId; }

    /// Set the category manager for display attributes
    void SetCategoryMgr(ITfCategoryMgr* pCategoryMgr) { compositionMgr_.SetCategoryMgr(pCategoryMgr); }

    /// Check if we want to handle this key
    bool WantKey(UINT vkCode, bool isKeyDown);

    /// Handle a key press
    bool HandleKey(ITfContext* pContext, UINT vkCode);

    /// Process backspace
    void ProcessBackspace(ITfContext* pContext);

    /// Commit current composition
    void Commit(ITfContext* pContext);

    /// Commit with trailing character (e.g., space)
    void CommitWithChar(ITfContext* pContext, wchar_t appendChar);

    /// Reset engine state
    void Reset();

    /// Check if there's pending composition (for Ctrl shortcuts)
    bool HasComposition() const { return engine_->HasComposition(); }

    /// Check if TSF composition is active
    bool IsComposing() const { return compositionMgr_.IsComposing(); }

    /// Check if engine has buffer (for sync check)
    bool HasEngineBuffer() const { return engine_->HasComposition(); }

    /// Reset only engine buffer (for sync recovery)
    void ResetEngine() { engine_->Reset(); }

private:
    void RequestEditSession(ITfContext* pContext, EditSession* pEditSession);

    std::unique_ptr<TelexEngineAdapter> engine_;
    CompositionManager compositionMgr_;
    TypingConfig config_;
    TfClientId clientId_ = TF_CLIENTID_NULL;
};

}  // namespace TSF
}  // namespace NextKey
