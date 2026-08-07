// src/app/output/RichEditEmReplaceSelInjector.h
//
// Sent-message channel via EM_REPLACESEL for hosts that require it.
// Covers Win11 New Notepad RichEditD2DPT only — ~5% of host classes.
// (Sprint 1 D12 verdict: this host class refuses posted-message channels
// for IME-style replacement; only sent EM_REPLACESEL works correctly
// under chaos pressure.)
//
// Spec: docs/plans/sprint-2-output-injector.md §2.3
#pragma once

#include "IOutputInjector.h"

namespace NextKey::Output {

class RichEditEmReplaceSelInjector final : public IOutputInjector {
public:
    explicit RichEditEmReplaceSelInjector(bool forced = false) noexcept : forced_(forced) {}
    // reinjectVk is honoured: forwarded to the SendInput fallbacks, and on
    // the EM_REPLACESEL path compensated as one fewer backspace (no key
    // stream exists there). The editMsgPath gate does NOT guarantee it is
    // zero — see the impl.
    bool Replace(std::size_t bsCount, std::wstring_view text,
                 unsigned short reinjectVk = 0) noexcept override;
    void SendKey(unsigned short vkCode) noexcept override;

    // SendMessage is synchronous — by the time it returns, the edit is
    // applied. No settle gap needed for synth-guard.
    std::chrono::milliseconds SettleBudget() const noexcept override {
        return std::chrono::milliseconds{0};
    }

    bool IsMessageBasedReplace() const noexcept override {
        return true;
    }

    bool IsForced() const noexcept override {
        return forced_;
    }

private:
    bool forced_{false};
};

}  // namespace NextKey::Output
