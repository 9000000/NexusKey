// src/app/output/IOutputInjector.h
//
// Output Injection Strategy interface (CODE_GOVERNANCE §2).
// Implementations encapsulate the OS-side mechanism (batch SendInput,
// EM_REPLACESEL, split-dispatch with Sleep). HookEngine reads via
// std::atomic_load(shared_ptr) on the hook thread (RCU pattern, same
// as Sprint 1 D6 config_).
//
// Hot-path overhead: 1 atomic_load (~10 ns) + 1 virtual call (~1 ns
// when devirtualized via `final`) + mechanism. Total ~11 ns; well
// below the 1 ms hook budget (Rule #11.1).
//
// Spec: docs/plans/sprint-2-output-injector.md §2.1
#pragma once

#include <chrono>
#include <cstddef>
#include <string_view>

namespace NextKey::Output {

class IOutputInjector {
public:
    virtual ~IOutputInjector() = default;

    // Replace caret region: delete bsCount chars, then insert text.
    // Returns true if delivered; false if caller should fall back to
    // passthrough (today's TryEditMessagePaste failure semantics).
    [[nodiscard]] virtual bool Replace(std::size_t bsCount,
                                       std::wstring_view text) noexcept = 0;

    // Re-inject a single VK as if the user pressed it (down + up, with
    // NEXUSKEY_EXTRA_INFO marker). Used by HookEngine::InjectKey for
    // the synth-pending re-inject case (HookEngine.cpp line ~905).
    virtual void SendKey(unsigned short vkCode) noexcept = 0;

    // Time the synth pressure from this channel takes to drain. Used
    // by HookEngine's commit-undo synth-guard. Default 100 ms (paranoid
    // — matches today's hardcoded kSynthSettleMs). Each impl overrides
    // to match its actual mechanism: Win32 batch ~30 ms, RichEdit
    // sent-message 0 ms, Electron split ~100 ms.
    [[nodiscard]] virtual std::chrono::milliseconds SettleBudget() const noexcept {
        return std::chrono::milliseconds{100};
    }

    // ─── Channel traits (post-T3 follow-up) ──────────────────────────
    // Replaces HookEngine's duplicated isElectronApp_ / needBaitChar_
    // atomic flags. Source of truth lives with the injector — the
    // dispatch channel that picked the trait is also the one that knows
    // its character. HookEngine reads via std::atomic_load(&injector_)
    // → trait method (1 atomic load + 1 virtual call ~11 ns total,
    // same overhead pattern as SettleBudget()).

    // True iff this channel runs through a multi-process renderer where
    // physical WM_KEYDOWN and synthetic VK_PACKET arrive out of order
    // (Electron / Qt / WebView2). HookEngine uses this to block
    // mid-word passthrough once a synth has fired in this word —
    // without that gate the multi-process renderer reorders our
    // injected chars/BS against the user's physical keystroke and the
    // app text drifts. Default false (vanilla single-process renderer).
    [[nodiscard]] virtual bool HasMultiProcessRenderer() const noexcept {
        return false;
    }

    // True iff this channel needs a U+202F bait char prefix before
    // backspaces to dismiss Chromium-style autocomplete suggestions
    // (Chrome, Edge, WebView2, Excel, Outlook). Without it, BS land
    // into the still-open suggest popup and get swallowed. HookEngine
    // uses this to skip the game-compat reinjectVk path (the bait
    // already keeps the renderer's suggest dismissed; an extra physical
    // VK would race with the bait char). Default false.
    [[nodiscard]] virtual bool NeedsBaitCharPrefix() const noexcept {
        return false;
    }
};

}  // namespace NextKey::Output
