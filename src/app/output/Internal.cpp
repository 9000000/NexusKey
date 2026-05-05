// src/app/output/Internal.cpp
//
// Definitions for the test seams declared in Internal.h. Pointers init
// to real Win32 APIs at static-init time.
#include "Internal.h"

namespace NextKey::Output::Internal {

SendInputFn    g_sendInput    = ::SendInput;
SendMessageWFn g_sendMessageW = ::SendMessageW;
SleepFn        g_sleep        = ::Sleep;

bool TrackedSendInput(INPUT* events, UINT count) noexcept {
    UINT sent = g_sendInput(count, events, sizeof(INPUT));
    return sent == count;
}

}  // namespace NextKey::Output::Internal
