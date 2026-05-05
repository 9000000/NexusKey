// src/app/output/RichEditEmReplaceSelInjector.cpp
//
// D0 stub. Returns false / no-op. D2 implements Replace via
// EM_GETSEL / EM_SETSEL / EM_REPLACESEL.
#include "RichEditEmReplaceSelInjector.h"

namespace NextKey::Output {

bool RichEditEmReplaceSelInjector::Replace(std::size_t /*bsCount*/,
                                           std::wstring_view /*text*/) noexcept {
    return false;  // D2 implementation
}

void RichEditEmReplaceSelInjector::SendKey(unsigned short /*vkCode*/) noexcept {
    // D2 implementation. Will fall through to Win32 SendInput for the
    // physical re-inject semantics that InjectKey requires.
}

}  // namespace NextKey::Output
