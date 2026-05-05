// tests/output/InjectorTestBase.h
//
// Shared GTest fixture for IOutputInjector implementations. Swaps the
// function-pointer test seams (Internal::g_sendInput / g_sendMessageW
// / g_sleep) for capturing lambdas in SetUp(), restores in TearDown().
//
// Tests inspect captured INPUT events / SendMessage calls / Sleep
// delays via static members. Each test is sequential (GTest runs
// fixtures one at a time) so static state is safe.
//
// Spec: docs/plans/sprint-2-output-injector.md §5.1
#pragma once

#include "app/output/Internal.h"

#include <gtest/gtest.h>
#include <vector>
#include <tuple>
#include <windows.h>

namespace NextKey::Output::Test {

class InjectorTestBase : public ::testing::Test {
protected:
    // Captured by g_sendInput lambda. Each call appends events.
    static inline std::vector<INPUT> capturedInputs;

    // Captured by g_sleep lambda. Each call appends ms.
    static inline std::vector<DWORD> sleepDelays;

    // Captured by g_sendMessageW lambda. Each call appends (hwnd, msg, w, l).
    static inline std::vector<std::tuple<HWND, UINT, WPARAM, LPARAM>> capturedMsgs;

    // 0 = full delivery (return n). Non-zero = partial (return this value).
    // Tests set this in their body BEFORE calling Replace to simulate
    // renderer drop.
    static inline UINT sendInputReturnOverride = 0;

    // Default SendMessageW return for tests that don't override.
    static inline LRESULT sendMessageReturnDefault = 1;

    void SetUp() override {
        capturedInputs.clear();
        sleepDelays.clear();
        capturedMsgs.clear();
        sendInputReturnOverride = 0;
        sendMessageReturnDefault = 1;

        Internal::g_sendInput = [](UINT n, LPINPUT inputs, int) -> UINT {
            for (UINT i = 0; i < n; ++i) capturedInputs.push_back(inputs[i]);
            return sendInputReturnOverride > 0 ? sendInputReturnOverride : n;
        };
        Internal::g_sleep = [](DWORD ms) {
            sleepDelays.push_back(ms);
        };
        // Tests can override either of these in their bodies BEFORE calling
        // the impl. By default both capture and return sendMessageReturnDefault
        // (1 = success). g_sendMessageTimeoutW also writes 0 to the result
        // out-param — tests caring about return value override the lambda.
        Internal::g_sendMessageW = [](HWND h, UINT m, WPARAM w, LPARAM l) -> LRESULT {
            capturedMsgs.emplace_back(h, m, w, l);
            return sendMessageReturnDefault;
        };
        Internal::g_sendMessageTimeoutW = [](HWND h, UINT m, WPARAM w, LPARAM l,
                                             UINT /*flags*/, UINT /*timeout*/,
                                             PDWORD_PTR result) -> LRESULT {
            capturedMsgs.emplace_back(h, m, w, l);
            if (result) *result = 0;
            return sendMessageReturnDefault;
        };
    }

    void TearDown() override {
        Internal::g_sendInput           = ::SendInput;
        Internal::g_sleep               = ::Sleep;
        Internal::g_sendMessageW        = ::SendMessageW;
        Internal::g_sendMessageTimeoutW = ::SendMessageTimeoutW;
    }
};

}  // namespace NextKey::Output::Test
