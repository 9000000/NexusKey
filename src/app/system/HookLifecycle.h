// VKey - Hook Lifecycle (Wave 3 PR 3.1, 2026-05-23)
// SPDX-License-Identifier: GPL-3.0-only
//
// Owns the dedicated WH_KEYBOARD_LL + WH_MOUSE_LL hook thread, the HHOOK
// handles, the mailbox wake-up plumbing, and the start-time handshake CV.
// Extracted from HookEngine to isolate Win32 lifecycle concerns from engine
// business logic — HookEngine still owns ProcessKeyDown / focus / config /
// output, but no longer owns the thread/hooks/mailbox storage.
//
// Why a dedicated thread (preserved verbatim from HookEngine's prior design):
//   * `WH_KEYBOARD_LL` callbacks fire on the installer thread's message pump.
//   * Win10/11 silently unhooks any LL hook whose pump can't service events
//     within `LowLevelHooksTimeout` (≤1000ms, registry-tunable). Sciter
//     rendering / SharedState contention / config reloads on the main UI
//     thread were causing those stalls and getting hooks unhooked under us.
//   * Dedicated thread = no UI work = no stalls = no silent unhook.

#pragma once

#include "HookCommandMailbox.h"
#include "core/DorionHookReclaimPolicy.h"

#include <Windows.h>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <thread>

namespace NextKey {

enum class DorionKeyboardReclaimResult : std::uint8_t {
    Replaced,
    ForegroundChanged,
    BusyTimeout,
    UnhookFailed,
    InstallFailed,
    ScheduleFailed,
};

class HookLifecycle {
public:
    /// Invoked from the hook thread on WM_APP_HOOK_COMMAND wake-up. HookEngine
    /// sets this at Start() to its DrainHookCommands implementation.
    using DrainFn = std::function<void()>;
    /// Invoked from the hook thread's message pump after a matcher fire was
    /// posted. User callbacks never execute inside the low-level hook proc.
    using HotkeyDispatchFn = std::function<void(std::size_t)>;
    /// HookEngine-owned quiet-state gate. Called only on the hook-owner pump,
    /// outside an LL callback, before the delayed Dorion transaction.
    using DorionReclaimReadyFn = std::function<bool()>;
    /// Terminal result for exactly one verified Dorion process identity.
    using DorionReclaimResultFn = std::function<void(
        DorionProcessIdentity, DorionKeyboardReclaimResult)>;

    HookLifecycle();
    ~HookLifecycle();

    HookLifecycle(const HookLifecycle&) = delete;
    HookLifecycle& operator=(const HookLifecycle&) = delete;

    /// Spawn the hook thread, install WH_KEYBOARD_LL + WH_MOUSE_LL, block
    /// until the thread signals ready (5s timeout). Returns false on hook
    /// install failure (keyboardHook_ remains nullptr; thread is joined).
    [[nodiscard]] bool Start(HINSTANCE hInstance,
                              HOOKPROC keyboardProc,
                              HOOKPROC mouseProc,
                              DrainFn drainFn,
                              HotkeyDispatchFn hotkeyDispatchFn,
                              DorionReclaimReadyFn dorionReclaimReadyFn,
                              DorionReclaimResultFn dorionReclaimResultFn);

    /// Post WM_QUIT to the hook thread, join. Hooks are unhooked from the
    /// hook thread itself (MSDN requirement: unhook on installer thread).
    void Stop();

    /// Hook thread id. Returns 0 before Start completes the handshake or
    /// after Stop.
    [[nodiscard]] DWORD ThreadId() const noexcept {
        return threadId_.load(std::memory_order_acquire);
    }

    /// True while a lifecycle owner thread still needs to be joined. This is
    /// deliberately distinct from IsRunning(): a runtime replacement can lose
    /// the keyboard handle while its message pump remains alive. Start/Stop
    /// ownership is serialized by HookEngine's controlling thread.
    [[nodiscard]] bool HasOwnerThread() const noexcept {
        return thread_.joinable();
    }

    /// True when the keyboard hook was successfully installed and the
    /// thread is still alive. Mouse hook is best-effort and not reflected
    /// here. A bounded Dorion replacement publishes its handle atomically.
    [[nodiscard]] bool IsRunning() const noexcept {
        return keyboardHook_.load(std::memory_order_acquire) != nullptr;
    }

    /// Shared mailbox for cross-thread command bits (focus changes, config
    /// reload, tick poll, toggle VN). Producers (main / worker / hotkey)
    /// call Post; the hook pump's WM_APP_HOOK_COMMAND handler invokes the
    /// drain callback. The wake-fn is wired internally at thread start.
    [[nodiscard]] HookCommandMailbox& Mailbox() noexcept { return mailbox_; }

    /// Queue a matched application-hotkey slot for callback dispatch by this
    /// lifecycle's message pump. Safe from the low-level hook callback and a
    /// no-op before startup or after shutdown.
    void PostHotkey(std::size_t slot) noexcept;

    /// Resolve a PID into the process-lifetime identity used by the Dorion
    /// compatibility policy. This is a cold-path OpenProcess/GetProcessTimes
    /// query; callers must keep it outside the low-level hook callback.
    [[nodiscard]] static DorionProcessIdentity ResolveProcessIdentity(
        std::uint32_t pid) noexcept;

    /// Queue one delayed, exact-process Dorion compatibility transaction. The
    /// request runs only after its caller's LL callback unwinds, and never
    /// installs a second VKey keyboard hook alongside the first.
    [[nodiscard]] bool RequestDorionKeyboardReclaim(
        DorionProcessIdentity identity) noexcept;

private:
    [[nodiscard]] HHOOK InstallKeyboardHook() noexcept;
    void ThreadProc();

    std::thread thread_;
    std::atomic<DWORD> threadId_{0};
    std::atomic<bool> ready_{false};
    std::mutex startMutex_;
    std::condition_variable startCv_;

    // Hook handles — written by ThreadProc only (single-writer = installer
    // thread). The keyboard handle is atomic because the bounded Dorion
    // transaction can replace it after startup while main-thread code calls
    // IsRunning(). Mouse remains startup/teardown-only and is never touched.
    std::atomic<HHOOK> keyboardHook_{nullptr};
    HHOOK mouseHook_ = nullptr;

    HINSTANCE hInstance_ = nullptr;
    HOOKPROC keyboardProc_ = nullptr;
    HOOKPROC mouseProc_ = nullptr;

    HookCommandMailbox mailbox_;
    DrainFn drainFn_;
    HotkeyDispatchFn hotkeyDispatchFn_;
    DorionReclaimReadyFn dorionReclaimReadyFn_;
    DorionReclaimResultFn dorionReclaimResultFn_;
};

}  // namespace NextKey
