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
    explicit SplitDispatchInjector(int sleepMsBetweenBatches) noexcept
        : sleepMs_(sleepMsBetweenBatches) {}

    bool Replace(std::size_t bsCount, std::wstring_view text) noexcept override;
    void SendKey(unsigned short vkCode) noexcept override;

    // Covers split Sleep (5-6 ms) + Electron / Qt event-loop (~30-90 ms).
    // Empirical from current HookEngine kSynthSettleMs hardcode.
    std::chrono::milliseconds SettleBudget() const noexcept override {
        return std::chrono::milliseconds{100};
    }

private:
    int sleepMs_;
};

}  // namespace NextKey::Output
