// src/app/output/OutputInjectorFactory.cpp
//
// D2: Create() dispatches RichEditEmReplaceSelInjector when
// classification reports isRichEditD2DPT=true. ClassifyWindow itself
// is still a stub (D3 ports the full classification logic — exe name
// scan + class detection — from HookEngine::OnFocusChanged). For now
// HookEngine populates WindowClassification fields manually and calls
// Create() directly, see HookEngine.cpp comment "Sprint 2 D2: build
// the IOutputInjector for this classification".
#include "OutputInjectorFactory.h"

#include "Win32SendInputInjector.h"
#include "RichEditEmReplaceSelInjector.h"
// D3 will include SplitDispatchInjector.h as Electron + Console
// classifications become live in Create().

namespace NextKey::Output {

WindowClassification ClassifyWindow(HWND /*hwnd*/) noexcept {
    // D2 stub. D3 ports the full classification logic from HookEngine.
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
    return std::make_shared<Win32SendInputInjector>(c.isChromium);
}

}  // namespace NextKey::Output
