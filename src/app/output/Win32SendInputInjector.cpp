// src/app/output/Win32SendInputInjector.cpp
//
// D0 stub. Returns false / no-op. D1 implements Replace + SendKey.
#include "Win32SendInputInjector.h"

namespace NextKey::Output {

bool Win32SendInputInjector::Replace(std::size_t /*bsCount*/,
                                     std::wstring_view /*text*/) noexcept {
    // D1 implementation. Stub returns false → caller falls back to
    // passthrough (today's TryEditMessagePaste failure semantics).
    return false;
}

void Win32SendInputInjector::SendKey(unsigned short /*vkCode*/) noexcept {
    // D1 implementation.
}

}  // namespace NextKey::Output
