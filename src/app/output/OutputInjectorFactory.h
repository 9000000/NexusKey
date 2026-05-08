// src/app/output/OutputInjectorFactory.h
//
// Two-phase focus detection (Rule #11.3):
//   Phase 1 — ClassifyWindow(HWND): no shared state writes, may take
//              1-5 ms (Win32 calls, exe path lookup, process scan).
//   Phase 2 — Create(WindowClassification): construct injector, ~1
//              heap alloc. Caller atomic_store-publishes the result.
//
// Spec: docs/plans/sprint-2-output-injector.md §2.6
#pragma once

#include "IOutputInjector.h"
#include <windows.h>
#include <memory>

namespace NextKey::Output {

// Phase 1 result — pure data, no shared writes (Rule #11.3).
struct WindowClassification {
    bool isRichEditD2DPT = false;  // Win11 New Notepad
    bool isElectron      = false;  // Discord / Slack / VSCode etc
    bool isConsole       = false;  // CMD / PowerShell
    bool isChromium      = false;  // Chrome / Edge — bait-char hint
    bool useClipboard    = false;  // User configured clipboard fallback
};

// Phase 1 — classify focused window. No shared writes.
[[nodiscard]] WindowClassification ClassifyWindow(HWND hwnd) noexcept;

// Phase 2 — construct injector for a classification. Always returns a
// usable injector (default branch is Win32). Caller atomic_store-
// publishes the result to HookEngine::injector_.
[[nodiscard]] std::shared_ptr<IOutputInjector> Create(
    const WindowClassification& c) noexcept;

}  // namespace NextKey::Output
