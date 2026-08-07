// src/app/output/ClipboardInjector.h
#pragma once

#include "IOutputInjector.h"
#include <windows.h>
#include <chrono>

namespace NextKey::Output {

class ClipboardInjector final : public IOutputInjector {
public:
    ClipboardInjector() noexcept = default;
    ~ClipboardInjector() override = default;

    // reinjectVk is honoured (prepended to the dispatched INPUT batch): the
    // per-app "send method = Clipboard" override selects this injector
    // without setting OutputDispatcher's useClipboardPaste_, so a non-zero
    // reinjectVk does reach here through the generic branch. See the impl.
    bool Replace(std::size_t bsCount, std::wstring_view text,
                 unsigned short reinjectVk = 0) noexcept override;
    void SendKey(unsigned short vkCode) noexcept override;

    std::chrono::milliseconds SettleBudget() const noexcept override {
        return std::chrono::milliseconds{150};
    }
};

} // namespace NextKey::Output
