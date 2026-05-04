#include "SendInputDriver.h"

// clang-format off
#include <Windows.h>
#include <mmsystem.h>  // timeBeginPeriod / timeEndPeriod
// clang-format on

namespace NextKey::TestRunner::SendInputDriver {

namespace {

// SendInput returns the number of events injected (1 on success, 0 on failure).
[[nodiscard]] bool EmitKeyEvent(WORD vk, bool keyUp) noexcept {
    INPUT input = {};
    input.type = INPUT_KEYBOARD;
    input.ki.wVk = vk;
    input.ki.dwFlags = keyUp ? KEYEVENTF_KEYUP : 0;
    return SendInput(1, &input, sizeof(INPUT)) == 1;
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

    bool ok = true;
    if (needsShift) ok &= EmitKeyEvent(VK_SHIFT, false);
    if (needsCtrl)  ok &= EmitKeyEvent(VK_CONTROL, false);
    if (needsAlt)   ok &= EmitKeyEvent(VK_MENU, false);

    ok &= EmitKeyEvent(vk, false);
    ok &= EmitKeyEvent(vk, true);

    if (needsAlt)   ok &= EmitKeyEvent(VK_MENU, true);
    if (needsCtrl)  ok &= EmitKeyEvent(VK_CONTROL, true);
    if (needsShift) ok &= EmitKeyEvent(VK_SHIFT, true);

    return ok;
}

bool Driver::SendKeyCombo(uint16_t vk, bool ctrl, bool shift, bool alt) noexcept {
    bool ok = true;
    if (ctrl)  ok &= EmitKeyEvent(VK_CONTROL, false);
    if (shift) ok &= EmitKeyEvent(VK_SHIFT, false);
    if (alt)   ok &= EmitKeyEvent(VK_MENU, false);

    ok &= EmitKeyEvent(static_cast<WORD>(vk), false);
    ok &= EmitKeyEvent(static_cast<WORD>(vk), true);

    if (alt)   ok &= EmitKeyEvent(VK_MENU, true);
    if (shift) ok &= EmitKeyEvent(VK_SHIFT, true);
    if (ctrl)  ok &= EmitKeyEvent(VK_CONTROL, true);

    return ok;
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

    // QueryPerformanceFrequency is constant per system -- cache once.
    static const uint64_t freq = []() noexcept {
        LARGE_INTEGER f;
        QueryPerformanceFrequency(&f);
        return static_cast<uint64_t>(f.QuadPart);
    }();

    LARGE_INTEGER start;
    QueryPerformanceCounter(&start);

    const uint64_t targetTicks = (micros * freq) / 1'000'000ULL;

    // For long waits, Sleep most of it (saves CPU) then busy-wait the last
    // ~1 ms for precision. Below 2 ms, pure busy-wait -- Sleep is too coarse.
    if (micros > 2'000) {
        Sleep(static_cast<DWORD>((micros - 1'000) / 1'000));
    }

    LARGE_INTEGER now;
    do {
        QueryPerformanceCounter(&now);
    } while (static_cast<uint64_t>(now.QuadPart - start.QuadPart) < targetTicks);
}

}  // namespace NextKey::TestRunner::SendInputDriver
