// VKey - Hook Lifecycle Implementation
// SPDX-License-Identifier: GPL-3.0-only

#include "HookLifecycle.h"
#include "core/CrashLog.h"
#include "core/Debug.h"

namespace NextKey {

// Mirrors the HookEngine.cpp prefix-only pattern: args not evaluated when the
// runtime logger is off, so HOOK_LOG in tight pump loops is near-zero cost.
#define HOOK_LIFE_LOG(fmt, ...) do {                                           \
    if (::NextKey::Logger::IsEnabled())                                        \
        ::NextKey::Logger::Log(L"[HookLife] " fmt, ##__VA_ARGS__);             \
} while (0)

// Custom WM_APP message ids used only inside this lifecycle's pump.
static constexpr UINT WM_APP_HOOK_COMMAND    = WM_APP + 2;
static constexpr UINT WM_APP_HOTKEY_FIRED    = WM_APP + 3;

HookLifecycle::HookLifecycle() = default;

HookLifecycle::~HookLifecycle() {
    Stop();
}

bool HookLifecycle::Start(HINSTANCE hInstance,
                           HOOKPROC keyboardProc,
                           HOOKPROC mouseProc,
                           DrainFn drainFn,
                           HotkeyDispatchFn hotkeyDispatchFn) {
    if (keyboardHook_) return false;  // Already running

    hInstance_     = hInstance;
    keyboardProc_  = keyboardProc;
    mouseProc_     = mouseProc;
    drainFn_       = std::move(drainFn);
    hotkeyDispatchFn_ = std::move(hotkeyDispatchFn);
    ready_.store(false, std::memory_order_release);

    thread_ = std::thread(&HookLifecycle::ThreadProc, this);

    // Wait for hook thread to finish installing hooks (or fail). Timeout 5s
    // as safety — a healthy thread signals within milliseconds.
    {
        std::unique_lock<std::mutex> lk(startMutex_);
        startCv_.wait_for(lk, std::chrono::seconds(5),
                          [this] { return ready_.load(std::memory_order_acquire); });
    }
    if (!keyboardHook_) {
        NEXTKEY_LOG(L"HookLifecycle: keyboard hook install failed (thread did not signal ready or SetWindowsHookExW failed)");
        if (thread_.joinable()) {
            const DWORD tid = threadId_.load(std::memory_order_acquire);
            if (tid) PostThreadMessage(tid, WM_QUIT, 0, 0);
            thread_.join();
        }
        return false;
    }
    return true;
}

void HookLifecycle::Stop() {
    if (thread_.joinable()) {
        const DWORD tid = threadId_.load(std::memory_order_acquire);
        if (tid) PostThreadMessage(tid, WM_QUIT, 0, 0);
        thread_.join();
    }
    keyboardHook_ = nullptr;
    mouseHook_    = nullptr;
    threadId_.store(0, std::memory_order_release);
    ready_.store(false, std::memory_order_release);
    drainFn_ = nullptr;
    hotkeyDispatchFn_ = nullptr;
    // #10: clear any stale wake latch / pending bits so that if this lifecycle
    // is restarted, the first cross-thread Post fires its wake instead of being
    // suppressed by a wakePosted_=true left over from a command the pump exited
    // before draining. No-op in the normal start-once flow.
    mailbox_.ResetLatch();
}

void HookLifecycle::PostHotkey(std::size_t slot) noexcept {
    const DWORD tid = threadId_.load(std::memory_order_acquire);
    if (tid) PostThreadMessageW(tid, WM_APP_HOTKEY_FIRED,
                                static_cast<WPARAM>(slot), 0);
}

void HookLifecycle::ThreadProc() {
    // Dedicated message-pump thread for WH_KEYBOARD_LL + WH_MOUSE_LL. These
    // are installer-thread-bound — the callback runs on this thread, and
    // Windows dispatches events via the thread's message queue. Keeping
    // this thread otherwise idle guarantees the pump stays responsive
    // within the LowLevelHooksTimeout window (silent-unhook avoidance).
    threadId_.store(GetCurrentThreadId(), std::memory_order_release);

    // Phase 2a: wire the mailbox wake trampoline now that we own a valid
    // thread id. Producers on other threads call mailbox_.Post(...); the
    // first post per empty→non-empty edge fires this lambda which kicks
    // the pump via WM_APP_HOOK_COMMAND. Subsequent posts in the same edge
    // coalesce (wakePosted latch).
    const DWORD wakeTid = threadId_.load(std::memory_order_acquire);
    mailbox_.SetWakeFn([wakeTid]{
        PostThreadMessageW(wakeTid, WM_APP_HOOK_COMMAND, 0, 0);
    });

    keyboardHook_ = SetWindowsHookExW(WH_KEYBOARD_LL, keyboardProc_, hInstance_, 0);
    if (!keyboardHook_) {
        HOOK_LIFE_LOG(L"ThreadProc: keyboard-hook installation FAILED err=%lu", GetLastError());
        // Signal Start() that we tried (but failed) so it can observe
        // keyboardHook_ == nullptr and abort.
        {
            std::lock_guard<std::mutex> lk(startMutex_);
            ready_.store(true, std::memory_order_release);
        }
        startCv_.notify_one();
        return;
    }

    mouseHook_ = SetWindowsHookExW(WH_MOUSE_LL, mouseProc_, hInstance_, 0);
    // Mouse hook is best-effort — proceed even if it fails.

    // Signal main: hooks installed, HHOOKs visible via keyboardHook_/mouseHook_.
    {
        std::lock_guard<std::mutex> lk(startMutex_);
        ready_.store(true, std::memory_order_release);
    }
    startCv_.notify_one();

    HOOK_LIFE_LOG(L"ThreadProc: pump started tid=%lu", wakeTid);

    // Message pump. Besides LL hook dispatch, this thread services:
    //  • WM_APP_HOOK_COMMAND — mailbox wake-up; invokes drainFn_ provided by
    //    HookEngine. Drain is ALSO called from inside LowLevelKeyboardProc
    //    (step 5 barrier in HookEngine), so reaching it here means no
    //    keystroke triggered a drain between the post and this pump cycle.
    //  • WM_APP_HOTKEY_FIRED — HookEngine's passive matcher posted a slot id
    //    in wParam. hotkeyDispatchFn_ invokes the per-slot callback in pump
    //    context, restoring the single-writer invariant for callbacks that
    //    call HookEngine::CommitPending().
    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        if (msg.message == WM_APP_HOOK_COMMAND) {
            if (drainFn_) {
                try {
                    drainFn_();
                } catch (const std::exception& e) {
                    CrashLog(L"HookLifecycle::DrainCallback", e.what());
                } catch (...) {
                    CrashLog(L"HookLifecycle::DrainCallback", "(non-std exception)");
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
                    CrashLog(L"HookLifecycle::DispatchHotkey", "(non-std exception)");
                }
            }
            continue;
        }
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    // Must unhook on the same thread that installed (MSDN requirement).
    if (keyboardHook_) {
        UnhookWindowsHookEx(keyboardHook_);
        keyboardHook_ = nullptr;
    }
    if (mouseHook_) {
        UnhookWindowsHookEx(mouseHook_);
        mouseHook_ = nullptr;
    }
    HOOK_LIFE_LOG(L"ThreadProc: pump exited tid=%lu", wakeTid);
}

}  // namespace NextKey
