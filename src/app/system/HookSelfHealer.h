// NexusKey - Hook Self-Healer (Windows-only)
// SPDX-License-Identifier: GPL-3.0-only
//
// Detects when an external app installs a higher-priority LL keyboard
// hook above NexusKey's hook and skips CallNextHookEx (Anti-Dorion).
// Strategy: dual-channel comparison — Raw Input always reaches us
// (kernel-level), so missing LL hook fires while Raw Input fires =
// hook hijacked. Reinstall hooks via injected callback to jump back
// to top of LIFO chain.
//
// Threading: all methods (except RecordHookFire) are called from the
// hook thread. RecordHookFire is called inline from LowLevelKeyboardProc
// — same thread as the WndProc, no atomic needed.

#pragma once

#ifdef _WIN32
#include <Windows.h>
#endif

#include <functional>
#include <memory>

namespace NextKey {

/// Reinstall callback — invoked by the healer when it detects hook death.
/// Implementation must call UnhookWindowsHookEx + SetWindowsHookExW for both
/// keyboard and mouse hooks (kept paired for chain symmetry). Returns true
/// on success; false signals the healer to keep cooldown unreset so future
/// retries are still possible.
using ReinstallHookFn = std::function<bool()>;

class IHookSelfHealer {
public:
    virtual ~IHookSelfHealer() = default;
    virtual bool Start() = 0;
    virtual void Stop() = 0;
    virtual void RecordHookFire() = 0;
};

#ifdef _WIN32

class RawInputSelfHealer : public IHookSelfHealer {
public:
    RawInputSelfHealer(HINSTANCE hInst, ReinstallHookFn reinstaller);
    ~RawInputSelfHealer() override;

    RawInputSelfHealer(const RawInputSelfHealer&) = delete;
    RawInputSelfHealer& operator=(const RawInputSelfHealer&) = delete;

    bool Start() override;
    void Stop() override;
    void RecordHookFire() override;

private:
    static LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);

    HINSTANCE hInst_ = nullptr;
    ReinstallHookFn reinstaller_;
    HWND hwnd_ = nullptr;
    DWORD lastLlHookTime_ = 0;
    uint8_t consecutiveRawMisses_ = 0;
    DWORD lastSelfHealTime_ = 0;

    static constexpr UINT_PTR SELF_HEAL_TIMER_ID = 42;
    static constexpr uint8_t SELF_HEAL_MISS_THRESHOLD = 3;
    static constexpr DWORD SELF_HEAL_COOLDOWN_MS = 10000;
    static constexpr DWORD SELF_HEAL_HOOK_FRESHNESS_MS = 200;

    static thread_local RawInputSelfHealer* tlsActive_;
};

#endif  // _WIN32

}  // namespace NextKey
