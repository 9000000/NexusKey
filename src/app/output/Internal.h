// src/app/output/Internal.h
//
// Test-seam infrastructure private to src/app/output/. Function-pointer
// indirection on SendInput / SendMessageW / Sleep so injector impls are
// testable on Windows GTest with mocked APIs (no GUI required).
//
// Production wires the pointers to ::SendInput / ::SendMessageW / ::Sleep.
// Tests swap to capturing lambdas in fixture SetUp() and restore in
// TearDown() — see tests/output/InjectorTestBase.h.
//
// Spec: docs/plans/sprint-2-output-injector.md §2.5
#pragma once

#include <windows.h>

namespace NextKey::Output::Internal {

using SendInputFn    = UINT (WINAPI*)(UINT, LPINPUT, int);
using SendMessageWFn = LRESULT (WINAPI*)(HWND, UINT, WPARAM, LPARAM);
using SleepFn        = void (WINAPI*)(DWORD);

// Test seams. Production initializes to the real Win32 APIs.
extern SendInputFn    g_sendInput;
extern SendMessageWFn g_sendMessageW;
extern SleepFn        g_sleep;

// Wrapper around g_sendInput with partial-send detection. Returns true
// iff all events delivered; false on partial (renderer drop case —
// detected when SendInput returns fewer events than requested).
[[nodiscard]] bool TrackedSendInput(INPUT* events, UINT count) noexcept;

// Marker dwExtraInfo so own synth events skip our own hook (Rule #11.4
// early-return). MUST match HookEngine::NEXUSKEY_EXTRA_INFO ("NK"). If
// these diverge, own synth events are not recognized as own → infinite
// re-entry loop. The integration test (chaos corpus) catches this.
constexpr ULONG_PTR kNexusKeyExtraInfo = 0x4E4BULL;

}  // namespace NextKey::Output::Internal
