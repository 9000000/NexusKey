#include "SendInputDriver.h"

// clang-format off
#include <Windows.h>
#include <mmsystem.h>  // timeBeginPeriod / timeEndPeriod
// clang-format on

namespace NextKey::TestRunner::SendInputDriver {

namespace {

void EmitKeyEvent(WORD vk, bool keyUp) noexcept {
    INPUT input = {};
    input.type = INPUT_KEYBOARD;
    input.ki.wVk = vk;
    input.ki.dwFlags = keyUp ? KEYEVENTF_KEYUP : 0;
    SendInput(1, &input, sizeof(INPUT));
}

}  // namespace

Driver::Driver(const Options& opts) noexcept
    : options_(opts) {
    if (opts.useTimeBeginPeriod) {
        if (timeBeginPeriod(1) == TIMERR_NOERROR) {
            timerResolutionAcquired_ = true;
        }
    }
}

Driver::~Driver() noexcept {
    if (timerResolutionAcquired_) {
        timeEndPeriod(1);
    }
}

bool Driver::SendChar(char16_t ch) noexcept {
    const SHORT scan = VkKeyScanW(static_cast<WCHAR>(ch));
    if (scan == -1) return false;

    const WORD vk = LOBYTE(scan);
    const BYTE mods = HIBYTE(scan);
    const bool needsShift = (mods & 1) != 0;
    const bool needsCtrl  = (mods & 2) != 0;
    const bool needsAlt   = (mods & 4) != 0;

    if (needsShift) EmitKeyEvent(VK_SHIFT, false);
    if (needsCtrl)  EmitKeyEvent(VK_CONTROL, false);
    if (needsAlt)   EmitKeyEvent(VK_MENU, false);

    EmitKeyEvent(vk, false);
    EmitKeyEvent(vk, true);

    if (needsAlt)   EmitKeyEvent(VK_MENU, true);
    if (needsCtrl)  EmitKeyEvent(VK_CONTROL, true);
    if (needsShift) EmitKeyEvent(VK_SHIFT, true);

    return true;
}

bool Driver::SendString(std::u16string_view input) noexcept {
    bool first = true;
    for (char16_t ch : input) {
        if (!first) BusyWaitMicros(options_.interKeyMicros);
        if (!SendChar(ch)) return false;
        first = false;
    }
    return true;
}

void BusyWaitMicros(uint64_t micros) noexcept {
    if (micros == 0) return;

    LARGE_INTEGER freq;
    QueryPerformanceFrequency(&freq);

    LARGE_INTEGER start;
    QueryPerformanceCounter(&start);

    const uint64_t targetTicks =
        (micros * static_cast<uint64_t>(freq.QuadPart)) / 1'000'000ULL;

    // For long waits, Sleep most of it (saves CPU) then busy-wait the last
    // ~1 ms for precision. Below 2 ms, pure busy-wait — Sleep is too coarse.
    if (micros > 2'000) {
        Sleep(static_cast<DWORD>((micros - 1'000) / 1'000));
    }

    LARGE_INTEGER now;
    do {
        QueryPerformanceCounter(&now);
    } while (static_cast<uint64_t>(now.QuadPart - start.QuadPart) < targetTicks);
}

}  // namespace NextKey::TestRunner::SendInputDriver
