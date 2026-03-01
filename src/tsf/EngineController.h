// NexusKey - Engine Controller Header
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "stdafx.h"
#include "core/engine/IInputEngine.h"
#include "core/config/TypingConfig.h"
#include "core/config/ConfigEvent.h"
#include "core/ipc/SharedStateManager.h"
#include "CompositionManager.h"
#include "EditSession.h"
#include "LanguageBarButton.h"
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
    bool HasComposition() const { return engine_->Count() > 0; }

    /// Check if TSF composition is active
    bool IsComposing() const { return compositionMgr_.IsComposing(); }

    /// Check if engine has buffer (for sync check)
    bool HasEngineBuffer() const { return engine_->Count() > 0; }

    /// Reset only engine buffer (for sync recovery)
    void ResetEngine() { engine_->Reset(); }

    /// Switch input method at word boundary
    void SwitchInputMethod(InputMethod method);

    /// Check for config changes (call periodically, e.g., on focus)
    /// Returns true if config was reloaded
    bool CheckConfigEvent();

    /// Check if engine is enabled (app is running)
    [[nodiscard]] bool IsEnabled() const noexcept { return engineEnabled_; }

    /// Check if Vietnamese mode is active
    [[nodiscard]] bool IsVietnameseMode() const noexcept { return vietnameseMode_; }

    /// Toggle Vietnamese/English mode (atomic flag + icon refresh)
    void ToggleVietnameseMode();

    /// Initialize language bar button (call after SetClientId/SetCategoryMgr)
    bool InitLanguageBar(ITfThreadMgr* pThreadMgr);

    /// Cleanup language bar button
    void UninitLanguageBar();

    /// Re-read flags from SharedState (call on focus)
    void RefreshFlags();

private:
    void RequestEditSession(ITfContext* pContext, EditSession* pEditSession);

    /// Check if current app is Scintilla-based (Notepad++, etc.)
    bool IsScintillaApp() const;

    /// Apply config from SharedState
    void ApplySharedState(const SharedState& state);

    std::unique_ptr<IInputEngine> engine_;
    CompositionManager compositionMgr_;
    TypingConfig config_;
    InputMethod currentMethod_ = InputMethod::Telex;
    TfClientId clientId_ = TF_CLIENTID_NULL;
    ConfigEvent configEvent_;       // For detecting config changes
    SharedStateManager sharedState_; // For reading config from App
    uint32_t lastEpoch_ = 0;        // Last seen config epoch
    bool engineEnabled_ = true;     // ENGINE_ENABLED flag from SharedState
    bool vietnameseMode_ = true;    // VIETNAMESE_MODE flag from SharedState
    uint8_t autoCapState_ = 0;      // 0=idle, 1=after-punct, 2=capitalize-next
    LanguageBarButton* langBarButton_ = nullptr;  // Owned, Release'd in UninitLanguageBar
};

}  // namespace TSF
}  // namespace NextKey
