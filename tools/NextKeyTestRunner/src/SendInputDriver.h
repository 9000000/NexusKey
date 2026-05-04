// SendInputDriver.h — Win32 SendInput wrapper for the test runner.
//
// Acquires 1 ms timer resolution via timeBeginPeriod for the lifetime of the
// Driver instance. Provides a busy-wait helper for sub-ms inter-key timing
// (kernel Sleep granularity is ~15.6 ms by default; even after timeBeginPeriod
// it bottoms out around 1 ms). For inter-key delays > 2 ms we use Sleep for
// most of the wait then busy-wait the last ~1 ms for precision.
//
// VkKeyScanW handles char → (VK, modifiers) under the CURRENT keyboard layout,
// so this driver is layout-aware. Chars that cannot be typed on the active
// layout (e.g. uppercase Vietnamese passthrough on US-QWERTY) make SendChar
// return false — caller decides whether to abort or continue.
//
// Windows-only. Not compiled on Linux.

#pragma once

#include <cstdint>
#include <string_view>

namespace NextKey::TestRunner::SendInputDriver {

struct Options {
    uint32_t interKeyMicros = 10'000;   // 10 ms default — realistic typing speed
    bool useTimeBeginPeriod = true;     // ask kernel for 1 ms timer resolution
};

class Driver {
public:
    explicit Driver(const Options& opts) noexcept;
    ~Driver() noexcept;

    Driver(const Driver&) = delete;
    Driver& operator=(const Driver&) = delete;

    // Returns false if the char cannot be typed on the current keyboard layout.
    [[nodiscard]] bool SendChar(char16_t ch) noexcept;

    // Sends each char with `interKeyMicros` delay between them. Stops at the
    // first untypeable char and returns false; otherwise returns true.
    [[nodiscard]] bool SendString(std::u16string_view input) noexcept;

    // Sends a single VK code with explicit modifier state. Use for Ctrl+A,
    // Ctrl+C, Delete, etc. `vk` is a Win32 virtual key code (e.g. 0x41 for 'A').
    // Returns false if any underlying SendInput call failed (e.g. UAC consent
    // dialog blocking input, session locked).
    [[nodiscard]] bool SendKeyCombo(uint16_t vk, bool ctrl, bool shift, bool alt) noexcept;

private:
    Options options_;
    bool timerResolutionAcquired_ = false;
};

// Sleeps `micros` microseconds. For micros > 2000 uses Sleep+busy-wait; below
// that, pure busy-wait on QueryPerformanceCounter (sub-ms precision).
void BusyWaitMicros(uint64_t micros) noexcept;

}  // namespace NextKey::TestRunner::SendInputDriver
