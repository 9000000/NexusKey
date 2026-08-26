// VKey - Hook Lifecycle Implementation
// SPDX-License-Identifier: GPL-3.0-only

#include "HookLifecycle.h"
#include "core/CrashLog.h"
#include "core/Debug.h"
#include "core/DorionHookReclaimSequence.h"
#include "core/SingleHookReplacement.h"

#include <chrono>
#include <cstdint>
#include <exception>
#include <utility>

namespace NextKey {

// Mirrors the HookEngine.cpp prefix-only pattern: args not evaluated when the
// runtime logger is off, so HOOK_LOG in tight pump loops is near-zero cost.
#define HOOK_LIFE_LOG(fmt, ...) do {                                           \
    if (::NextKey::Logger::IsEnabled())                                        \
        ::NextKey::Logger::Log(L"[HookLife] " fmt, ##__VA_ARGS__);             \
} while (0)

// Custom WM_APP message ids used only inside this lifecycle's pump.
static constexpr UINT WM_APP_HOOK_COMMAND = WM_APP + 2;
static constexpr UINT WM_APP_HOTKEY_FIRED = WM_APP + 3;
static constexpr UINT WM_APP_DORION_KEYBOARD_RECLAIM = WM_APP + 4;

// One bounded compatibility attempt per verified Dorion process lifetime.
// The initial delay closes Dorion's observed focus-before-hook-install race;
// gate retries wait for a safe transaction boundary without churning hooks.
static constexpr UINT kDorionInitialDelayMs = 1800;
static constexpr UINT kDorionGateRetryMs = 100;
static constexpr std::uint8_t kDorionMaxGateChecks = 30;

static_assert(sizeof(LPARAM) >= sizeof(std::uint64_t),
              "Dorion process identity requires the supported x64 build");

HookLifecycle::HookLifecycle() = default;

HookLifecycle::~HookLifecycle() {
    Stop();
}

DorionProcessIdentity HookLifecycle::ResolveProcessIdentity(
        std::uint32_t pid) noexcept {
    if (pid == 0) return {};

    const HANDLE process = OpenProcess(
        PROCESS_QUERY_LIMITED_INFORMATION, FALSE, static_cast<DWORD>(pid));
    if (!process) return {};

    FILETIME creation{};
    FILETIME exit{};
    FILETIME kernel{};
    FILETIME user{};
    const bool queried = GetProcessTimes(
        process, &creation, &exit, &kernel, &user) != FALSE;
    CloseHandle(process);
    if (!queried) return {};

    const std::uint64_t creationTime =
        (static_cast<std::uint64_t>(creation.dwHighDateTime) << 32)
        | creation.dwLowDateTime;
    if (creationTime == 0) return {};
    return {pid, creationTime};
}

bool HookLifecycle::Start(HINSTANCE hInstance,
                          HOOKPROC keyboardProc,
                          HOOKPROC mouseProc,
                          DrainFn drainFn,
                          HotkeyDispatchFn hotkeyDispatchFn,
                          DorionReclaimReadyFn dorionReclaimReadyFn,
                          DorionReclaimResultFn dorionReclaimResultFn) {
    // A replacement install failure can leave the pump alive without a
    // keyboard handle. Ownership—not handle value—is the startup guard.
    if (thread_.joinable()) return false;

    hInstance_ = hInstance;
    keyboardProc_ = keyboardProc;
    mouseProc_ = mouseProc;
    drainFn_ = std::move(drainFn);
    hotkeyDispatchFn_ = std::move(hotkeyDispatchFn);
    dorionReclaimReadyFn_ = std::move(dorionReclaimReadyFn);
    dorionReclaimResultFn_ = std::move(dorionReclaimResultFn);
    ready_.store(false, std::memory_order_release);

    thread_ = std::thread(&HookLifecycle::ThreadProc, this);

    // A healthy hook thread completes this handshake within milliseconds.
    {
        std::unique_lock<std::mutex> lk(startMutex_);
        startCv_.wait_for(lk, std::chrono::seconds(5), [this] {
            return ready_.load(std::memory_order_acquire);
        });
    }
    if (!IsRunning()) {
        NEXTKEY_LOG(L"HookLifecycle: keyboard hook install failed (thread did not signal ready or SetWindowsHookExW failed)");
        if (thread_.joinable()) {
            const DWORD tid = threadId_.load(std::memory_order_acquire);
            if (tid) PostThreadMessageW(tid, WM_QUIT, 0, 0);
            thread_.join();
        }
        threadId_.store(0, std::memory_order_release);
        drainFn_ = nullptr;
        hotkeyDispatchFn_ = nullptr;
        dorionReclaimReadyFn_ = nullptr;
        dorionReclaimResultFn_ = nullptr;
        return false;
    }
    return true;
}

void HookLifecycle::Stop() {
    if (thread_.joinable()) {
        const DWORD tid = threadId_.load(std::memory_order_acquire);
        if (tid) PostThreadMessageW(tid, WM_QUIT, 0, 0);
        thread_.join();
    }
    keyboardHook_.store(nullptr, std::memory_order_release);
    mouseHook_ = nullptr;
    threadId_.store(0, std::memory_order_release);
    ready_.store(false, std::memory_order_release);
    drainFn_ = nullptr;
    hotkeyDispatchFn_ = nullptr;
    dorionReclaimReadyFn_ = nullptr;
    dorionReclaimResultFn_ = nullptr;
    // Clear a stale wake latch so a future Start receives its first command.
    mailbox_.ResetLatch();
}

void HookLifecycle::PostHotkey(std::size_t slot) noexcept {
    const DWORD tid = threadId_.load(std::memory_order_acquire);
    if (tid) {
        PostThreadMessageW(
            tid, WM_APP_HOTKEY_FIRED, static_cast<WPARAM>(slot), 0);
    }
}

bool HookLifecycle::RequestDorionKeyboardReclaim(
        DorionProcessIdentity identity) noexcept {
    const DWORD tid = threadId_.load(std::memory_order_acquire);
    return tid != 0 && identity.IsValid()
        && PostThreadMessageW(
               tid,
               WM_APP_DORION_KEYBOARD_RECLAIM,
               static_cast<WPARAM>(identity.pid),
               static_cast<LPARAM>(identity.creationTime)) != FALSE;
}

HHOOK HookLifecycle::InstallKeyboardHook() noexcept {
    return SetWindowsHookExW(WH_KEYBOARD_LL, keyboardProc_, hInstance_, 0);
}

void HookLifecycle::ThreadProc() {
    // WH_KEYBOARD_LL callbacks execute on their installer thread via this
    // message queue. Keep this owner otherwise idle to stay comfortably below
    // LowLevelHooksTimeout and avoid silent removal by Windows.
    threadId_.store(GetCurrentThreadId(), std::memory_order_release);
    const DWORD wakeTid = threadId_.load(std::memory_order_acquire);
    mailbox_.SetWakeFn([wakeTid] {
        PostThreadMessageW(wakeTid, WM_APP_HOOK_COMMAND, 0, 0);
    });

    const HHOOK initialKeyboardHook = InstallKeyboardHook();
    keyboardHook_.store(initialKeyboardHook, std::memory_order_release);
    if (!initialKeyboardHook) {
        HOOK_LIFE_LOG(
            L"ThreadProc: keyboard-hook installation FAILED err=%lu",
            GetLastError());
        {
            std::lock_guard<std::mutex> lk(startMutex_);
            ready_.store(true, std::memory_order_release);
        }
        startCv_.notify_one();
        return;
    }

    mouseHook_ = SetWindowsHookExW(WH_MOUSE_LL, mouseProc_, hInstance_, 0);
    // Mouse hook is best-effort and is never part of Dorion compatibility.

    {
        std::lock_guard<std::mutex> lk(startMutex_);
        ready_.store(true, std::memory_order_release);
    }
    startCv_.notify_one();
    HOOK_LIFE_LOG(L"ThreadProc: pump started tid=%lu", wakeTid);

    DorionDelayedReclaimSlots dorionSlots;

    auto notifyResult = [this](DorionProcessIdentity identity,
                               DorionKeyboardReclaimResult result) noexcept {
        if (!dorionReclaimResultFn_) return;
        try {
            dorionReclaimResultFn_(identity, result);
        } catch (const std::exception& e) {
            CrashLog(L"HookLifecycle::DorionReclaimResult", e.what());
        } catch (...) {
            CrashLog(
                L"HookLifecycle::DorionReclaimResult", "(non-std exception)");
        }
    };

    auto currentForegroundIdentity = []() noexcept {
        DWORD pid = 0;
        if (const HWND foreground = GetForegroundWindow()) {
            GetWindowThreadProcessId(foreground, &pid);
        }
        return HookLifecycle::ResolveProcessIdentity(
            static_cast<std::uint32_t>(pid));
    };

    auto reclaimReady = [this]() noexcept {
        if (!dorionReclaimReadyFn_) return false;
        try {
            return dorionReclaimReadyFn_();
        } catch (const std::exception& e) {
            CrashLog(L"HookLifecycle::DorionReclaimReady", e.what());
        } catch (...) {
            CrashLog(
                L"HookLifecycle::DorionReclaimReady", "(non-std exception)");
        }
        return false;
    };

    auto scheduleTicket = [&dorionSlots](DorionDelayedReclaimTicket ticket,
                                         UINT delayMs) noexcept {
        const UINT_PTR timerId = SetTimer(nullptr, 0, delayMs, nullptr);
        if (timerId == 0) return false;
        ticket.timerId = static_cast<std::uintptr_t>(timerId);
        ticket.dueTickMs = GetTickCount64() + delayMs;
        if (!dorionSlots.Arm(ticket)) {
            (void)KillTimer(nullptr, timerId);
            return false;
        }
        return true;
    };

    struct ReplacementAttempt {
        SingleHookReplacementStatus status;
        DWORD lastError{ERROR_SUCCESS};
    };
    auto runReplacement = [this]() noexcept {
        ReplacementAttempt attempt{
            .status = SingleHookReplacementStatus::InstallFailed,
        };
        struct ReplacementContext {
            HookLifecycle* self;
            DWORD* lastError;
        } context{this, &attempt.lastError};

        const SingleHookReplacementOps ops{
            .context = &context,
            .unhook = [](void* opaque, std::uintptr_t rawHandle) noexcept {
                auto& ctx = *static_cast<ReplacementContext*>(opaque);
                const HHOOK hook = reinterpret_cast<HHOOK>(rawHandle);
                if (UnhookWindowsHookEx(hook) != FALSE) {
                    return SingleHookUnhookStatus::Removed;
                }
                *ctx.lastError = GetLastError();
                return *ctx.lastError == ERROR_INVALID_HOOK_HANDLE
                    ? SingleHookUnhookStatus::AlreadyAbsent
                    : SingleHookUnhookStatus::Failed;
            },
            .install = [](void* opaque) noexcept -> std::uintptr_t {
                auto& ctx = *static_cast<ReplacementContext*>(opaque);
                const HHOOK hook = ctx.self->InstallKeyboardHook();
                if (!hook) *ctx.lastError = GetLastError();
                return reinterpret_cast<std::uintptr_t>(hook);
            },
        };

        const HHOOK current = keyboardHook_.load(std::memory_order_acquire);
        std::uintptr_t rawHandle = reinterpret_cast<std::uintptr_t>(current);
        attempt.status = ReplaceSingleHook(rawHandle, ops);
        keyboardHook_.store(
            reinterpret_cast<HHOOK>(rawHandle), std::memory_order_release);
        return attempt;
    };

    auto reportReplacement = [&notifyResult](
                                 DorionProcessIdentity identity,
                                 const ReplacementAttempt& attempt) noexcept {
        switch (attempt.status) {
        case SingleHookReplacementStatus::Replaced:
            HOOK_LIFE_LOG(
                L"ThreadProc: Dorion delayed keyboard reclaim OK pid=%u creation=%llu",
                identity.pid,
                static_cast<unsigned long long>(identity.creationTime));
            notifyResult(identity, DorionKeyboardReclaimResult::Replaced);
            break;
        case SingleHookReplacementStatus::UnhookFailed:
            HOOK_LIFE_LOG(
                L"ThreadProc: Dorion delayed keyboard reclaim unhook FAILED pid=%u err=%lu",
                identity.pid, attempt.lastError);
            notifyResult(identity, DorionKeyboardReclaimResult::UnhookFailed);
            break;
        case SingleHookReplacementStatus::InstallFailed:
            HOOK_LIFE_LOG(
                L"ThreadProc: Dorion delayed keyboard reclaim install FAILED pid=%u err=%lu",
                identity.pid, attempt.lastError);
            notifyResult(identity, DorionKeyboardReclaimResult::InstallFailed);
            break;
        }
    };

    MSG msg{};
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        if (msg.message == WM_APP_DORION_KEYBOARD_RECLAIM) {
            const DorionProcessIdentity identity{
                static_cast<std::uint32_t>(msg.wParam),
                static_cast<std::uint64_t>(msg.lParam),
            };
            if (!identity.IsValid()) {
                notifyResult(identity, DorionKeyboardReclaimResult::ScheduleFailed);
                continue;
            }
            // A duplicate posted message shares the first request's eventual
            // terminal result; never create a second timer or hook attempt.
            if (dorionSlots.HasPending(identity)) {
                HOOK_LIFE_LOG(
                    L"ThreadProc: duplicate Dorion reclaim ignored pid=%u creation=%llu",
                    identity.pid,
                    static_cast<unsigned long long>(identity.creationTime));
                continue;
            }
            if (currentForegroundIdentity() != identity) {
                HOOK_LIFE_LOG(
                    L"ThreadProc: Dorion reclaim not scheduled; foreground changed pid=%u creation=%llu",
                    identity.pid,
                    static_cast<unsigned long long>(identity.creationTime));
                notifyResult(
                    identity, DorionKeyboardReclaimResult::ForegroundChanged);
                continue;
            }

            DorionDelayedReclaimTicket ticket{
                .identity = identity,
                .remainingGateChecks = kDorionMaxGateChecks,
            };
            if (!scheduleTicket(ticket, kDorionInitialDelayMs)) {
                HOOK_LIFE_LOG(
                    L"ThreadProc: Dorion reclaim scheduling FAILED pid=%u err=%lu",
                    identity.pid, GetLastError());
                notifyResult(
                    identity, DorionKeyboardReclaimResult::ScheduleFailed);
                continue;
            }
            HOOK_LIFE_LOG(
                L"ThreadProc: Dorion reclaim armed pid=%u creation=%llu delay=%ums",
                identity.pid,
                static_cast<unsigned long long>(identity.creationTime),
                kDorionInitialDelayMs);
            continue;
        }

        if (msg.message == WM_TIMER) {
            const auto timerId = static_cast<std::uintptr_t>(msg.wParam);
            // SetTimer(nullptr, ..., nullptr) produces a thread timer with no
            // HWND and no TIMERPROC. A window timer or foreign callback may
            // legally reuse the same numeric ID and must stay untouched.
            const bool isOwnedThreadTimer =
                msg.hwnd == nullptr && msg.lParam == 0
                && dorionSlots.HasTimer(timerId);
            if (isOwnedThreadTimer) {
                const auto pending = dorionSlots.TakeIfDue(
                    timerId, GetTickCount64());
                if (!pending) {
                    // This is our timer, but a recycled/stale message arrived
                    // before its current deadline. Leave the live timer armed.
                    continue;
                }

                (void)KillTimer(nullptr, static_cast<UINT_PTR>(timerId));
                DorionDelayedReclaimTicket ticket = *pending;
                if (currentForegroundIdentity() != ticket.identity) {
                    HOOK_LIFE_LOG(
                        L"ThreadProc: Dorion reclaim cancelled; foreground changed pid=%u creation=%llu",
                        ticket.identity.pid,
                        static_cast<unsigned long long>(
                            ticket.identity.creationTime));
                    notifyResult(
                        ticket.identity,
                        DorionKeyboardReclaimResult::ForegroundChanged);
                    continue;
                }

                if (!reclaimReady()) {
                    if (ticket.remainingGateChecks <= 1) {
                        HOOK_LIFE_LOG(
                            L"ThreadProc: Dorion reclaim quiet gate timed out pid=%u",
                            ticket.identity.pid);
                        notifyResult(
                            ticket.identity,
                            DorionKeyboardReclaimResult::BusyTimeout);
                        continue;
                    }
                    --ticket.remainingGateChecks;
                    if (!scheduleTicket(ticket, kDorionGateRetryMs)) {
                        HOOK_LIFE_LOG(
                            L"ThreadProc: Dorion quiet retry scheduling FAILED pid=%u err=%lu",
                            ticket.identity.pid, GetLastError());
                        notifyResult(
                            ticket.identity,
                            DorionKeyboardReclaimResult::ScheduleFailed);
                        continue;
                    }
                    HOOK_LIFE_LOG(
                        L"ThreadProc: Dorion reclaim busy pid=%u checksLeft=%u",
                        ticket.identity.pid,
                        static_cast<unsigned>(ticket.remainingGateChecks));
                    continue;
                }

                // Revalidate after the quiet-state probes, immediately before
                // the non-atomic unhook/install transaction.
                if (currentForegroundIdentity() != ticket.identity) {
                    notifyResult(
                        ticket.identity,
                        DorionKeyboardReclaimResult::ForegroundChanged);
                    continue;
                }
                reportReplacement(ticket.identity, runReplacement());
                continue;
            }
            // Foreign WM_TIMER messages (for example ClipboardInjector's
            // TIMERPROC) must reach DispatchMessageW below.
        }

        if (msg.message == WM_APP_HOOK_COMMAND) {
            if (drainFn_) {
                try {
                    drainFn_();
                } catch (const std::exception& e) {
                    CrashLog(L"HookLifecycle::DrainCallback", e.what());
                } catch (...) {
                    CrashLog(
                        L"HookLifecycle::DrainCallback", "(non-std exception)");
                }
            }
            continue;
        }
        if (msg.message == WM_APP_HOTKEY_FIRED) {
            if (hotkeyDispatchFn_) {
                try {
                    hotkeyDispatchFn_(static_cast<std::size_t>(msg.wParam));
                } catch (const std::exception& e) {
                    CrashLog(L"HookLifecycle::DispatchHotkey", e.what());
                } catch (...) {
                    CrashLog(
                        L"HookLifecycle::DispatchHotkey", "(non-std exception)");
                }
            }
            continue;
        }
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    for (const auto timerId : dorionSlots.ActiveTimerIds()) {
        if (timerId != 0) {
            (void)KillTimer(nullptr, static_cast<UINT_PTR>(timerId));
        }
    }
    dorionSlots.Clear();

    // Unhook on the installer thread. At all times there was at most one VKey
    // keyboard hook; the mouse hook remained untouched by compatibility work.
    if (const HHOOK hook =
            keyboardHook_.exchange(nullptr, std::memory_order_acq_rel)) {
        (void)UnhookWindowsHookEx(hook);
    }
    if (mouseHook_) {
        (void)UnhookWindowsHookEx(mouseHook_);
        mouseHook_ = nullptr;
    }
    HOOK_LIFE_LOG(L"ThreadProc: pump exited tid=%lu", wakeTid);
}

}  // namespace NextKey
