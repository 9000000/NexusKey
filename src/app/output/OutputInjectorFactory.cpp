// src/app/output/OutputInjectorFactory.cpp
//
// Phase 2 of the two-phase focus pipeline. Phase 1 lives in
// `HookEngine::ClassifyFocusedWindow` (HookEngine.cpp:3175) — see the
// header for why classification stayed in HookEngine post Phase 2b.
//
// Create() dispatches all four channel branches:
//   RichEditD2DPT → RichEditEmReplaceSelInjector
//   Electron      → SplitDispatchInjector(6 ms, multi-process renderer)
//   Console       → SplitDispatchInjector(5 ms, single-process)
//   default       → Win32SendInputInjector(isChromium)
//   useClipboard  → ClipboardInjector (highest priority, user opt-in)
#include "OutputInjectorFactory.h"

#include "Win32SendInputInjector.h"
#include "RichEditEmReplaceSelInjector.h"
#include "SplitDispatchInjector.h"
#include "ClipboardInjector.h"

namespace NextKey::Output {

namespace {
// Tuned to match the previous HookEngine constants:
//   Electron: 6 ms (Discord/Slack/VSCode renderer drain).
//   Console : 5 ms (CMD/PowerShell readline ingest).
constexpr int kElectronSleepMs = 6;
constexpr int kConsoleSleepMs  = 5;
}  // namespace

std::shared_ptr<IOutputInjector> Create(
        const WindowClassification& c) noexcept {
    // Priority order:
    //   if c.useClipboard    → ClipboardInjector              [Sprint 2 follow-up]
    //   if c.isRichEditD2DPT → RichEditEmReplaceSelInjector  [D2]
    //   if c.isElectron      → SplitDispatchInjector(6)       [D3]
    //   if c.isConsole       → SplitDispatchInjector(5)       [D3]
    //   default              → Win32SendInputInjector(c.isChromium)
    
    if (c.useClipboard) {
        return std::make_shared<ClipboardInjector>();
    }
    if (c.isRichEditD2DPT) {
        return std::make_shared<RichEditEmReplaceSelInjector>();
    }
    if (c.isElectron) {
        // Electron-on-Chromium hosts (WebView2 / Tauri / Dorion) need the
        // bait prefix even though they go through the split channel.
        // hasMultiProcessRenderer=true blocks mid-word passthrough in
        // HookEngine — Electron's multi-process renderer reorders
        // physical WM_KEYDOWN against synthetic VK_PACKET.
        return std::make_shared<SplitDispatchInjector>(
            kElectronSleepMs, c.isChromium, /*hasMultiProcessRenderer=*/true);
    }
    if (c.isConsole) {
        // Console hosts (CMD/PowerShell) never use Chromium suggest and
        // are single-process renderers, so both traits are false here.
        return std::make_shared<SplitDispatchInjector>(
            kConsoleSleepMs, /*needsBaitCharPrefix=*/false,
            /*hasMultiProcessRenderer=*/false);
    }
    return std::make_shared<Win32SendInputInjector>(c.isChromium);
}

}  // namespace NextKey::Output
