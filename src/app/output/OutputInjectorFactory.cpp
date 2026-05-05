// src/app/output/OutputInjectorFactory.cpp
//
// D3: Create() now dispatches all four classification branches —
// RichEditD2DPT → RichEditEmReplaceSelInjector, Electron → Split(6),
// Console → Split(5), default → Win32(isChromium).
//
// ClassifyWindow remains a stub. HookEngine still owns the
// classification logic in OnFocusChanged because the same locals feed
// non-dispatch concerns (passthrough policy, retry-loop gating). D4
// will lift the logic up here once those atomic flags are removed.
#include "OutputInjectorFactory.h"

#include "Win32SendInputInjector.h"
#include "RichEditEmReplaceSelInjector.h"
#include "SplitDispatchInjector.h"

namespace NextKey::Output {

namespace {
// Tuned to match the previous HookEngine constants:
//   Electron: 6 ms (Discord/Slack/VSCode renderer drain).
//   Console : 5 ms (CMD/PowerShell readline ingest).
constexpr int kElectronSleepMs = 6;
constexpr int kConsoleSleepMs  = 5;
}  // namespace

WindowClassification ClassifyWindow(HWND /*hwnd*/) noexcept {
    // D3 stub. D4 ports the full classification logic from HookEngine.
    return WindowClassification{};
}

std::shared_ptr<IOutputInjector> Create(
        const WindowClassification& c) noexcept {
    // Priority order:
    //   if c.isRichEditD2DPT  → RichEditEmReplaceSelInjector  [D2]
    //   if c.isElectron       → SplitDispatchInjector(6)       [D3]
    //   if c.isConsole        → SplitDispatchInjector(5)       [D3]
    //   default               → Win32SendInputInjector(c.isChromium)
    if (c.isRichEditD2DPT) {
        return std::make_shared<RichEditEmReplaceSelInjector>();
    }
    if (c.isElectron) {
        // Electron-on-Chromium hosts (WebView2 / Tauri / Dorion) need the
        // bait prefix even though they go through the split channel.
        return std::make_shared<SplitDispatchInjector>(
            kElectronSleepMs, c.isChromium);
    }
    if (c.isConsole) {
        // Console hosts (CMD/PowerShell) never use Chromium suggest, so
        // bait is unconditionally off here.
        return std::make_shared<SplitDispatchInjector>(kConsoleSleepMs);
    }
    return std::make_shared<Win32SendInputInjector>(c.isChromium);
}

}  // namespace NextKey::Output
