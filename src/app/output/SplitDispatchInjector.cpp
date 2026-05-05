// src/app/output/SplitDispatchInjector.cpp
//
// D0 stub. Returns false / no-op. D3 implements split-dispatch:
// SendInput(BS batch) → Sleep(sleepMs_) → SendInput(char batch).
#include "SplitDispatchInjector.h"

namespace NextKey::Output {

bool SplitDispatchInjector::Replace(std::size_t /*bsCount*/,
                                    std::wstring_view /*text*/) noexcept {
    return false;  // D3 implementation
}

void SplitDispatchInjector::SendKey(unsigned short /*vkCode*/) noexcept {
    // D3 implementation.
}

}  // namespace NextKey::Output
