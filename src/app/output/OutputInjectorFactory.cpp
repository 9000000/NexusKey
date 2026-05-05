// src/app/output/OutputInjectorFactory.cpp
//
// D0 stub: ClassifyWindow returns all-false → Create returns default
// Win32SendInputInjector for every host. D2 wires RichEdit detection;
// D3 wires Electron / Console / Chromium detection.
#include "OutputInjectorFactory.h"

#include "Win32SendInputInjector.h"
// D2/D3 will include the other impl headers as their classifications
// become live in Create().

namespace NextKey::Output {

WindowClassification ClassifyWindow(HWND /*hwnd*/) noexcept {
    // D0 stub. D2 adds RichEditD2DPT class detection.
    // D3 adds Electron / Console / Chromium exe-name detection.
    return WindowClassification{};
}

std::shared_ptr<IOutputInjector> Create(
        const WindowClassification& c) noexcept {
    // Priority order (D2/D3 wire the higher-priority branches):
    //   if c.isRichEditD2DPT  → RichEditEmReplaceSelInjector
    //   if c.isElectron       → SplitDispatchInjector(6)
    //   if c.isConsole        → SplitDispatchInjector(5)
    //   default               → Win32SendInputInjector(c.isChromium)
    return std::make_shared<Win32SendInputInjector>(c.isChromium);
}

}  // namespace NextKey::Output
