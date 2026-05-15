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

using SendInputFn          = UINT (WINAPI*)(UINT, LPINPUT, int);
using SendMessageWFn       = LRESULT (WINAPI*)(HWND, UINT, WPARAM, LPARAM);
using SendMessageTimeoutWFn = LRESULT (WINAPI*)(HWND, UINT, WPARAM, LPARAM,
                                                UINT, UINT, PDWORD_PTR);
using SleepFn              = void (WINAPI*)(DWORD);
// Sprint 2 D5: synth-counter callback. Fires from TrackedSendInput
// pre-SendInput (positive delta = events about to dispatch) and on
// partial-send (negative delta = compensate for events that didn't
// land). HookEngine wires this to its synthEventsPending_ atomic so
// the per-event decrement in LowLevelKeyboardProc balances correctly.
// Function pointer (not std::function) keeps the layer decoupled — no
// HookEngine dependency leaking into src/app/output/.
using SynthCounterFn       = void (*)(int delta) noexcept;

// Test seams. Production initializes to the real Win32 APIs.
extern SendInputFn           g_sendInput;
extern SendMessageWFn        g_sendMessageW;
extern SendMessageTimeoutWFn g_sendMessageTimeoutW;
extern SleepFn               g_sleep;
extern SynthCounterFn        g_synthCounterCallback;  // null = disabled

// Wrapper around g_sendInput with partial-send detection. Returns true
// iff all events delivered; false on partial (renderer drop case —
// detected when SendInput returns fewer events than requested). Fires
// g_synthCounterCallback (when non-null) before SendInput with +count,
// then again with -(count-sent) on partial.
[[nodiscard]] bool TrackedSendInput(INPUT* events, UINT count) noexcept;

// Marker dwExtraInfo so own synth events skip our own hook (Rule #11.4
// early-return). MUST match HookEngine::VKEY_EXTRA_INFO ("NK"). If
// these diverge, own synth events are not recognized as own → infinite
// re-entry loop. The integration test (chaos corpus) catches this.
constexpr ULONG_PTR kVKeyExtraInfo = 0x4E4BULL;

}  // namespace NextKey::Output::Internal
