// src/app/output/SplitDispatchInjector.h
//
// Split SendInput with Sleep between BS batch and char batch. Covers
// Electron (Discord / Slack / VSCode, sleepMs=6) and Console (CMD /
// PowerShell, sleepMs=5) — ~10% of host classes.
//
// Why merge Electron + Console: only difference is 1 ms in baseMs. Two
// impls 99% identical = duplication; one impl with constructor
// sleepMs_ parameterizes cleanly.
//
// Spec: docs/plans/sprint-2-output-injector.md §2.4
#pragma once

#include "IOutputInjector.h"

namespace NextKey::Output {

class SplitDispatchInjector final : public IOutputInjector {
public:
    explicit SplitDispatchInjector(int sleepMsBetweenBatches,
                                   bool needsBaitCharPrefix = false,
                                   bool hasMultiProcessRenderer = false,
                                   bool requiresSyntheticAlphaLockstep = false) noexcept
        : sleepMs_(sleepMsBetweenBatches),
          needsBaitCharPrefix_(needsBaitCharPrefix),
          hasMultiProcessRenderer_(hasMultiProcessRenderer),
          requiresSyntheticAlphaLockstep_(
              hasMultiProcessRenderer || requiresSyntheticAlphaLockstep) {}

    bool Replace(std::size_t bsCount, std::wstring_view text,
                 unsigned short reinjectVk = 0) noexcept override;
    void SendKey(unsigned short vkCode) noexcept override;

    // Covers split Sleep (5-6 ms) + Electron / Qt event-loop (~30-90 ms).
    // Empirical from current HookEngine kSynthSettleMs hardcode.
    std::chrono::milliseconds SettleBudget() const noexcept override {
        return std::chrono::milliseconds{100};
    }

    // Channel traits are dispatch-arm hints set by the factory based on
    // WindowClassification. Electron is both multi-process and lockstep;
    // console is single-process but still lockstep because ConPTY/TUI input
    // can reorder physical WM_KEYDOWN against synthetic VK_PACKET.
    bool NeedsBaitCharPrefix() const noexcept override {
        return needsBaitCharPrefix_;
    }
    bool HasMultiProcessRenderer() const noexcept override {
        return hasMultiProcessRenderer_;
    }
    bool RequiresSyntheticAlphaLockstep() const noexcept override {
        return requiresSyntheticAlphaLockstep_;
    }

private:
    int sleepMs_;
    // WebView2 / Electron-on-Chromium hosts need the same U+202F
    // autocomplete-dismiss prefix as Win32 Chromium browsers — the
    // dispatch channel changes (split + Sleep) but the renderer-side
    // suggest engine is the same Chromium one and still requires the
    // bait. Without it, BS land into a still-open suggest popup and
    // get swallowed.
    bool needsBaitCharPrefix_;
    // True for Electron / Qt-on-Chromium hosts. Used only by leaked-key
    // suppression; console hosts remain false here.
    bool hasMultiProcessRenderer_;
    // Blocks physical/synthetic alpha mixing within one transformed word.
    bool requiresSyntheticAlphaLockstep_;
};

}  // namespace NextKey::Output
