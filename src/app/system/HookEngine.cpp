// VKey - Keyboard Hook Engine Implementation
// SPDX-License-Identifier: GPL-3.0-only

#include "HookEngine.h"
#include "Win32CaseMapper.h"
#include "PerfHistogram.h"  // Phase 1 — per-stage histogram (compiles to no-op when VKEY_PERF_HIST undef)
#include "helpers/AppHelpers.h"
#include "output/OutputInjectorFactory.h"  // Sprint 2 T3 — output channel strategy
#include "output/Internal.h"  // Sprint 2 D5 — g_synthCounterCallback bridge
#include "core/engine/CodeTableConverter.h"
#include "core/engine/EngineFactory.h"
#include "core/config/ConfigManager.h"
#include "core/CjkSwitchDecision.h"
#include "core/CommitUndoExemption.h"
#include "core/DigitLedWordDecision.h"
#include "core/MacroCase.h"
#include "core/MacroPrefix.h"
#include "core/ipc/SharedStateManager.h"
#include "core/Debug.h"
#include "core/CrashLog.h"
#include <algorithm>
#include <cassert>
#include <cstdio>
#include <exception>
#include <tlhelp32.h>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace NextKey {

/// Custom thread message used between OnFocusChanged (sender, main thread)
/// and HookThreadProc (receiver, hook thread) to re-install LL hooks at
/// the top of the hook chain. Defined once to avoid duplication.
static constexpr UINT WM_APP_REINSTALL_HOOKS = WM_APP + 1;

/// Phase 2a — wake trampoline for `HookCommandMailbox`. Producers on main /
/// worker / hotkey threads call `mailbox_.Post(bit, ...)`; that fires this
/// message once per empty→non-empty edge to break the hook thread out of
/// `GetMessage` so it drains promptly. Subsequent posts before drain run
/// coalesce (no extra messages) per the wakePosted latch.
static constexpr UINT WM_APP_HOOK_COMMAND   = WM_APP + 2;

/// `WM_APP_REINSTALL_HOOKS` wParam — labels which trigger fired the reinstall.
/// Logged by HookThreadProc so field-collected logs can distinguish causes
/// (e.g. confirm whether Java-trigger reinstalls are frequent enough to
/// indicate jnativehook re-arming during a JVM session).
static constexpr WPARAM REINSTALL_REASON_CHROMIUM = 0;
static constexpr WPARAM REINSTALL_REASON_JAVA     = 1;

// ═══════════════════════════════════════════════════════════
// VKEY_ASSERT_HOOK_THREAD — Phase 2d single-writer invariant.
//
// Composition-state mutation entry points (ResetComposition,
// CommitComposition, ClearWordState, ReplaceComposition,
// ReplayCommittedChars, HandleAlphaKey, HandleBackspace,
// ApplyConfigOnHookThread) MUST run on the hook thread. The macro is
// debug-only (NDEBUG elides it) — Release builds pay nothing. Pre-Start
// is a free pass: hookThreadId_ is 0 until HookThreadProc claims it,
// and Start() legitimately runs composition setup on main before the
// hook thread spawns.
// ═══════════════════════════════════════════════════════════
#ifdef NDEBUG
  #define VKEY_ASSERT_HOOK_THREAD() ((void)0)
#else
  #define VKEY_ASSERT_HOOK_THREAD()                                            \
      do {                                                                     \
          const DWORD _expected = hookThreadId_;                                \
          if (_expected != 0) {                                                 \
              const DWORD _current = GetCurrentThreadId();                      \
              if (_current != _expected) {                                      \
                  HOOK_LOG(L"VKEY_ASSERT_HOOK_THREAD violated: tid=%lu, expected hook tid=%lu — %hs", \
                           _current, _expected, __func__);                      \
                  assert(_current == _expected &&                               \
                         "composition-state mutation must run on the hook thread"); \
              }                                                                 \
          }                                                                     \
      } while (0)
#endif

// ═══════════════════════════════════════════════════════════
// HOOK_LOG → unified runtime-gated Logger (core/Logger.h).
// Enable from Settings → System → "Bật debug log". Output file is shared
// with NEXTKEY_LOG / TSF_LOG: VKey_<process>_<pid>.log next to
// VKeyApp.exe (falls back to %APPDATA%\VKey\logs\ if install dir is
// read-only). Flushing/closing is owned by the Logger (DLL detach + EXE
// process exit) — hook Start/Stop does NOT toggle the logger lifecycle.
// ═══════════════════════════════════════════════════════════
#define HOOK_LOG(fmt, ...) do {                                              \
    if (::NextKey::Logger::IsEnabled())                                      \
        ::NextKey::Logger::Log(L"[Hook] " fmt, ##__VA_ARGS__);               \
} while (0)

std::atomic<HookEngine*> HookEngine::s_instance{nullptr};

HookEngine::HookEngine() {
    // Sprint 2 T3: seed injector_ with the default Win32 impl so the hook
    // hot path's std::atomic_load(&injector_) never returns nullptr — even
    // before the first OnFocusChanged classifies the foreground window.
    // The default classification (all flags false) maps to
    // Win32SendInputInjector(needsBaitCharPrefix=false), the safest
    // mechanism (batch SendInput, no Sleep, no SendMessage).
    injector_.store(NextKey::Output::Create({}), std::memory_order_release);
}

HookEngine::~HookEngine() {
    Stop();
}

void HookEngine::CommitPending() {
    std::lock_guard<std::mutex> _lock(stateMutex_);
    if (engine_ && engine_->Count() > 0) {
        CommitComposition();
    }
}

// REQUIRES: caller holds stateMutex_. Sprint 1 D11 removed the self-lock so
// the std::mutex transition doesn't deadlock through the
// QuickSyncFromSharedState → ApplyConfig and ReloadFromToml → ApplyConfig
// recursive paths. Direct callers: Start (single-threaded init, no race),
// QuickSyncFromSharedState (locked), ReloadFromToml (caller-locked).
void HookEngine::ApplyConfig(const TypingConfig& config) {
    beepOnSwitch_ = config.beepOnSwitch;
    smartSwitch_ = config.smartSwitch;
    excludeApps_ = config.excludeApps;
    tsfApps_ = config.tsfApps;
    cjkAutoSwitch_ = config.cjkAutoSwitch;
    autoCaps_.store(config.autoCaps, std::memory_order_release);
    macroEnabled_.store(config.macroEnabled, std::memory_order_release);
    macroInEnglish_.store(config.macroInEnglish, std::memory_order_release);
    autoCapsMacro_.store(config.autoCapsMacro, std::memory_order_release);
    // Runtime file-logger gate (Settings → System → "Bật debug log").
    ::NextKey::Logger::SetEnabled(config.debugLogEnabled);
}

void HookEngine::ApplyHotkeyRegistry(HotkeyRegistry registry) {
    // RCU publish — readers (hook hot path) pick up on next load(). Old
    // registry stays alive until any in-flight Matches() returns.
    hotkeys_.store(std::make_shared<const HotkeyRegistry>(std::move(registry)),
                   std::memory_order_release);
}

namespace {

// Map L/R modifier VK variants down to their canonical form so HotkeyRegistry
// triggers (stored as canonical VK_CONTROL/VK_MENU/VK_SHIFT/VK_LWIN) match
// hook events (which report VK_LCONTROL/VK_RCONTROL etc.).
[[nodiscard]] uint32_t CanonicalModifierVk(DWORD vk) noexcept {
    switch (vk) {
    case VK_LCONTROL: case VK_RCONTROL: return VK_CONTROL;
    case VK_LMENU:    case VK_RMENU:    return VK_MENU;
    case VK_LSHIFT:   case VK_RSHIFT:   return VK_SHIFT;
    case VK_RWIN:                       return VK_LWIN;  // collapse to one Win
    default:                            return vk;
    }
}

// Pack the cached modifier booleans into a HotkeyRegistry-style bitmask.
[[nodiscard]] uint32_t ComputeModMask(bool ctrl, bool shift, bool alt, bool win) noexcept {
    uint32_t mask = 0;
    if (ctrl)  mask |= kModCtrl;
    if (shift) mask |= kModShift;
    if (alt)   mask |= kModAlt;
    if (win)   mask |= kModWin;
    return mask;
}

// Canonical-VK → modTapCount_[] / modTapLastTs_[] index. Returns -1 for any
// VK that isn't one of the 4 modifiers we track. Keeps the slots stable so
// the array can be a flat fixed-size buffer.
[[nodiscard]] int ModIdxFor(uint32_t canonicalVk) noexcept {
    switch (canonicalVk) {
    case VK_CONTROL: return 0;
    case VK_SHIFT:   return 1;
    case VK_MENU:    return 2;
    case VK_LWIN:    return 3;
    default:         return -1;
    }
}

}  // namespace

bool HookEngine::Start(HINSTANCE hInstance, const TypingConfig& config,
                        bool initialVietnamese, uint8_t startupMode) {
    if (keyboardHook_) return false;  // Already running

    // Enable the file logger before the first HOOK_LOG so the start banner is
    // captured when the user already had the toggle on. ApplyConfig() re-asserts
    // this below for subsequent config reloads.
    ::NextKey::Logger::SetEnabled(config.debugLogEnabled);
    HOOK_LOG(L"=== HookEngine::Start ===");

    s_instance = this;
    // Sprint 2 D5: route the IOutputInjector → Internal::TrackedSendInput
    // event count back into synthEventsPending_. Wired AFTER s_instance
    // is set (callback dereferences it). Hook thread isn't installed yet
    // so no synth dispatch can fire before this point.
    NextKey::Output::Internal::g_synthCounterCallback = &HookEngine::OnSynthDispatched;
    currentMethod_.store(config.inputMethod, std::memory_order_release);
    config_.store(std::make_shared<const TypingConfig>(config), std::memory_order_release);
    // Sprint 1 D11: ApplyConfig requires caller-held stateMutex_. Start runs
    // single-threaded (hookThread_ not yet spawned, no Settings dialog yet),
    // so the lock is defensive — it documents the ApplyConfig contract.
    {
        std::lock_guard<std::mutex> _lock(stateMutex_);
        ApplyConfig(config);
    }
    // Load unified hotkey registry. On first launch after v3 upgrade, the
    // `[[hotkeys]]` section is missing — migrate reads legacy `[features]`
    // toggles directly from TOML and persists `[hotkey_state]` so future
    // launches read the new schema directly.
    ApplyHotkeyRegistry(ConfigManager::MigrateLegacyHotkeysIfNeeded(
        ConfigManager::GetConfigPath()));
    autoCapState_ = AutoCapState::Idle;
    engine_ = EngineFactory::Create(config);
    vietnameseMode_.store(initialVietnamese, std::memory_order_release);
    startupMode_ = startupMode;

    // Create shared memory for smart switch and load persisted English-mode apps
    if (smartSwitch_) {
        (void)smartSwitchMgr_.Create();
        if (startupMode_ == 2) {  // Remember: load persisted per-app modes
            auto englishApps = ConfigManager::LoadEnglishModeApps(ConfigManager::GetConfigPath());
            for (auto& app : englishApps) {
                appModeMap_[std::move(app)] = false;  // false = English mode
            }
            if (!appModeMap_.empty()) {
                smartSwitchMgr_.LoadFromMap(appModeMap_);
            }
        }
    }

    currentCodeTable_ = config.codeTable;
    globalCodeTable_ = config.codeTable;
    globalInputMethod_ = config.inputMethod;

    // Cache initial SharedState values (pointer set by main.cpp via SetSharedStateReader)
    if (sharedStatePtr_) {
        SharedState state = sharedStatePtr_->Read();
        if (state.IsValid()) {
            lastFeatureFlags_ = state.GetFeatureFlags();
            lastSpellCheck_ = state.spellCheck;
            lastInputMethod_ = state.inputMethod;
            lastCodeTable_ = state.codeTable;
            lastConfigGeneration_ = state.configGeneration;
        }
    }

    // Phase 3d — single rebuild: TOML parse for overrides/excluded/TSF/
    // macros + atomic snapshot publish. Replaces the four legacy
    // Reload* + PublishConfigSnapshot calls from earlier.
    RebuildSnapshotFromToml(static_cast<std::uint32_t>(lastConfigGeneration_));

    // Spawn dedicated hook thread that owns keyboardHook_ + mouseHook_ and runs
    // its own GetMessage pump. This decouples LL hook dispatch from the main/UI
    // thread (which runs Sciter rendering, SharedState locks, config reloads).
    // Win10+ silently removes LL hooks whose installer-thread pump can't service
    // events within `LowLevelHooksTimeout` (default 300 ms, configurable up to
    // ~1000 ms via `HKCU\Control Panel\Desktop\LowLevelHooksTimeout`) — keeping
    // the hook thread minimal + dedicated avoids hitting that deadline.
    cachedHInstance_ = hInstance;
    hookThreadReady_.store(false);
    hookThread_ = std::thread(&HookEngine::HookThreadProc, this);

    // Wait for hook thread to finish installing hooks (or fail). Timeout 5s as
    // safety — a healthy thread signals within milliseconds.
    {
        std::unique_lock<std::mutex> lk(hookStartMutex_);
        hookStartCv_.wait_for(lk, std::chrono::seconds(5),
                              [this] { return hookThreadReady_.load(); });
    }
    if (!keyboardHook_) {
        NEXTKEY_LOG(L"HookEngine: Failed to install keyboard hook (hook thread returned without setting keyboardHook_)");
        HOOK_LOG(L"FAILED to install keyboard hook (hook thread did not signal ready or SetWindowsHookExW failed)");
        if (hookThread_.joinable()) {
            if (hookThreadId_) PostThreadMessage(hookThreadId_, WM_QUIT, 0, 0);
            hookThread_.join();
        }
        return false;
    }

    // Install focus change hooks — two separate hooks for exact event targeting
    // (avoids receiving ~20 unrelated events in the 0x0003..0x0017 range).
    focusHook_ = SetWinEventHook(
        EVENT_SYSTEM_FOREGROUND, EVENT_SYSTEM_FOREGROUND,
        nullptr, WinEventProc,
        0, 0, WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS);
    // MINIMIZEEND: restoring a window from the taskbar may not fire FOREGROUND
    // (taskbar gets the foreground event, filtered as Shell_TrayWnd).
    minimizeHook_ = SetWinEventHook(
        EVENT_SYSTEM_MINIMIZEEND, EVENT_SYSTEM_MINIMIZEEND,
        nullptr, WinEventProc,
        0, 0, WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS);

    // Sprint 1 D10: 200 ms focus / CJK poll is no longer driven by SetTimer.
    // The owning EXE wires MainThreadWorker::SetTickHandler([](){ OnTickPoll(); })
    // and SetTickInterval(200ms); Start does not own the cadence anymore.

    // Phase 1 perf histogram (docs/plans/2026-05-19-architecture-review-design.md).
    // Path: %APPDATA%\VKey\perf-histogram-<pid>-<startTs>.log. The Enabled() gate
    // is sourced from SharedState.diagFlags inside QuickSyncFromSharedState; we
    // seed it here from the TOML toggle so the very first keystroke is captured.
    {
        wchar_t pathBuf[MAX_PATH];
        const DWORD startTs = GetTickCount();
        const DWORD pid = GetCurrentProcessId();
        const std::wstring base = ConfigManager::GetAppDataDirectory();
        const int n = swprintf_s(pathBuf, MAX_PATH,
            L"%ls\\perf-histogram-%lu-%lu.log", base.c_str(), pid, startTs);
        if (n > 0) {
            Perf::Histogram::SetLogPath(pathBuf);
        }
        Perf::Histogram::SetEnabled(config.perfHistogramEnabled);
    }

    NEXTKEY_LOG(L"HookEngine started (method=%d, vietnamese=%d)",
                static_cast<int>(currentMethod_.load(std::memory_order_acquire)),
                vietnameseMode_.load(std::memory_order_acquire));
    HOOK_LOG(L"Hook installed OK (method=%d, vietnamese=%d)",
             static_cast<int>(currentMethod_.load(std::memory_order_acquire)),
             vietnameseMode_.load(std::memory_order_acquire));
    return true;
}

void HookEngine::Stop() {
    HOOK_LOG(L"=== HookEngine::Stop ===");
    // Phase 1 perf histogram: final flush before we tear down so the
    // last 60s window of samples reaches disk. Idempotent.
    Perf::Histogram::Stop();
    // Persist smart switch English-mode apps to TOML before shutdown
    if (startupMode_ == 2) {  // Remember: persist per-app modes
        SaveEnglishModeAppsIfDirty();
    }
    // Ask hook thread to exit (it owns keyboardHook_/mouseHook_ and will
    // UnhookWindowsHookEx them on the same thread that installed — required by
    // LL hook semantics). WinEvent hooks + timer stay on main thread.
    if (hookThread_.joinable()) {
        if (hookThreadId_) PostThreadMessage(hookThreadId_, WM_QUIT, 0, 0);
        hookThread_.join();
    }
    keyboardHook_ = nullptr;
    mouseHook_ = nullptr;
    hookThreadId_ = 0;
    hookThreadReady_.store(false);

    if (focusHook_) {
        UnhookWinEvent(focusHook_);
        focusHook_ = nullptr;
    }
    if (minimizeHook_) {
        UnhookWinEvent(minimizeHook_);
        minimizeHook_ = nullptr;
    }
    // Sprint 1 D10: focusPollTimer_ retired — owner stops its
    // MainThreadWorker (which owns the 200 ms tick) before us.
    // Sprint 2 D5: clear the synth-counter callback BEFORE nulling
    // s_instance — otherwise an in-flight Internal::TrackedSendInput
    // could dereference s_instance after we cleared it.
    NextKey::Output::Internal::g_synthCounterCallback = nullptr;
    if (s_instance == this) {
        s_instance = nullptr;
    }
    layoutSuppressed_ = false;
    cachedIsCompatLayout_ = true;
    NEXTKEY_LOG(L"HookEngine stopped");
}

void HookEngine::HookThreadProc() {
    // Dedicated message-pump thread for WH_KEYBOARD_LL + WH_MOUSE_LL. These are
    // installer-thread-bound — the callback runs on this thread, and Windows
    // dispatches events via the thread's message queue. Keeping this thread
    // otherwise idle guarantees the pump stays responsive within the
    // LowLevelHooksTimeout window (silent-unhook avoidance).
    hookThreadId_ = GetCurrentThreadId();

    // Phase 2a: wire the mailbox wake trampoline now that we own a valid
    // thread id. Producers on other threads call mailbox_.Post(...); the
    // first post per empty→non-empty edge fires this lambda which kicks
    // the pump via WM_APP_HOOK_COMMAND. Subsequent posts in the same edge
    // coalesce (wakePosted latch). Captured `this` is safe — mailbox is
    // a member, lifetime is HookEngine's.
    const DWORD wakeTid = hookThreadId_;
    mailbox_.SetWakeFn([wakeTid]{
        PostThreadMessageW(wakeTid, WM_APP_HOOK_COMMAND, 0, 0);
    });

    keyboardHook_ = SetWindowsHookExW(WH_KEYBOARD_LL, LowLevelKeyboardProc, cachedHInstance_, 0);
    if (!keyboardHook_) {
        HOOK_LOG(L"HookThreadProc: SetWindowsHookExW(WH_KEYBOARD_LL) FAILED err=%lu", GetLastError());
        // Signal main thread that we tried (but failed) so it can observe
        // keyboardHook_ == nullptr and abort Start().
        {
            std::lock_guard<std::mutex> lk(hookStartMutex_);
            hookThreadReady_.store(true);
        }
        hookStartCv_.notify_one();
        return;
    }

    mouseHook_ = SetWindowsHookExW(WH_MOUSE_LL, LowLevelMouseProc, cachedHInstance_, 0);
    // Mouse hook is best-effort — proceed even if it fails.

    // Signal main: hooks installed, HHOOKs visible via keyboardHook_/mouseHook_.
    {
        std::lock_guard<std::mutex> lk(hookStartMutex_);
        hookThreadReady_.store(true);
    }
    hookStartCv_.notify_one();

    HOOK_LOG(L"HookThreadProc: pump started tid=%lu", hookThreadId_);

    // Message pump. Besides LL hook dispatch, this thread services
    // WM_APP_REINSTALL_HOOKS posted by OnFocusChanged for Chromium / Java
    // top-of-chain priority. wParam carries REINSTALL_REASON_* (see top of file).
    //
    // Reinstalls are throttled (`kMinReinstallIntervalMs`) so a burst of
    // focus events (Alt-Tab through several Chromium/Java windows in
    // succession) doesn't translate into a burst of unhook/rehook gaps.
    // Each gap is microseconds-to-ms, so a single one is harmless — but
    // five back-to-back can swallow a stray keystroke. Pending duplicate
    // messages collapse into the throttle check.
    MSG msg;
    DWORD lastReinstallTime = 0;
    constexpr DWORD kMinReinstallIntervalMs = 500;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        if (msg.message == WM_APP_REINSTALL_HOOKS) {
            const wchar_t* reasonName =
                msg.wParam == REINSTALL_REASON_JAVA ? L"java" : L"chromium";
            const DWORD now = GetTickCount();
            const DWORD sinceLast = now - lastReinstallTime;
            if (lastReinstallTime != 0 && sinceLast < kMinReinstallIntervalMs) {
                HOOK_LOG(L"HookThreadProc: reinstall SKIPPED (throttle %ums < %ums) reason=%ls",
                         sinceLast, kMinReinstallIntervalMs, reasonName);
                continue;
            }
            lastReinstallTime = now;

            // Unhook before re-install. Don't gate the re-install on the
            // unhook target existing — if a prior reinstall transient-failed
            // and left a NULL handle, we still want to attempt recovery
            // (gating would lock out retry permanently).
            if (keyboardHook_) UnhookWindowsHookEx(keyboardHook_);
            keyboardHook_ = SetWindowsHookExW(WH_KEYBOARD_LL, LowLevelKeyboardProc, cachedHInstance_, 0);
            if (!keyboardHook_) {
                HOOK_LOG(L"HookThreadProc: SetWindowsHookExW(WH_KEYBOARD_LL) reinstall FAILED err=%lu",
                         GetLastError());
            }

            if (mouseHook_) UnhookWindowsHookEx(mouseHook_);
            mouseHook_ = SetWindowsHookExW(WH_MOUSE_LL, LowLevelMouseProc, cachedHInstance_, 0);
            if (!mouseHook_) {
                HOOK_LOG(L"HookThreadProc: SetWindowsHookExW(WH_MOUSE_LL) reinstall FAILED err=%lu",
                         GetLastError());
            }

            HOOK_LOG(L"HookThreadProc: Hooks reinstalled reason=%ls kb=%ls mouse=%ls",
                     reasonName,
                     keyboardHook_ ? L"OK" : L"FAIL",
                     mouseHook_ ? L"OK" : L"FAIL");
            continue;
        }
        if (msg.message == WM_APP_HOOK_COMMAND) {
            // Phase 2a: wake-up posted by mailbox_.Post on a non-hook thread.
            // The drain is also called from inside LowLevelKeyboardProc (step
            // 5 barrier), so reaching it here means no keystroke triggered a
            // drain between the post and the pump cycle — process the bits
            // promptly so focus/config updates aren't deferred to the next
            // keydown.
            try {
                DrainHookCommands();
            } catch (const std::exception& e) {
                CrashLog(L"HookThreadProc::DrainHookCommands", e.what());
            } catch (...) {
                CrashLog(L"HookThreadProc::DrainHookCommands", "(non-std exception)");
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
    HOOK_LOG(L"HookThreadProc: pump exited tid=%lu", hookThreadId_);
}

void HookEngine::ToggleVietnameseMode() {
    // Phase 2c: ToggleVietnameseMode is called from any thread (tray menu
    // on main, hotkey on either main or the hook pump itself when fired
    // via HotkeyRegistry, modifier-only double-tap). All composition-state
    // writes (CommitComposition, vietnameseMode_, appModeMap_, etc.) must
    // happen on the hook thread (Rule 11.3 single-writer). Post the bit
    // and let the drain do the work.
    mailbox_.Post(HookCommand::kToggleVN);
}

void HookEngine::SetCodeTable(CodeTable ct) {
    std::lock_guard<std::mutex> _lock(stateMutex_);
    // Commit any pending composition before switching
    if (ct != currentCodeTable_ && engine_->Count() > 0) {
        CommitComposition();
    }

    currentCodeTable_ = ct;

}

CodeTable HookEngine::GetCodeTable() const noexcept {
    // Priority 1: Manual per-app override (set explicitly by user)
    // Check previousExe_ first as a fallback: on the first focus event after startup,
    // currentExe_ may not yet reflect the typing app.
    // Phase 3c: read from RCU snapshot — lock-free, safe on any thread.
    auto snap = configSnapshot_.load(std::memory_order_acquire);
    auto lookupOverride = [&](const std::wstring& exe) -> const CodeTable* {
        if (exe.empty() || !snap) return nullptr;
        auto it = snap->appEncodingOverrides.find(exe);
        return (it != snap->appEncodingOverrides.end()) ? &it->second : nullptr;
    };
    if (auto* v = lookupOverride(previousExe_)) return *v;
    if (auto* v = lookupOverride(currentExe_))  return *v;

    return currentCodeTable_;
}

void HookEngine::QuickSyncFromSharedState() {
    // Pre-T3 Minor 2 fix (Rule #11.3): hot path is lock-free. The common
    // case — no SharedState change since the last call — returns before
    // any mutex acquire, eliminating the per-keystroke contention with
    // main-thread writers (ToggleVietnameseMode, SetCodeTable, …) that
    // showed up as p99 jitter under chaos. Slow path still takes the
    // lock and re-checks the epoch under it (double-checked locking) so
    // hook ↔ main both detecting a bump are serialised cleanly.
    //
    // Sprint 1 D11 contract preserved: callers must NOT hold stateMutex_.
    // OnTickPoll releases its lock before calling OnFocusChanged, so the
    // inner OnFocusChanged → QuickSync chain reaches the slow-path lock
    // without recursion. ProcessKeyDown (hook thread) and
    // SyncConfigFromSharedState (public API) call this without any lock
    // held.
    if (!sharedStatePtr_) return;

    // Fast path: lock-free atomic epoch check. SharedState::ReadEpoch is
    // a memory-mapped 32-bit seqlock counter; lastEpoch_ is std::atomic.
    // Common case under steady-state typing: epoch unchanged → return
    // without any stateMutex_ acquire. Cost: ~5 ns total.
    uint32_t epoch = sharedStatePtr_->ReadEpoch();
    uint32_t seenEpoch = lastEpoch_.load(std::memory_order_acquire);
    if (epoch == seenEpoch && (epoch & 1) == 0) return;

    // Slow path: SharedState may have changed. Acquire the lock to
    // serialise with main-thread writers (ApplyConfig, ReloadFromToml).
    std::lock_guard<std::mutex> _lock(stateMutex_);

    // Double-check inside the lock — a concurrent QuickSync caller (hook
    // ↔ main race on configGeneration bump) may have already applied
    // this epoch. Without the recheck both threads would run the full
    // body and the second one would no-op only after wasted TOML reload.
    epoch = sharedStatePtr_->ReadEpoch();
    seenEpoch = lastEpoch_.load(std::memory_order_acquire);
    if (epoch == seenEpoch && (epoch & 1) == 0) return;

    SharedState state = sharedStatePtr_->Read();
    if (!state.IsValid()) return;
    lastEpoch_.store(state.epoch, std::memory_order_release);

    // Phase 1: surface SharedState.diagFlags bit 0 into the perf histogram
    // gate. Atomic store — Histogram::SetEnabled holds no lock and is safe to
    // call inside this slow-path block (already serialised by stateMutex_).
    Perf::Histogram::SetEnabled((state.diagFlags & DiagFlags::PERF_HISTOGRAM) != 0);

    // ── Config generation check: detect TOML changes from Settings/subdialogs ──
    // When configGeneration changes, do a full TOML reload (macros, excluded apps, etc.).
    // Replaces the old ConfigEvent (Named Event + WaitForSingleObject syscall).
    //
    // Phase 3c thread-aware routing:
    //   • Hook thread → defer to worker (Rule 11.2 — TOML parse is forbidden
    //     here, ~1-10 ms). Set `pendingConfigReload_`; the next OnTickPoll
    //     drains it and runs ReloadFromToml on the worker thread.
    //   • Worker / main → run inline. Already on a thread where TOML parse
    //     is acceptable, no point bouncing through another tick.
    if (state.configGeneration != lastConfigGeneration_) {
        if (hookThreadId_ != 0 && GetCurrentThreadId() == hookThreadId_) {
            pendingConfigReload_.store(true, std::memory_order_release);
            NEXTKEY_LOG(L"HookEngine: configGeneration bump (%u) seen on hook — deferring Reload to worker tick",
                        state.configGeneration);
        } else {
            lastConfigGeneration_ = state.configGeneration;
            NEXTKEY_LOG(L"HookEngine: configGeneration changed (%u), full TOML reload", state.configGeneration);
            ReloadFromToml();
        }
    }

    uint32_t ff = state.GetFeatureFlags();
    uint8_t sc = state.spellCheck;
    uint8_t im = state.inputMethod;
    uint8_t ct = state.codeTable;

    // No change → no-op (cheap: integer compares on mapped memory)
    if (ff == lastFeatureFlags_ && sc == lastSpellCheck_ &&
        im == lastInputMethod_ && ct == lastCodeTable_) return;
    lastFeatureFlags_ = ff;
    lastSpellCheck_ = sc;
    lastInputMethod_ = im;
    lastCodeTable_ = ct;

    NEXTKEY_LOG(L"HookEngine: SharedState changed (ff=0x%04X, spell=%d, method=%d, ct=%d)", ff, sc, im, ct);

    TypingConfig cfg = *config_.load(std::memory_order_acquire);
    DecodeFeatureFlags(ff, cfg);
    cfg.spellCheckEnabled = sc != 0;
    cfg.inputMethod = static_cast<InputMethod>(im);
    cfg.codeTable = static_cast<CodeTable>(ct);

    bool methodChanged = (currentMethod_.load(std::memory_order_acquire) != cfg.inputMethod);
    bool codeTableChanged = (currentCodeTable_ != cfg.codeTable);
    ApplyConfig(cfg);
    config_.store(std::make_shared<const TypingConfig>(cfg), std::memory_order_release);

    // P3e fix: defer engine recreate to ApplyConfigOnHookThread. QuickSync's
    // slow path runs on whichever thread called it (worker via OnTickPoll →
    // OnFocusChanged, or main via SyncConfigFromSharedState). The engine
    // swap + CommitComposition must run on the hook thread to avoid the
    // race that surfaced under `-InjectConfigReloadMs 50` chaos.
    if (methodChanged) {
        mailbox_.Post(HookCommand::kConfigApply);
    }

    if (codeTableChanged) {
        currentCodeTable_ = cfg.codeTable;
        globalCodeTable_ = cfg.codeTable;
    }

    {
        // Phase 3d: macroEnabled toggled but configGeneration didn't bump
        // (typical case — user flips the macro feature switch). Compare
        // the snapshot's macro presence against the new desired state;
        // if they disagree, rebuild + republish. On the hook thread this
        // path is now deferred via pendingConfigReload_ (same as the
        // configGeneration-bump path) so TOML parse stays off-hook.
        const bool macroOn = macroEnabled_.load(std::memory_order_acquire);
        auto snap = configSnapshot_.load(std::memory_order_acquire);
        const bool snapHasMacros = snap && !snap->macroTable.empty();
        if (macroOn != snapHasMacros) {
            if (hookThreadId_ != 0 && GetCurrentThreadId() == hookThreadId_) {
                pendingConfigReload_.store(true, std::memory_order_release);
            } else {
                RebuildSnapshotFromToml(static_cast<std::uint32_t>(lastConfigGeneration_));
            }
        }
    }
}

void HookEngine::SyncConfigFromSharedState() {
    QuickSyncFromSharedState();
}

void HookEngine::ReloadFromToml() {
    PERF_SCOPE(::NextKey::Perf::Stage::ConfigReload);
    NEXTKEY_LOG(L"HookEngine: full TOML reload");

    // Read TOML for fields not in SharedState (beep, smartSwitch, excludeApps, hotkey)
    auto config = ConfigManager::LoadOrDefault();

    // Override with SharedState for fields that Settings updates immediately
    // (TOML may be stale due to deferred save)
    if (sharedStatePtr_) {
        SharedState state = sharedStatePtr_->Read();
        if (state.IsValid()) {
            config.inputMethod = static_cast<InputMethod>(state.inputMethod);
            config.spellCheckEnabled = state.spellCheck != 0;
            DecodeFeatureFlags(state.GetFeatureFlags(), config);
            NEXTKEY_LOG(L"HookEngine: read SharedState (epoch=%u, featureFlags=0x%04X)",
                        state.epoch, state.GetFeatureFlags());
        }
    }

    // P3e fix — single-writer for `engine_`. Pre-P3e, this function called
    // CommitComposition + `engine_ = EngineFactory::Create(...)` inline.
    // Post-P3c, ReloadFromToml runs on the worker thread (Rule 11.2 forbids
    // TOML parse on hook), so the inline engine swap raced against the hook
    // hot path's `engine_->Peek/Push/Count` reads — UAF discovered by
    // run-chaos.ps1 -InjectConfigReloadMs 50 (5×11 failures: composition
    // state lost mid-word). Defer both the commit AND the engine recreate
    // to ApplyConfigOnHookThread; the hook drain runs them between
    // keystrokes where they're single-writer safe.
    config_.store(std::make_shared<const TypingConfig>(config), std::memory_order_release);
    ApplyConfig(config);
    // Reload `[[hotkeys]]` from TOML alongside main config — keeps registry in
    // sync when Settings dialog persists rebindings via SaveHotkeyRegistry.
    // Still runs migration (idempotent — no-op if section already populated).
    ApplyHotkeyRegistry(ConfigManager::MigrateLegacyHotkeysIfNeeded(
        ConfigManager::GetConfigPath()));

    currentCodeTable_ = config.codeTable;
    globalCodeTable_ = config.codeTable;
    globalInputMethod_ = config.inputMethod;

    // Phase 3d — one helper does it all: TOML parse for overrides /
    // excluded apps / TSF apps / macros, ConfigSnapshot::Build (derives
    // spaceMacroKeys), atomic publish. The re-evaluate block below reads
    // the freshly-published snapshot for the current-app fields.
    RebuildSnapshotFromToml(static_cast<std::uint32_t>(lastConfigGeneration_));
    auto rcuSnap = configSnapshot_.load(std::memory_order_acquire);

    // Re-evaluate excluded status for current app (set was just reloaded)
    bool newExcluded = false;
    if (excludeApps_ && !currentExe_.empty() && rcuSnap) {
        newExcluded = rcuSnap->excludedAppSet.count(currentExe_) > 0;
        isExcludedApp_.store(newExcluded, std::memory_order_release);
    } else {
        newExcluded = isExcludedApp_.load(std::memory_order_acquire);
    }

    // Re-evaluate TSF app status for current foreground app
    const bool wasTsfApp = isTsfApp_.load(std::memory_order_acquire);
    bool newTsfApp;
    if (tsfApps_ && !newExcluded && rcuSnap && !rcuSnap->tsfAppSet.empty() && !currentExe_.empty()) {
        newTsfApp = rcuSnap->tsfAppSet.count(currentExe_) > 0;
    } else {
        newTsfApp = false;
    }
    isTsfApp_.store(newTsfApp, std::memory_order_release);
    HOOK_LOG(L"  Engine (config reload): %s for '%s' (tsf_feature=%d, in_tsf_list=%d, excluded=%d)",
             newTsfApp ? L"TSF (hook passthrough)" : L"HOOK",
             currentExe_.c_str(),
             tsfApps_ ? 1 : 0,
             (rcuSnap && !currentExe_.empty() && rcuSnap->tsfAppSet.count(currentExe_) > 0) ? 1 : 0,
             newExcluded ? 1 : 0);
    if (tsfModeCallback_) {
        const bool tsfReadonly = !newTsfApp && !newExcluded;
        if (newTsfApp != wasTsfApp) {
            HOOK_LOG(L"  TSF_ACTIVE flag: %s → %s",
                     wasTsfApp ? L"true" : L"false", newTsfApp ? L"true" : L"false");
        }
        tsfModeCallback_(newTsfApp, tsfReadonly);
    }

    // Re-apply per-app encoding override for current app. Encoding is a
    // plain enum (`CodeTable`) read on the hook hot path without locking;
    // a worker-side write is a torn-read risk but NOT a UAF — minor
    // staleness window only. Acceptable for an enum-sized field.
    if (!currentExe_.empty() && !newExcluded && !newTsfApp && rcuSnap) {
        auto it = rcuSnap->appEncodingOverrides.find(currentExe_);
        currentCodeTable_ = (it != rcuSnap->appEncodingOverrides.end())
            ? it->second : globalCodeTable_;
    }
    // P3e fix — per-app inputMethod override engine recreate moved to
    // ApplyConfigOnHookThread (same race surface as the unconditional
    // recreate removed above). Worker thread cannot safely swap
    // `engine_` while hook hot path holds raw pointer reads.

    // Notify main process to reload hotkey / QuickConvert configs.
    // Main owns HotkeyManager slots and calls UpdateHotkey there.
    if (configReloadCallback_) {
        configReloadCallback_();
    }

    // P3e fix — post kConfigApply to the hook mailbox so the drain runs
    // ApplyConfigOnHookThread between keystrokes. This is the producer
    // for the dormant handler we wired in P2c — finally lit up. The
    // mailbox coalesces against rapid republishes (one Apply per drain
    // cycle) so chaos `-InjectConfigReloadMs 50` doesn't queue up many.
    mailbox_.Post(HookCommand::kConfigApply);
}

// ═══════════════════════════════════════════════════════════
// Static Hook Callbacks → Instance Dispatch
// ═══════════════════════════════════════════════════════════

LRESULT CALLBACK HookEngine::LowLevelKeyboardProc(int nCode, WPARAM wParam, LPARAM lParam) {
    // Phase 1: Tier 2 budget marker (<30ms p99). Wraps the full LL callback
    // body so the recorded delta includes every nested stage. PERF_SCOPE
    // compiles to (void)0 when VKEY_PERF_HIST is not defined.
    PERF_SCOPE(::NextKey::Perf::Stage::TotalKeydown);
    // `self` declared outside the try so the catch block can call
    // ResetComposition (Rule 11.5 — "ALWAYS reset state on exception"). Without
    // this, a throw escaping ProcessKeyDown leaves engine_/previousComposition_
    // in a half-updated state for the next keystroke. Re-load is cheap (atomic
    // load) and ResetComposition asserts hook-thread (which we are, here).
    HookEngine* self = s_instance.load(std::memory_order_relaxed);
    // Top-level catch: a C++ throw escaping a low-level hook unwinds through
    // KiUserCallbackDispatcher and Windows raises STATUS_FATAL_USER_CALLBACK_EXCEPTION
    // (0xC000041D), terminating the process. Swallow + log so the next keystroke
    // gets a fresh attempt instead of the app silently disappearing.
    try {
        auto* pKey = reinterpret_cast<KBDLLHOOKSTRUCT*>(lParam);

        // Always track our own synthetic events regardless of nCode.
        // When nCode < 0, Windows tells us to pass the message along — but the event
        // still represents a delivered synthetic that was counted when sent.
        // Without this, synthEventsPending_ leaks on every nCode < 0 delivery.
        if (self && pKey->dwExtraInfo == VKEY_EXTRA_INFO) {
            HOOK_LOG(L"  PASSTHRU (dwExtraInfo=NK): vk=0x%02X scan=0x%04X flags=0x%08X nCode=%d",
                     pKey->vkCode, pKey->scanCode, pKey->flags, nCode);
            if (self->synthEventsPending_ > 0) --self->synthEventsPending_;
            return CallNextHookEx(nullptr, nCode, wParam, lParam);
        }

        if (nCode == HC_ACTION && self) {
            // Skip events while we're sending (safety backup)
            if (self->sending_) {
                HOOK_LOG(L"  PASSTHRU (sending_): vk=0x%02X scan=0x%04X flags=0x%08X",
                         pKey->vkCode, pKey->scanCode, pKey->flags);
                return CallNextHookEx(nullptr, nCode, wParam, lParam);
            }

            // Rule 11.4 step 5 — drain cross-thread commands BEFORE the
            // English-mode / modifier-key dispatch chain so state mutations
            // posted by main / worker / hotkey threads land before this
            // keystroke is classified. Drain is cheap when nothing is
            // pending (one atomic load + one branch).
            //
            // Placement constraint: MUST come after sending_ (synthetic
            // events from injector_->Replace must not re-enter drain) and
            // BEFORE the English-mode passthrough so an in-flight V/E
            // toggle posted from the hotkey thread takes effect on the
            // very next keystroke, not the one after.
            self->DrainHookCommands();

            bool isDown = (wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN);
            bool isUp = (wParam == WM_KEYUP || wParam == WM_SYSKEYUP);

            HOOK_LOG(L"KEY vk=0x%02X scan=0x%04X flags=0x%08X %s",
                     pKey->vkCode, pKey->scanCode, pKey->flags,
                     isDown ? L"DOWN" : (isUp ? L"UP" : L"OTHER"));

            // REGRESSION TRAP — DO NOT UNCOMMENT
            //
            // Sprint 1 D4 originally took stateMutex_ here to guard the racing
            // reads of `engine_`, `previousComposition_`, app-detect flags, etc.
            // Phase B (D5-D11) replaced every reader/writer with std::atomic
            // + RCU patterns; the lock is no longer needed and the type
            // (`std::recursive_mutex`) was downgraded to `std::mutex` in D11
            // — uncommenting this line triggers a compile error which IS the
            // intentional regression trap. `tools/audit/check_hook_thread_no
            // _mutex.sh` Check 1 verifies this line stays commented (one of
            // 3 such lines across hook callbacks). If you're tempted to "clean
            // up" the dangling reference, read the audit script first.
            // std::lock_guard<std::recursive_mutex> _lock(self->stateMutex_);

            if (isDown) {
                if (self->ProcessKeyDown(pKey->vkCode, pKey->scanCode, pKey->flags)) {
                    HOOK_LOG(L"  → EATEN (key-down vk=0x%02X)", pKey->vkCode);
                    return 1;  // Eat the keystroke
                }
            } else if (isUp) {
                if (self->ProcessKeyUp(pKey->vkCode, pKey->flags)) {
                    return 1;  // Eat the keystroke
                }
            }
        }
    } catch (const std::exception& e) {
        CrashLog(L"HookEngine::LowLevelKeyboardProc", e.what());
        // Rule 11.5 safety net: an exception escaping ProcessKey* leaves the
        // engine + previousComposition + per-word flags in an undefined state.
        // ResetComposition clears them so the next keystroke starts fresh
        // instead of compounding the corruption.
        if (self) self->ResetComposition();
    } catch (...) {
        CrashLog(L"HookEngine::LowLevelKeyboardProc", "(non-std exception)");
        if (self) self->ResetComposition();
    }
    return CallNextHookEx(nullptr, nCode, wParam, lParam);
}

void CALLBACK HookEngine::WinEventProc(HWINEVENTHOOK, DWORD event, HWND hwnd, LONG, LONG, DWORD, DWORD) {
    try {
        HookEngine* self = s_instance.load(std::memory_order_relaxed);
        if (!self) return;

        // Per WINEVENT_OUTOFCONTEXT semantics, this callback fires on the
        // INSTALLER thread (main, where SetWinEventHook was called) — NOT
        // the hook thread, despite what older comments here used to claim.
        // Phase 2b makes that explicit by deferring all composition-state
        // writes to ApplyFocusOnHookThread via the mailbox. WinEventProc
        // is now classification-only: no engine_ access, no mutation of
        // autoCapState_ / previousComposition_ / per-app atomic flags.
        // (Accessing those from main would race with hook-thread writers
        // — Rule 11.3 violation.)

        if (event == EVENT_SYSTEM_MINIMIZEEND) {
            // Window restored from taskbar — re-evaluate focus with the actual foreground window.
            // Don't use hwnd directly: the restored window may not be foreground yet.
            HOOK_LOG(L"MINIMIZEEND (hwnd=%p) — re-evaluating focus", hwnd);
            self->OnFocusChanged(nullptr);  // nullptr → uses GetForegroundWindow()
            return;
        }

        HOOK_LOG(L"FOCUS changed (hwnd=%p) — classifying + posting", hwnd);
        self->OnFocusChanged(hwnd);
    } catch (const std::exception& e) {
        CrashLog(L"HookEngine::WinEventProc", e.what());
    } catch (...) {
        CrashLog(L"HookEngine::WinEventProc", "(non-std exception)");
    }
}

LRESULT CALLBACK HookEngine::LowLevelMouseProc(int nCode, WPARAM wParam, LPARAM lParam) {
    try {
        if (nCode == HC_ACTION && wParam == WM_LBUTTONDOWN) {
            HookEngine* self = s_instance.load(std::memory_order_relaxed);
            if (self) {
                // REGRESSION TRAP — DO NOT UNCOMMENT (see LowLevelKeyboardProc
                // above for the full rationale). Mouse path includes a writer
                // (ResetComposition); torn-read risk pre-Phase-B was higher
                // here than the keyboard read paths. Phase B replaced this
                // with atomic state — current cachedFocusedHwnd_ + Reset-
                // Composition write set is captured as Pre-T3 review Minor
                // 1 in docs/TODO.md (still-open audit). Audit Check 1
                // enforces this line stays commented.
                // std::lock_guard<std::recursive_mutex> _lock(self->stateMutex_);
                HOOK_LOG(L"MOUSE click — resetting composition (engine count=%zu, prev='%s')",
                         self->engine_->Count(), self->previousComposition_.c_str());
                // Always reset, even when engine is idle: commitUndoState_ and commitStack_
                // may hold a previously committed word. If not cleared here, a click elsewhere
                // followed by Backspace triggers ReplayCommittedChars() at the new cursor
                // position — identical to the Ctrl+A bug.
                self->ResetComposition();
                // Click may move focus to another control within the same app (no
                // EVENT_SYSTEM_FOREGROUND fires) — invalidate cache so the next
                // TryEditMessagePaste re-queries the focused HWND.
                self->cachedFocusedHwnd_.store(nullptr, std::memory_order_relaxed);
                self->cachedFocusedClass_.clear();  // see HWND comment in HookEngine.h: tuple race benign
            }
        }
    } catch (const std::exception& e) {
        CrashLog(L"HookEngine::LowLevelMouseProc", e.what());
    } catch (...) {
        CrashLog(L"HookEngine::LowLevelMouseProc", "(non-std exception)");
    }
    return CallNextHookEx(nullptr, nCode, wParam, lParam);
}

// Forward declarations for file-scope helpers used in ProcessKeyDown
static HWND GetInputTarget();
static bool IsIncompatibleLayout(HKL hkl);

// ═══════════════════════════════════════════════════════════
// Core Processing
// ═══════════════════════════════════════════════════════════

bool HookEngine::ProcessKeyDown(DWORD vkCode, DWORD /*scanCode*/, DWORD /*flags*/) {
    // H1b: top-of-pipeline guards extracted to RunTopGuards (steps 0/0b/1/1b/1c).
    // Behavior preserved byte-identical — see method comment for details.
    switch (RunTopGuards(vkCode)) {
        case KeyOutcome::Eat: return true;
        case KeyOutcome::Pass: return false;
        case KeyOutcome::Fallthrough: break;
    }

    // Non-modifier key pressed — invalidate modifier-only hotkey combo (any
    // pending double-tap chain on Ctrl/Shift/Alt/Win is now contaminated)
    otherKeyPressed_ = true;
    for (int i = 0; i < kModCount; ++i) modTapCount_[i] = 0;

    // Watchdog: reset synthEventsPending_ if stuck > 500ms.
    // Covers event loss in Electron/Console multi-process apps where synthetic
    // events can be dropped under heavy CPU load, causing cascading re-injection
    // and ghost characters.
    if (synthEventsPending_ > 0) {
        DWORD elapsed = GetTickCount() - lastSynthSendTime_;
        if (elapsed > 500) {
            HOOK_LOG(L"  watchdog: synthEventsPending_ reset from %d (stuck %ums)",
                     synthEventsPending_.load(), elapsed);
            synthEventsPending_ = 0;
        }
    }

    // 2c. Fast English exit — skip commit-undo step when no undo is pending.
    //      Commit-undo only applies to Vietnamese words (line 691 checks vietnameseMode_).
    //      When English mode + undo Idle + no English macros → nothing below applies.
    // Sprint 1 D5.2: hoist atomic config-flag loads to a single snapshot at the
    // top of the hot path. Same-thread within ProcessKeyDown — no need to re-load
    // (config writers run on main and cannot interleave a sub-ms hook callback).
    const bool vnMode = vietnameseMode_.load(std::memory_order_acquire);
    const bool macroOn = macroEnabled_.load(std::memory_order_acquire);
    const bool macroEng = macroInEnglish_.load(std::memory_order_acquire);
    if (!vnMode &&
        commitUndoState_ == CommitUndoState::Idle &&
        !(macroOn && macroEng)) {
        return false;
    }

    // 2d. Backspace-into-committed-word state machine (Idle/Ready/Primed).
    // H1a: body extracted to HandleCommitUndo. Returned outcome dictates whether
    // ProcessKeyDown short-circuits (Eat/Pass) or continues with subsequent
    // steps (Fallthrough). Behavior preserved byte-identical to pre-H1a.
    switch (HandleCommitUndo(vkCode, vnMode)) {
        case KeyOutcome::Eat: return true;
        case KeyOutcome::Pass: return false;
        case KeyOutcome::Fallthrough: break;
    }

    // Cache key states once per keystroke (GetKeyState is a snapshot, safe to
    // cache). Used by HandlePreDispatch (vnMode tracking) and DispatchKeyAction.
    const bool cachedShift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
    const bool cachedCapsLock = (GetKeyState(VK_CAPITAL) & 0x0001) != 0;
    const bool cachedCtrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
    const bool cachedAlt = (GetKeyState(VK_MENU) & 0x8000) != 0;
    const bool cachedWin = (GetKeyState(VK_LWIN) & 0x8000) != 0 || (GetKeyState(VK_RWIN) & 0x8000) != 0;

    // H1c: English-mode short-circuit + Vietnamese pre-dispatch tracking
    // (steps 3 / 3a-3d). Behavior preserved byte-identical.
    switch (HandlePreDispatch(vkCode, vnMode, macroOn, macroEng,
                              cachedShift, cachedCapsLock,
                              cachedCtrl, cachedAlt, cachedWin)) {
        case KeyOutcome::Eat: return true;
        case KeyOutcome::Pass: return false;
        case KeyOutcome::Fallthrough: break;
    }

    // H1c: action dispatch (steps 4b-10). Returns Eat or Pass for every code path.
    switch (DispatchKeyAction(vkCode, cachedShift, cachedCapsLock, cachedCtrl,
                              cachedAlt, cachedWin, macroOn)) {
        case KeyOutcome::Eat: return true;
        case KeyOutcome::Pass: return false;
        case KeyOutcome::Fallthrough: break;
    }

    return false;
}

// H1b: top-of-pipeline guards extracted from ProcessKeyDown steps 0/0b/1/1b/1c.
//
//  Step 0  — QuickSyncFromSharedState (atomic config epoch; see comment below).
//  Step 0b — TSF early-out: foreground app is in TSF list, hook does nothing.
//  Step 1  — Track modifier keys (LCTRL/RCTRL/LSHIFT/RSHIFT/LMENU/RMENU/LWIN/RWIN);
//            pass through without consumption (don't eat modifier keys themselves).
//  Step 1b — Toggle keys (CapsLock/NumLock/ScrollLock): pass through without
//            committing composition (CapsLock often pressed mid-word).
//  Step 1c — Excluded-app passthrough: same-PID short-circuit; different-PID
//            verifies via VerifyExcludedState; on cleared, NotifyModeChange and
//            fall through to normal processing for this keystroke.
//
// Behavior is byte-identical to the pre-extraction inline block. Returns:
//   Eat         → ProcessKeyDown returns true (no top guards do this today,
//                 reserved for future use).
//   Pass        → ProcessKeyDown returns false (TSF / modifier / toggle /
//                 still-excluded paths).
//   Fallthrough → continue with subsequent ProcessKeyDown steps (only when no
//                 guard matched, or excluded-app cleared its PID).
//
// The post-guard bookkeeping in ProcessKeyDown (otherKeyPressed_=true,
// modTapCount_[]=0, synth-pending watchdog) lives in the wrapper, not here, so it
// runs only on Fallthrough. The excluded-app same-PID and still-excluded paths
// set otherKeyPressed_ themselves before returning Pass, preserving the original
// "any non-modifier key invalidates the modifier-only combo" semantics.
HookEngine::KeyOutcome HookEngine::RunTopGuards(DWORD vkCode) {
    PERF_SCOPE(::NextKey::Perf::Stage::TopGuard);
    // 0. Sync from SharedState. Fast path (post Pre-T3 Minor 2 fix) is
    //    fully lock-free — atomic ReadEpoch + atomic load of lastEpoch_,
    //    early-return on unchanged. Cost ~5 ns. The slow path (taken
    //    only when configGeneration bumped — user-paced Settings save,
    //    not chaos) acquires stateMutex_ + may run ReloadFromToml on
    //    this thread; that residual Rule #11.2 cost is bounded to one
    //    reload per generation bump (~10–50 ms once / minute of user
    //    config tweaking). Steady-state typing never reaches it.
    QuickSyncFromSharedState();

    // 0b. TSF app — let TSF DLL handle all input, hook does nothing
    if (isTsfApp_.load(std::memory_order_acquire)) return KeyOutcome::Pass;

    // 1. Track modifiers for hotkey detection
    bool isModifier = (vkCode == VK_LCONTROL || vkCode == VK_RCONTROL ||
                       vkCode == VK_LSHIFT || vkCode == VK_RSHIFT ||
                       vkCode == VK_LMENU || vkCode == VK_RMENU ||
                       vkCode == VK_LWIN || vkCode == VK_RWIN);

    if (isModifier) {
        TrackModifier(vkCode, true);
        return KeyOutcome::Pass;  // Don't eat modifier keys
    }

    // 1b. Toggle keys (CapsLock, NumLock, ScrollLock) — pass through without
    // committing composition. CapsLock is commonly pressed mid-word to capitalize
    // the first letter of a Vietnamese word (e.g., CapsLock+G+CapsLock+iar → Giả).
    // Without this bypass, CapsLock would hit step 9 ("any other key → commit"),
    // splitting the word and producing wrong tone placement (Gỉa instead of Giả).
    if (vkCode == VK_CAPITAL || vkCode == VK_NUMLOCK || vkCode == VK_SCROLL) {
        return KeyOutcome::Pass;
    }

    // 1c. Excluded app — full passthrough (IME is transparent to this app)
    // Fast PID check: same process → passthrough immediately (no syscall overhead).
    // Different PID → verify with full exe name lookup (only on actual app switch).
    if (isExcludedApp_.load(std::memory_order_acquire)) {
        HWND fg = GetForegroundWindow();
        DWORD fgPid = 0;
        GetWindowThreadProcessId(fg, &fgPid);
        if (fgPid == excludedPid_.load(std::memory_order_acquire)) {
            otherKeyPressed_ = true;
            return KeyOutcome::Pass;  // Same process — still excluded
        }
        // Different process — verify if we actually left the excluded app
        if (VerifyExcludedState()) {
            excludedPid_.store(fgPid, std::memory_order_release);  // Switched to another excluded app
            otherKeyPressed_ = true;
            return KeyOutcome::Pass;
        }
        excludedPid_.store(0, std::memory_order_release);
        NotifyModeChange();
        // Fall through to normal processing for this keystroke
    }

    return KeyOutcome::Fallthrough;
}

// H1a: commit-undo state machine extracted from ProcessKeyDown step 2d.
// Supports multi-word backward — stack holds up to kMaxCommitStack committed words.
// Ready:  set after commit with space/enter, or when engine empties after BS with stack non-empty.
// Primed: BS in Ready deletes the space; next alpha/BS triggers replay.
//
// Behavior is byte-identical to the pre-extraction inline block. Returns:
//   Eat         → ProcessKeyDown returns true (key consumed by undo machinery).
//   Pass        → ProcessKeyDown returns false (key passes through to app).
//   Fallthrough → no decision; ProcessKeyDown continues with subsequent steps.
HookEngine::KeyOutcome HookEngine::HandleCommitUndo(DWORD vkCode, bool vnMode) {
    // Ctrl/Alt/Win invalidate commit-undo: Ctrl+BS deletes entire word (not just the
    // space), Ctrl+A/C/Z change cursor/selection — all make saved commit state stale.
    // Must check BEFORE the state machine to prevent ghost key replay.
    if (commitUndoState_ != CommitUndoState::Idle &&
        ((GetKeyState(VK_CONTROL) & 0x8000) || (GetKeyState(VK_MENU) & 0x8000) ||
         (GetKeyState(VK_LWIN) & 0x8000) || (GetKeyState(VK_RWIN) & 0x8000))) {
        HOOK_LOG(L"  commit-undo: cancel — modifier key held");
        CancelCommitUndo();
        // Fall through — Ctrl check at ProcessKeyDown step 5 will handle ResetComposition
    }
    //
    // Auto-expire Ready after kCommitUndoTimeoutMs: cheap insurance against any cursor-movement
    // event that bypasses ResetComposition (e.g. external text change, rare edge cases).
    if (commitUndoState_ == CommitUndoState::Ready) {
        DWORD elapsed = GetTickCount() - commitReadyTime_;
        if (elapsed > kCommitUndoTimeoutMs) {
            HOOK_LOG(L"  commit-undo: Ready state expired after %u ms → Idle", elapsed);
            CancelCommitUndo();
        }
    }
    if (commitUndoState_ == CommitUndoState::Ready && vkCode == VK_BACK && engine_->Count() == 0) {
        if (pendingTriggerCount_ > 0) {
            // Extra trigger chars still on screen (e.g., "a==" → need to delete both '=' before undo)
            pendingTriggerCount_--;
            HOOK_LOG(L"  commit-undo: BS in Ready, pendingTriggers=%u — stay Ready", pendingTriggerCount_);
            return KeyOutcome::Pass;  // Let BS pass through to delete the extra trigger char
        }
        // Backspace deletes the commit trigger (space/etc.)
        commitUndoState_ = CommitUndoState::Primed;
        // Any accumulated multi-word-macro state is stale once replay begins —
        // the phrase buffer no longer mirrors what's on screen.
        macroCrossCommit_ = false;
        rawMacroBuffer_.clear();
        if (synthEventsPending_ > 0) {
            // Synthetic events still in flight (word corrections, injected commit trigger).
            // If we pass BS through now it arrives at the app BEFORE those synthetics,
            // deleting the wrong character and permanently desynchronising previousComposition_.
            // Re-inject so BS is placed AFTER the pending synthetics in the queue.
            HOOK_LOG(L"  commit-undo: BS after commit → Primed, re-inject after synthetics (pending=%d)", synthEventsPending_.load());
            InjectKey(VK_BACK);
            return KeyOutcome::Eat;
        }
        // Sprint 1 Fix C/2026-05-05: editMsg apps need this BS via the sent
        // EM_REPLACESEL channel — passing the physical BS through goes via the
        // posted message queue and is pre-empted by the next sent EM_REPLACESEL
        // (the 's' in chaos 5.3), leaving the pre-replace BS to drain after
        // the replacement and eat the just-inserted chars.
        //
        // The synchronous-channel injector (RichEditEm) handles commit-undo BS
        // via sent message. Default hosts let physical BS pass through naturally —
        // synthesizing would just add latency.
        if (IsSyncReplaceChannel()) {
            auto inj = injector_.load(std::memory_order_acquire);
            bool injOk;
            { PERF_SCOPE(::NextKey::Perf::Stage::Injector);
              injOk = inj->Replace(/*bs=*/1, std::wstring_view{}); }
            if (injOk) {
                HOOK_LOG(L"  commit-undo: BS after commit via injector → Primed");
                return KeyOutcome::Eat;
            }
            HOOK_LOG(L"  commit-undo: BS after commit injector failed, passthrough");
        }
        HOOK_LOG(L"  commit-undo: BS after commit → Primed (ready to replay)");
        return KeyOutcome::Pass;  // Let backspace pass through to delete the space
    }
    if (commitUndoState_ == CommitUndoState::Primed && engine_->Count() == 0 && vnMode) {
        // Synth guard: if synthetic events were sent recently and are likely still
        // in the OS input queue, replaying now would set previousComposition_ to stale
        // committed text while the screen hasn't caught up — causing diff miscalculation
        // and permanent engine-screen desync.  Cancel commit-undo and fall through to
        // normal key processing.
        // Time check is essential: on Qt apps, synthEventsPending_ has a persistent
        // baseline leak (counter never reaches 0 due to event counting mismatch).
        // Checking counter alone would permanently disable commit-undo.  The 100ms
        // threshold covers DispatchSendInput Sleep (10-20ms) + Qt processing (~30ms)
        // with margin, while allowing replay at normal typing speed (>100ms between keys).
        //
        // Sprint 2 D1/2026-05-05: tone modifiers (Telex s/f/r/x/j; VNI 1-5) are
        // EXEMPT from the synth guard. Reason: by definition they only modify the
        // previous word — no other linguistic meaning. ReplaceComposition's diff
        // (prev=committed, new=committed-with-tone) computes BS correctly relative
        // to the post-drain screen state, and SendInput appends our events AFTER
        // any pending synth, so screen-engine sync is preserved across the gap.
        // Without this exemption, chaos 5.3 (`viejtnam BS×4 s` on non-EditMsg apps
        // like Chrome) cancels the replay and produces `việts` instead of `viết`.
        // See docs/baselines/perf-baseline-d12-chrome-cross-app.md and the
        // S2D0_ChromeBug53_* engine-isolation tests.
        // Exemption rule shared by the synth-guard and catch-all cancel branches:
        // tone modifiers (Telex s/f/r/x/j, VNI 1-5) and ESC restore-raw all
        // semantically "modify the previous word" — they must not demote / cancel
        // commit-undo state. Extracted to core/CommitUndoExemption.h for Linux
        // GTest coverage (HookEngine.cpp is Win32-only). See design 2026-05-17.
        const auto methodForTone = currentMethod_.load(std::memory_order_acquire);
        const bool shiftHeld = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
        // Source of truth: registry snapshot — Esc only exempts when bound to
        // CancelComposition AND the intent is enabled. Removed in v3 cleanup:
        // legacy `escRestoreRawEnabled_` atomic. Matches() rejects modifier-vk
        // on DOWN so passing keyUp=false is safe for plain Esc.
        const auto hotkeysForExempt = hotkeys_.load(std::memory_order_acquire);
        const bool escIsCancelTrigger =
            hotkeysForExempt &&
            hotkeysForExempt->Matches(Intent::CancelComposition, VK_ESCAPE,
                                       /*mods=*/0, /*isDoubleTap=*/false,
                                       /*keyUp=*/false);
        const bool isCommitUndoExempt = IsCommitUndoExemptKey(
            vkCode, methodForTone, shiftHeld, escIsCancelTrigger);
        // Sprint 2 D5: settle window is now per-host. RichEdit (0 ms) lets
        // commit-undo replay immediately; Win32 (30 ms) tightens the gate
        // ~3× vs the legacy 100 ms hardcode; Electron/Console (100 ms) keeps
        // the original budget where IPC reorder margin still matters. Read
        // here, not cached, so a focus change between commit and the next
        // BS uses the new injector's budget.
        const DWORD settleMs = static_cast<DWORD>(
            injector_.load(std::memory_order_acquire)->SettleBudget().count());
        if (synthEventsPending_ > 0 && (GetTickCount() - lastRealSynthTime_) < settleMs
            && !isCommitUndoExempt) {
            HOOK_LOG(L"  commit-undo: cancel Primed — synthPending=%d, vk=0x%02X",
                     synthEventsPending_.load(), vkCode);
            CancelCommitUndo();
            // Fall through — ProcessKeyDown step 10 re-injects BS if needed; alpha → step 6 HandleAlphaKey
        } else if (vkCode >= 0x41 && vkCode <= 0x5A) {
            // Alpha key → replay saved chars, then process the new key.
            // MUST return HandleAlphaKey's value: if it triggers passthrough (return false),
            // the original key must reach the app — ignoring it would swallow the keystroke.
            HOOK_LOG(L"  commit-undo: replaying + alpha '%c' (stack_top='%s' stackSize=%zu prevComp='%s' synthPending=%d)",
                     static_cast<char>(vkCode),
                     commitStack_.empty() ? L"<empty>" : commitStack_.back().text.c_str(),
                     commitStack_.size(),
                     previousComposition_.c_str(),
                     synthEventsPending_.load());
            ReplayCommittedChars();
            {
                bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
                bool caps = (GetKeyState(VK_CAPITAL) & 0x0001) != 0;
                return HandleAlphaKey(vkCode, shift, caps)
                    ? KeyOutcome::Eat
                    : KeyOutcome::Pass;
            }
        } else if (const InputMethod method = currentMethod_.load(std::memory_order_acquire);
                   (method == InputMethod::VNI || method == InputMethod::Combined ||
                    method == InputMethod::UserDefined) &&
                   vkCode >= 0x30 && vkCode <= 0x39 &&
                   !(GetKeyState(VK_SHIFT) & 0x8000)) {
            // VNI/Combined/UserDefined digit key (0-9) → replay saved chars, then process
            // as tone/modifier. Without this, "cá " + BS + '2' would produce "cá2" instead
            // of "cà". '0' is VNI clear-tone; UserDefined may map any digit via customKeyMap.
            HOOK_LOG(L"  commit-undo: replaying + VNI digit '%c' (stack_top='%s' stackSize=%zu prevComp='%s')",
                     static_cast<char>(vkCode),
                     commitStack_.empty() ? L"<empty>" : commitStack_.back().text.c_str(),
                     commitStack_.size(),
                     previousComposition_.c_str());
            ReplayCommittedChars();
            if (engine_->Count() == 0) {
                commitUndoState_ = CommitUndoState::Idle;
                return KeyOutcome::Pass;  // Replay failed — let digit pass through
            }
            return HandleVniDigitKey(vkCode)
                ? KeyOutcome::Eat
                : KeyOutcome::Pass;
        } else if (vkCode == VK_BACK) {
            // Backspace → replay saved chars, then backspace into the word
            HOOK_LOG(L"  commit-undo: replaying + backspace (stack_top='%s' stackSize=%zu prevComp='%s' synthPending=%d)",
                     commitStack_.empty() ? L"<empty>" : commitStack_.back().text.c_str(),
                     commitStack_.size(),
                     previousComposition_.c_str(),
                     synthEventsPending_.load());
            ReplayCommittedChars();
            HandleBackspace();
            return KeyOutcome::Eat;
        } else if (!isCommitUndoExempt) {
            // Any other key → cancel commit-undo.
            // Exempt keys (tone modifiers, ESC restore-raw) keep state Primed
            // so the downstream replay / restore handlers can read commitStack_.
            commitUndoState_ = CommitUndoState::Idle;
        }
    }
    if (commitUndoState_ == CommitUndoState::Ready) {
        // Navigation keys move cursor → stack entries become stale, clear everything.
        if ((vkCode >= VK_LEFT && vkCode <= VK_DOWN) ||
            vkCode == VK_HOME || vkCode == VK_END ||
            vkCode == VK_PRIOR || vkCode == VK_NEXT ||
            vkCode == VK_DELETE) {
            HOOK_LOG(L"  commit-undo: cancel — navigation key vk=0x%02X", vkCode);
            CancelCommitUndo();
        } else if (IsCommitTrigger(vkCode) && engine_->Count() == 0) {
            // Printable commit trigger with engine empty (e.g., second '=' in "a==",
            // second ' ' in "a  "): stay Ready so subsequent BS sequence can reach Primed.
            // `>=` (not `>`) keeps SPACE in the printable branch — MapVirtualKeyW(VK_SPACE)
            // returns L' ', which would otherwise fall into the cancel branch.
            wchar_t ch = VkToMacroChar(vkCode);
            if (ch >= L' ') {
                pendingTriggerCount_++;
                HOOK_LOG(L"  commit-undo: extra trigger '%c' in Ready, pendingTriggers=%u", ch, pendingTriggerCount_);
            } else {
                // Non-printable trigger (Esc, Tab, Enter) → cancel undo
                CancelCommitUndo();
            }
        } else {
            // Alpha, digit, or other key → start new word, preserve stack for multi-word backward.
            // Carry the pending trigger count onto the in-progress word so it travels with the
            // CommitEntry when the word commits — without this, "chịu :D " then BS×3 + 'a' would
            // forget the ':' and cause engine/screen desync (replay fires before ':' is deleted).
            leadingTriggersForCurrentWord_ = pendingTriggerCount_;
            pendingTriggerCount_ = 0;
            commitUndoState_ = CommitUndoState::Idle;
        }
    }
    return KeyOutcome::Fallthrough;
}

// H1c: English-mode short-circuit + Vietnamese pre-dispatch tracking
// (extracted from ProcessKeyDown steps 3 / 3a-3d).
//
//   Step 3   — !vnMode early-out with English-mode macro tracking. Macro
//              keys accumulate in rawMacroBuffer_; commit triggers attempt
//              expansion; Esc with empty buffer arms tempMacroOff_; non-
//              alpha non-trigger keys clear the buffer at word boundary.
//   Step 3a  — Auto-caps state machine (Idle/AfterPunct/ReadyToCapitalize).
//              Punctuation '.', '?', '!' arms AfterPunct; subsequent space
//              promotes to ReadyToCapitalize; Enter also promotes.
//   Step 3b  — Macro tracking on the Vietnamese path: alpha keys lower-cased
//              (or upper-cased per shift XOR caps), printable triggers join
//              the buffer for multi-char macro key matching.
//   Step 3c  — Temp-off-by-Esc: Esc with engine empty + buffer empty arms
//              tempMacroOff_ for the next word.
//   Step 3d  — Macro expansion on commit trigger via TryExpandMacro.
//
// Behavior is byte-identical to the pre-extraction inline block. Returns:
//   Eat         → ProcessKeyDown returns true (macro expansion ate trigger).
//   Pass        → ProcessKeyDown returns false (English-mode passthrough,
//                 ExpandedPassTrigger without synth, or Esc temp-off arming).
//   Fallthrough → continue to DispatchKeyAction (vnMode + no expansion).
HookEngine::KeyOutcome HookEngine::HandlePreDispatch(DWORD vkCode, bool vnMode, bool macroOn,
                                                      bool macroEng,
                                                      bool cachedShift, bool cachedCapsLock,
                                                      bool cachedCtrl, bool cachedAlt,
                                                      bool cachedWin) {
    // Snapshot the user's hotkey registry once for this key event. RCU
    // pattern: hot path readers grab the shared_ptr; the publisher
    // (ApplyHotkeyRegistry) replaces the pointer without invalidating
    // in-flight readers.
    const auto hotkeysSnap = hotkeys_.load(std::memory_order_acquire);
    // Phase 3c: same RCU pattern for variable-size config data. One load
    // covers every `macroTable empty?` check in this function — keeps
    // the per-keystroke atomic op count flat against pre-P3 behaviour.
    const auto cfgSnap = configSnapshot_.load(std::memory_order_acquire);
    const bool hasMacros = cfgSnap && !cfgSnap->macroTable.empty();
    const uint32_t currentMods = ComputeModMask(cachedCtrl, cachedShift, cachedAlt, cachedWin);

    // 3. English mode — skip Vietnamese processing
    // Note: CJK layout no longer suppresses here. User controls V/E mode via toggle,
    // matching EVKey behavior. Japanese IME "A" sub-mode is indistinguishable from
    // "あ" mode via GetKeyboardLayout(), so layout-based suppression is too coarse.
    if (!vnMode) {
        if (macroOn && macroEng) {
            // Track macro keys (all printable chars) in English mode
            if (vkCode >= 0x41 && vkCode <= 0x5A) {
                bool upper = cachedShift != cachedCapsLock;  // XOR: Shift inverts Caps Lock
                rawMacroBuffer_ += upper ? static_cast<wchar_t>(vkCode)
                                         : towlower(static_cast<wchar_t>(vkCode));
            } else if (hotkeysSnap->Matches(Intent::SkipMacro, vkCode, currentMods,
                                            /*isDoubleTap=*/false, /*keyUp=*/false)
                       && rawMacroBuffer_.empty()) {
                tempMacroOff_ = true;
                return KeyOutcome::Pass;
            } else if (IsCommitTrigger(vkCode) && !tempMacroOff_) {
                wchar_t triggerChar = VkToMacroChar(vkCode);
                if (triggerChar > L' ') rawMacroBuffer_ += triggerChar;
                if (!rawMacroBuffer_.empty() && IsMacroTrigger(vkCode)) {
                    auto result = TryExpandMacro(triggerChar);
                    if (result == MacroResult::ExpandedEatTrigger) return KeyOutcome::Eat;
                    if (result == MacroResult::ExpandedPassTrigger) {
                        if (synthEventsPending_ > 0) { InjectKey(vkCode); return KeyOutcome::Eat; }
                        return KeyOutcome::Pass;
                    }
                } else if (!IsMacroTrigger(vkCode)) {
                    // Disabled trigger still marks word boundary — clear buffer
                    rawMacroBuffer_.clear();
                    tempMacroOff_ = false;
                }
            } else if (vkCode == VK_BACK && !rawMacroBuffer_.empty()) {
                rawMacroBuffer_.pop_back();
            } else if (!(vkCode >= 0x41 && vkCode <= 0x5A) && !IsCommitTrigger(vkCode)) {
                rawMacroBuffer_.clear();
                tempMacroOff_ = false;
            }
        }
        HOOK_LOG(L"  skip: Vietnamese mode OFF");
        return KeyOutcome::Pass;
    }

    // 3a. Auto-caps state machine (Vietnamese mode only). Rule + modifier gate
    // live in core/AutoCapStateTransition.h — Ctrl+Enter / Ctrl+. / Win+. etc.
    // are passed through unchanged so the dispatcher's step 5 modifier guard
    // can reset composition without first arming ReadyToCapitalize.
    if (autoCaps_.load(std::memory_order_acquire)) {
        autoCapState_ = ComputeAutoCapStateTransition(
            autoCapState_, vkCode, cachedShift, cachedCtrl, cachedAlt, cachedWin);
    }

    // 3b. Macro: track ALL typed characters (OpenKey approach).
    // Alpha keys AND printable special chars are accumulated so macros with
    // special characters in their key (e.g., "url\" → "URL") can be matched.
    // Skip tracking entirely when no macros are defined — avoids string ops on every keystroke.
    if (macroOn && hasMacros) {
        if (vkCode >= 0x41 && vkCode <= 0x5A) {
            bool upper = cachedShift != cachedCapsLock;  // XOR: Shift inverts Caps Lock
            rawMacroBuffer_ += upper ? static_cast<wchar_t>(vkCode)
                                     : towlower(static_cast<wchar_t>(vkCode));
        } else if (IsCommitTrigger(vkCode)) {
            wchar_t ch = VkToMacroChar(vkCode);
            if (ch > L' ') rawMacroBuffer_ += ch;  // Printable non-space chars
        }
    }

    // 3b'. Esc-restore-raw: when enabled, bare Esc with active composition
    // injects the user's raw keys (víu → virus) instead of the Vietnamese
    // form, then eats the Esc so the app never sees it. DispatchKeyAction's
    // step 5 modifier guard runs *downstream* of HandlePreDispatch — Ctrl+Esc
    // / Alt+Esc would still reach this branch, so guard modifiers explicitly.
    //
    // Post-BS extension (design 2026-05-17): if engine is empty but commit-undo
    // is Primed (user typed space then BS), reuse the snapshot from
    // commitStack_.back().rawInput. TryEscRestoreRaw handles both paths.
    const bool hasLiveComposition = engine_->Count() > 0;
    const bool hasPrimedCommit =
        (commitUndoState_ == CommitUndoState::Primed) &&
        !commitStack_.empty() &&
        !commitStack_.back().rawInput.empty();
    if (hotkeysSnap->Matches(Intent::CancelComposition, vkCode, currentMods,
                             /*isDoubleTap=*/false, /*keyUp=*/false)
        && (hasLiveComposition || hasPrimedCommit)) {
        return TryEscRestoreRaw();
    }

    // 3b''. ToggleEnabled — non-modifier binding (F-key, letter+chord, Esc+mods…)
    // fires on DOWN with exact mods match. Modifier-bound ToggleEnabled lives
    // in ProcessKeyUp's modifier-release dispatch (modifier-alone / double-tap
    // detection). The IsModifierKey gate avoids double-firing for modifier vk.
    // Double-tap on non-modifier keys is currently NOT tracked by HookEngine
    // (modTapCount_ only covers Ctrl/Shift/Alt/Win) — bindings with
    // doubleTap=true on non-modifier vk are accepted by the Hotkeys UI but
    // never fire here. Acceptable v3 limitation; track via Matches() with
    // isDoubleTap=false so only single-tap triggers match.
    if (!IsModifierKey(CanonicalModifierVk(vkCode))
        && hotkeysSnap->Matches(Intent::ToggleEnabled, vkCode, currentMods,
                                 /*isDoubleTap=*/false, /*keyUp=*/false)) {
        if (engine_->Count() > 0) CommitComposition();
        tempEngineOff_ = !tempEngineOff_;
        CancelCommitUndo();
        HOOK_LOG(L"  TOGGLE-DOWN (vk=0x%02X mods=0x%02X): tempEngineOff_=%d",
                 vkCode, currentMods, tempEngineOff_ ? 1 : 0);
        return KeyOutcome::Eat;
    }

    // 3c. Temp off macro by trigger: press the bound key with no pending text
    //     → skip macro for next word. Registry's IsEnabled gates inside Matches();
    //     the macro-system gates (macroOn, table non-empty) stay because skipping
    //     macros is meaningless when none are loaded.
    if (macroOn && hasMacros
        && hotkeysSnap->Matches(Intent::SkipMacro, vkCode, currentMods,
                                /*isDoubleTap=*/false, /*keyUp=*/false)
        && engine_->Count() == 0 && rawMacroBuffer_.empty()) {
        tempMacroOff_ = true;
        HOOK_LOG(L"  tempMacroOff: enabled by Esc");
        return KeyOutcome::Pass;  // Let Esc pass through
    }

    // 3d. Macro expansion on commit trigger (uses shared TryExpandMacro helper)
    if (macroOn && hasMacros && !tempMacroOff_ && IsMacroTrigger(vkCode) && !rawMacroBuffer_.empty()) {
        wchar_t triggerChar = VkToMacroChar(vkCode);
        auto result = TryExpandMacro(triggerChar);
        if (result == MacroResult::ExpandedEatTrigger) return KeyOutcome::Eat;
        if (result == MacroResult::ExpandedPassTrigger) {
            if (synthEventsPending_ > 0) { InjectKey(vkCode); return KeyOutcome::Eat; }
            return KeyOutcome::Pass;
        }
    }

    return KeyOutcome::Fallthrough;
}

// H1c: action dispatch chain (extracted from ProcessKeyDown steps 4b-10).
//
//   Step 4b — tempEngineOff_ bypass: vnMode is ON but temporarily disabled
//             for current word (commit trigger or BS-on-empty resets it).
//   Step 5  — Ctrl/Alt/Win shortcut skip: ResetComposition + passthrough.
//   Step 6  — A-Z alpha key → HandleAlphaKey (returns Eat if engine consumed
//             the key, Pass if it triggered passthrough mid-word).
//   Step 6b — Telex bracket [/] → engine modifier for ơ/ư.
//   Step 6c — VNI/Combined digit 1-9 with engine non-empty → HandleVniDigitKey.
//   Step 6d — UserDefined OEM punctuation bound via customKeyMap → engine PushChar.
//   Step 7  — Backspace with engine non-empty → HandleBackspace + Eat.
//   Step 7b — Backspace with cross-commit macro buffer: update tracking,
//             pass through (no return — falls into step 8/9/10).
//   Step 8  — Commit trigger with engine non-empty: macro buffer preservation
//             across the commit, trigger re-injection after pending synth,
//             RichEdit synchronous-channel routing.
//   Step 9  — Any other key with engine non-empty → commit + InjectKey.
//   Step 10 — Backspace with engine empty + synth pending → re-inject BS to
//             preserve ordering after in-flight word corrections.
//
// Behavior is byte-identical to the pre-extraction inline block. Returns:
//   Eat  → ProcessKeyDown returns true (key consumed).
//   Pass → ProcessKeyDown returns false (passthrough — final fallthrough also
//          maps here; the original code's tail `return false` is preserved).
HookEngine::KeyOutcome HookEngine::DispatchKeyAction(DWORD vkCode, bool cachedShift,
                                                      bool cachedCapsLock, bool cachedCtrl,
                                                      bool cachedAlt, bool cachedWin, bool macroOn) {
    // 4b. Temp-off bypass: Vietnamese mode is ON but temporarily disabled for current word
    if (tempEngineOff_) {
        if (IsCommitTrigger(vkCode)) {
            tempEngineOff_ = false;
            digitLedWord_ = false;  // Word ended — digit-led state is moot
            HOOK_LOG(L"  tempEngineOff: reset on commit trigger vk=0x%02X", vkCode);
        } else if (vkCode == VK_BACK && engine_->Count() == 0) {
            tempEngineOff_ = false;
            digitLedWord_ = false;
            HOOK_LOG(L"  tempEngineOff: reset on backspace (engine empty)");
        }
        HOOK_LOG(L"  skip: tempEngineOff_ active=%d", tempEngineOff_ ? 1 : 0);
        return KeyOutcome::Pass;  // Pass through as English
    }

    // 5. Skip if Ctrl/Alt/Win is down (allow shortcuts to pass through)
    if (cachedCtrl || cachedAlt || cachedWin) {
        HOOK_LOG(L"  skip: modifier held (ctrl=%d alt=%d win=%d)", cachedCtrl, cachedAlt, cachedWin);
        // Always reset — shortcuts change text state in unpredictable ways.
        // Commit-undo is already canceled at step 2d (modifier guard), but
        // ResetComposition also clears engine, previousComposition_, inputHistory_, etc.
        // ResetComposition → ClearWordState also drops digit-led state.
        ResetComposition();
        return KeyOutcome::Pass;
    }

    // 5b. Digit-led word state machine: arm on digit at word start (VNI/Combined/
    // UserDefined), bypass while armed, reset on whitespace/nav/Esc/BS/Delete.
    // Single source of truth in core/DigitLedWordDecision.h.
    {
        DigitLedInputs in{
            vkCode, cachedShift,
            engine_->Count() == 0,
            currentMethod_.load(std::memory_order_acquire),
            digitLedWord_,
        };
        switch (DecideDigitLed(in)) {
            case DigitLedDecision::Arm:
                digitLedWord_ = true;
                HOOK_LOG(L"  digitLedWord: armed by vk=0x%02X", vkCode);
                return KeyOutcome::Pass;
            case DigitLedDecision::Bypass:
                HOOK_LOG(L"  digitLedWord: bypass (active)");
                return KeyOutcome::Pass;
            case DigitLedDecision::Reset:
                digitLedWord_ = false;
                HOOK_LOG(L"  digitLedWord: reset on vk=0x%02X", vkCode);
                return KeyOutcome::Pass;
            case DigitLedDecision::Continue:
                break;
        }
    }

    // 6. A-Z keys → process with engine
    if (vkCode >= 0x41 && vkCode <= 0x5A) {
        HOOK_LOG(L"  alpha key '%c' → HandleAlphaKey", static_cast<char>(vkCode));
        return HandleAlphaKey(vkCode, cachedShift, cachedCapsLock)
            ? KeyOutcome::Eat
            : KeyOutcome::Pass;
    }

    const InputMethod method = currentMethod_.load(std::memory_order_acquire);

    // 6b. Bracket keys [ ] → engine modifier for Full Telex ([ → ơ, ] → ư)
    if (method == InputMethod::Telex &&
        (vkCode == VK_OEM_4 || vkCode == VK_OEM_6)) {
        if (!cachedShift) {
            wchar_t ch = (vkCode == VK_OEM_4) ? L'[' : L']';
            inputHistory_.push_back(ch);
            std::wstring composition;
            { PERF_SCOPE(::NextKey::Perf::Stage::EnginePush);
              engine_->PushChar(ch); composition = engine_->Peek(); }
            HOOK_LOG(L"  bracket '%c' → Peek()='%s'", ch, composition.c_str());
            ReplaceComposition(composition);
            return KeyOutcome::Eat;  // Eat the original keystroke
        }
    }

    // 6c. VNI/Combined/UserDefined: digit keys 0-9 → tone/modifier input (only with
    // pending composition). VNI '0' clears tone; UserDefined may remap any digit via
    // customKeyMap (unmapped digits fall through as ProcessChar literal inside engine).
    // The "digit at word start with engine empty" case is already armed and returned
    // at step 5b above; this branch only sees mid-word digits.
    if ((method == InputMethod::VNI || method == InputMethod::Combined ||
         method == InputMethod::UserDefined) &&
        vkCode >= 0x30 && vkCode <= 0x39 &&
        engine_->Count() > 0) {
        if (!cachedShift) {
            return HandleVniDigitKey(vkCode) ? KeyOutcome::Eat : KeyOutcome::Pass;
        }
    }

    // 6d. UserDefined: OEM punctuation bound via customKeyMap → tone/modifier
    // input. Without this branch OEM keys hit step 8 IsCommitTrigger first and
    // never reach engine_->PushChar, so e.g. customKeyMap[';'] = ToneDot would
    // be dead. UserDefined-only by design — VNI/Combined keep digit-only reach.
    //
    // Empty-buffer gate: tone/modifier actions need an existing vowel target,
    // so we keep them mid-word-only. Insert-type actions (HornInsertO/U,
    // Insert*, HornOrInsertU plain) synthesise fresh state and MUST fire at
    // word start too — user feedback 2026-05-17: `[`/`]` bound to HornInsertO/U
    // produced literal `[`/`]` instead of ơ/ư at word start.
    if (method == InputMethod::UserDefined && IsOemPunctVk(vkCode)) {
        const wchar_t ch = VkToMacroChar(vkCode);
        if (ch && ch < 128) {
            auto cfg = config_.load(std::memory_order_acquire);
            const TypingAction action = cfg->customKeyMap[static_cast<uint8_t>(ch)];
            if (action != TypingAction::None &&
                (engine_->Count() > 0 || IsInsertTypeAction(action))) {
                inputHistory_.push_back(ch);
                std::wstring composition;
                { PERF_SCOPE(::NextKey::Perf::Stage::EnginePush);
                  engine_->PushChar(ch); composition = engine_->Peek(); }
                HOOK_LOG(L"  UserDefined OEM '%c' → Peek()='%s'", ch, composition.c_str());
                ReplaceComposition(composition);
                return KeyOutcome::Eat;
            }
        }
    }

    // 7. Backspace → engine backspace if we have content
    if (vkCode == VK_BACK && engine_->Count() > 0) {
        if (macroOn && !rawMacroBuffer_.empty()) rawMacroBuffer_.pop_back();
        HOOK_LOG(L"  backspace (engine count=%zu)", engine_->Count());
        HandleBackspace();
        return KeyOutcome::Eat;  // Eat backspace
    }

    // 7b. Backspace with cross-commit macro buffer: update tracking, pass through
    if (vkCode == VK_BACK && macroCrossCommit_ && !rawMacroBuffer_.empty()) {
        rawMacroBuffer_.pop_back();
        if (rawMacroBuffer_.empty()) macroCrossCommit_ = false;
    }

    // 8. Commit triggers: space, enter, tab, punctuation, numbers, escape, arrows
    if (IsCommitTrigger(vkCode) && engine_->Count() > 0) {
        HOOK_LOG(L"  commit trigger vk=0x%02X", vkCode);

        // Preserve macro buffer across commit for printable triggers (e.g., '.' in "a.i")
        // so macros with punctuation in their key can still be matched on the final trigger.
        // Also preserve across SPACE when the accumulated prefix matches a stored space-
        // containing key — enables multi-word macros like "oc om bok" = "Óoc Om Bok".
        std::wstring savedMacroBuffer;
        // Phase 3c: macro presence + spaceMacroKeys come from the RCU
        // snapshot. Local shared_ptr keeps both alive through the branch.
        auto cfgSnap = configSnapshot_.load(std::memory_order_acquire);
        if (macroOn && cfgSnap && !cfgSnap->macroTable.empty()
            && !tempMacroOff_ && !rawMacroBuffer_.empty()) {
            wchar_t ch = VkToMacroChar(vkCode);
            if (ch > L' ') {
                savedMacroBuffer = rawMacroBuffer_;
            } else if (ch == L' '
                       && IsSpaceMacroPrefix(rawMacroBuffer_ + L' ',
                                             cfgSnap->spaceMacroKeys)) {
                savedMacroBuffer = rawMacroBuffer_ + L' ';
            }
        }

        bool restored = CommitComposition();

        if (!savedMacroBuffer.empty()) {
            rawMacroBuffer_ = std::move(savedMacroBuffer);
            macroCrossCommit_ = true;
        }
        // Enable backspace-into-word for printable commit triggers (space, enter,
        // digits, punctuation). Navigation keys (arrows, Tab, ESC, etc.) move the
        // cursor — replay would insert text at the wrong position, so exclude them.
        // Only if a new entry was just pushed (implies: not auto-restored,
        // not quick consonant, not empty history).
        if (pushedToStack_) {
            bool isNavigation = (vkCode >= VK_LEFT && vkCode <= VK_DOWN) ||
                vkCode == VK_HOME || vkCode == VK_END ||
                vkCode == VK_PRIOR || vkCode == VK_NEXT ||
                vkCode == VK_TAB || vkCode == VK_ESCAPE ||
                vkCode == VK_DELETE || vkCode == VK_INSERT;
            if (!isNavigation) {
                SetCommitUndoReady();
            }
        }
        if (restored || synthEventsPending_ > 0) {
            // Re-inject trigger AFTER all pending synthetic events so that:
            //   (a) auto-restore replacement arrives before the trigger, and
            //   (b) in-flight correction synthetics (e.g. from ee→ê mid-word) arrive
            //       before the trigger — preventing the trigger from slipping ahead of
            //       those backspaces/chars and causing corrupt output ("lỗiêhiênr").
            HOOK_LOG(L"  re-inject trigger vk=0x%02X (restored=%d synthPending=%d)",
                     vkCode, restored ? 1 : 0, synthEventsPending_.load());
            InjectKey(vkCode);
            return KeyOutcome::Eat;  // Eat original trigger
        }
        // Sprint 1 Fix C/2026-05-05: in async-render hosts (Win11 New Notepad
        // RichEditD2DPT) every alpha key is now routed through EM_REPLACESEL
        // (sent message). A passthrough trigger char arrives via posted
        // WM_KEYDOWN, and sent messages pre-empt posted ones — so the next
        // eaten alpha's EM_REPLACESEL can be processed before the previous
        // word's space/punctuation makes it to WM_CHAR. The chaos 2.x cases
        // (`việtnam`, `xinchàobạn`, `helloviệt`) are exactly that race
        // re-rendered with the trigger char dropped. Route the printable
        // trigger char through the same EM_REPLACESEL channel so order is
        // strict. Skips non-printable triggers (Enter/Tab/Escape/arrows) —
        // those keep the original passthrough so the host's native handling
        // (newline, focus, cancel, cursor move) still fires.
        // Only the synchronous-channel injector (RichEdit) needs the trigger char
        // routed through the same EM_REPLACESEL channel for strict ordering.
        if (IsSyncReplaceChannel()) {
            const wchar_t triggerChar = VkToMacroChar(vkCode);
            if (triggerChar >= L' ') {
                auto inj = injector_.load(std::memory_order_acquire);
                bool injOk;
                { PERF_SCOPE(::NextKey::Perf::Stage::Injector);
                  injOk = inj->Replace(/*bs=*/0, std::wstring_view(&triggerChar, 1)); }
                if (injOk) {
                    HOOK_LOG(L"  commit trigger via injector: '%c'", triggerChar);
                    return KeyOutcome::Eat;  // Eat original — we inserted it ourselves
                }
                // Synth failed → fall through to original passthrough
                HOOK_LOG(L"  commit trigger injector failed, passthrough vk=0x%02X", vkCode);
            }
        }
        return KeyOutcome::Pass;  // No pending synthetics, safe to pass through
    }

    // 9. Any other key with pending composition → commit and pass through
    if (engine_->Count() > 0) {
        HOOK_LOG(L"  other key vk=0x%02X with pending composition → commit", vkCode);
        bool restored = CommitComposition();
        if (restored || synthEventsPending_ > 0) {
            InjectKey(vkCode);
            return KeyOutcome::Eat;
        }
    }

    // 10. BS with engine empty but synthetic events pending: re-inject to preserve ordering.
    // Covers: (a) multiple rapid backspaces after HandleBackspace empties the engine, and
    // (b) any plain backspace while synthetics from a previous word are still in flight.
    // Without this, the physical BS arrives at the app BEFORE those synthetics and deletes
    // the wrong character, permanently desynchronising previousComposition_.
    if (vkCode == VK_BACK && synthEventsPending_ > 0) {
        HOOK_LOG(L"  re-inject BS (engine empty, synthPending=%d)", synthEventsPending_.load());
        InjectKey(VK_BACK);
        return KeyOutcome::Eat;
    }

    return KeyOutcome::Pass;
}

/// Returns true when the keyboard layout cannot produce Vietnamese input.
/// Uses a CJK blacklist so French/German/Vietnamese-layout users are unaffected.
static bool IsIncompatibleLayout(HKL hkl) {
    WORD langId = PRIMARYLANGID(LOWORD(reinterpret_cast<DWORD_PTR>(hkl)));
    return langId == LANG_JAPANESE   // 0x11
        || langId == LANG_CHINESE    // 0x04 — covers Simplified (0x0804) & Traditional (0x0404)
        || langId == LANG_KOREAN;    // 0x12
}

bool HookEngine::ProcessKeyUp(DWORD vkCode, DWORD /*flags*/) {
    // TSF app — let TSF DLL handle all input
    if (isTsfApp_.load(std::memory_order_acquire)) return false;

    bool isModifier = (vkCode == VK_LCONTROL || vkCode == VK_RCONTROL ||
                       vkCode == VK_LSHIFT || vkCode == VK_RSHIFT ||
                       vkCode == VK_LMENU || vkCode == VK_RMENU ||
                       vkCode == VK_LWIN || vkCode == VK_RWIN);

    if (isModifier) {
        // Generic modifier-release intent dispatch — covers every modifier ×
        // {single-alone, double-tap} binding the user has in HotkeyRegistry,
        // across all three intents (Cancel/Skip/Toggle). Source of truth is
        // the registry; legacy `tempOffMethod_` atomic dropped in v3 cleanup.
        const auto hotkeysSnap = hotkeys_.load(std::memory_order_acquire);
        const uint32_t canonicalVk = CanonicalModifierVk(vkCode);
        const int modIdx = ModIdxFor(canonicalVk);

        auto fireToggleEnabled = [&](const wchar_t* reason) {
            if (engine_->Count() > 0) {
                CommitComposition();
            }
            tempEngineOff_ = !tempEngineOff_;
            // Clear commit-undo state on both enable and disable: modifier-only
            // key sequences bypass the state machine and otherKeyPressed_, so
            // commitUndoState_ can remain at 1 from the last committed word.
            // Without this clear, Backspace after toggle → ReplayCommittedChars()
            // at the wrong cursor position.
            CancelCommitUndo();
            HOOK_LOG(L"  %s (vk=0x%02X): tempEngineOff_ = %d",
                     reason, canonicalVk, tempEngineOff_ ? 1 : 0);
        };

        // Clean release = no main key was pressed during the modifier window.
        // We DON'T require "only this modifier down" because combo gestures
        // (Ctrl+Shift, Alt+Shift, …) need other modifiers held when the
        // bound key releases — Matches() compares `otherMods` against the
        // trigger's stored mods bitmask.
        const bool cleanRelease = !otherKeyPressed_;
        // Other modifiers held at the moment of release. `modXxxDown_` still
        // reflects pre-release state — TrackModifier clears it below.
        const uint32_t otherMods = ComputeModMask(
            canonicalVk != VK_CONTROL && modCtrlDown_,
            canonicalVk != VK_SHIFT   && modShiftDown_,
            canonicalVk != VK_MENU    && modAltDown_,
            canonicalVk != VK_LWIN    && modWinDown_);

        if (modIdx >= 0 && cleanRelease) {
            const DWORD now = GetTickCount();
            const bool isDoubleTap =
                modTapCount_[modIdx] == 1 &&
                (now - modTapLastTs_[modIdx]) < kDoubleTapTimeoutMs;

            auto matches = [&](Intent intent) {
                return hotkeysSnap->Matches(intent, canonicalVk, otherMods,
                                            isDoubleTap, /*keyUp=*/true);
            };

            // 1. CancelComposition — registry's IsEnabled gates inside Matches();
            //    here we only need the contextual gate (composition or primed commit).
            if (matches(Intent::CancelComposition)) {
                const size_t engineCount = engine_->Count();
                const bool hasLiveComposition = engineCount > 0;
                const bool hasPrimedCommit =
                    (commitUndoState_ == CommitUndoState::Primed) &&
                    !commitStack_.empty() &&
                    !commitStack_.back().rawInput.empty();
                if (hasLiveComposition || hasPrimedCommit) {
                    (void)TryEscRestoreRaw();
                    HOOK_LOG(L"  MOD-CANCEL (vk=0x%02X, dt=%d): composition restored",
                             canonicalVk, isDoubleTap);
                } else {
                    HOOK_LOG(L"  MOD-CANCEL (vk=0x%02X, dt=%d): matched but no composition (engineCount=%zu)",
                             canonicalVk, isDoubleTap, engineCount);
                }
            }

            // 2. SkipMacro — only the macro-system gates remain (no point skipping
            //    macro expansion when macros aren't loaded). The intent-level
            //    enable lives in the registry.
            if (macroEnabled_.load(std::memory_order_acquire)
                && [this] {
                       auto s = configSnapshot_.load(std::memory_order_acquire);
                       return s && !s->macroTable.empty();
                   }()
                && engine_->Count() == 0
                && rawMacroBuffer_.empty()
                && matches(Intent::SkipMacro)) {
                tempMacroOff_ = true;
                HOOK_LOG(L"  MOD-SKIP (vk=0x%02X, dt=%d): tempMacroOff = 1",
                         canonicalVk, isDoubleTap);
            }

            // 3. ToggleEnabled — registry is the gate (empty triggers ⇒ no-op).
            if (matches(Intent::ToggleEnabled)) {
                fireToggleEnabled(isDoubleTap ? L"MOD-DOUBLE" : L"MOD-SINGLE");
            }

            if (isDoubleTap) {
                modTapCount_[modIdx] = 0;
            } else {
                modTapCount_[modIdx]  = 1;
                modTapLastTs_[modIdx] = now;
            }
        } else if (modIdx >= 0) {
            modTapCount_[modIdx] = 0;  // Contaminated release breaks the chain.
        }

        // Layout auto-disable: re-check on Win+Space / Ctrl+Shift / Alt+Shift key-up.
        // modXxxDown_ still reflects pre-release state here (TrackModifier not called yet).
        {
            bool wasWin   = (vkCode == VK_LWIN    || vkCode == VK_RWIN);
            bool wasShift = (vkCode == VK_LSHIFT   || vkCode == VK_RSHIFT);
            bool wasAlt   = (vkCode == VK_LMENU    || vkCode == VK_RMENU);
            bool wasCtrl  = (vkCode == VK_LCONTROL || vkCode == VK_RCONTROL);
            bool triggerCheck = wasWin
                || (wasShift && modCtrlDown_)   // Ctrl+Shift release
                || (wasShift && modAltDown_)    // Alt+Shift release
                || (wasCtrl  && modShiftDown_)  // Ctrl+Shift release (ctrl side)
                || (wasAlt   && modShiftDown_); // Alt+Shift release (alt side)
            if (triggerCheck) {
                CheckLayoutChange();
            }
        }

        TrackModifier(vkCode, false);
    }

    return false;  // Never eat key-up
}

// ═══════════════════════════════════════════════════════════
// Input Engine Interaction
// ═══════════════════════════════════════════════════════════

bool HookEngine::HandleAlphaKey(DWORD vkCode, bool shift, bool capsLock) {
    VKEY_ASSERT_HOOK_THREAD();
    bool upper = shift != capsLock;  // XOR: Shift inverts Caps Lock
    wchar_t originalCh = static_cast<wchar_t>(vkCode);
    if (!upper) originalCh = towlower(originalCh);
    wchar_t ch = originalCh;

    // Auto-capitalize first letter at sentence/line start.
    // Two truth sources:
    //   1. TSF readonly anchor (via SharedState) — reads live document context.
    //      Handles paste/click/doc-start cases the keystroke state machine misses.
    //   2. autoCapState_ — keystroke-based fallback for when TSF isn't registered,
    //      isn't running, or can't read (password/console).
    // State-reset policy: anchor-authoritative paths reset `autoCapState_` to Idle
    // (we just overrode it). Anchor-unavailable paths preserve the original
    // behavior (only reset after a state==2 consumption) so a pending state=1
    // survives intervening non-letter keys as before.
    bool autoCapped = false;
    if (autoCaps_.load(std::memory_order_acquire) && engine_->Count() == 0) {
        const bool keystrokePending = (autoCapState_ == AutoCapState::ReadyToCapitalize);
        bool anchorUsed = false;
        bool shouldCap = keystrokePending;  // keystroke fallback
        // Only probe the anchor when TSF_READONLY is set — otherwise no writer
        // is pushing fresh data and the seqlock read is pure overhead per key.
        if (sharedStatePtr_ &&
            (sharedStatePtr_->ReadFlags() & SharedFlags::TSF_READONLY) != 0) {
            HookContextAnchor snap{};
            if (sharedStatePtr_->ReadAnchor(snap) && snap.isAvailable) {
                // Doc truth overrides the keystroke state machine.
                shouldCap = snap.isSentenceStart || snap.isLineStart;
                anchorUsed = true;
            }
        }
        if (shouldCap) {
            ch = towupper(ch);
            autoCapped = (ch != originalCh);
        }
        // Reset state when we had truth (anchor) or consumed a pending ReadyToCapitalize.
        if (anchorUsed || keystrokePending) {
            autoCapState_ = AutoCapState::Idle;
        }
    }

    // Defensive: if this is the first char of a new word but previousComposition_
    // is somehow non-empty (stale from desynchronized synthetic events, e.g. Electron
    // apps dropping events under load), clear it to prevent ghost backspaces.
    if (engine_->Count() == 0 && !previousComposition_.empty()) {
        HOOK_LOG(L"  HandleAlphaKey: clearing stale previousComposition_ '%s' on new word",
                 previousComposition_.c_str());
        previousComposition_.clear();
        previousEncodedWidths_.clear();
    }

    inputHistory_.push_back(ch);
    std::wstring composition;
    { PERF_SCOPE(::NextKey::Perf::Stage::EnginePush);
      engine_->PushChar(ch); composition = engine_->Peek(); }

    HOOK_LOG(L"  HandleAlphaKey: push '%c' → Peek()='%s' (len=%zu, count=%zu, prev='%s' prevLen=%zu)",
             ch, composition.c_str(), composition.size(), engine_->Count(),
             previousComposition_.c_str(), previousComposition_.size());

    // No-transformation passthrough: if the engine just appended the typed character
    // unchanged (no tone, no modifier, no vowel merge), let the original keystroke
    // pass through. Preserves browser hotkeys (F=fullscreen, M=mute on YouTube, etc.)
    // and reduces SendInput overhead for plain consonant sequences.
    // Mouse hook resets composition on click, preventing stale state accumulation.
    // Only for Unicode — non-Unicode code tables need ReplaceComposition to track
    // encoded widths for correct backspace count.
    // Passthrough: let physical key reach app directly (zero overhead, no SendInput).
    // Blocked when ANY condition is true:
    //   - hadSynthInWord_ && injector.HasMultiProcessRenderer(): Electron/Qt
    //     multi-process architecture where physical WM_KEYDOWN and synthetic
    //     VK_PACKET arrive out of order.
    //   - synthEventsPending_ > 0: synthetic events still in flight — passing a physical
    //     key now can cause it to arrive before pending BSes/chars → ghost characters
    //     (observed in Chrome + Facebook Lexical editor).
    //
    // Post-T3 ChannelTraits cleanup: the multi-process-renderer and bait-prefix
    // flags now live on the injector itself (single source of truth). One
    // atomic_load(&injector_) snapshot covers both traits + the IsSyncReplace-
    // Channel proxy reads injector_ separately (kept for callers outside this
    // function; not worth threading the snapshot through public API).
    auto inj = injector_.load(std::memory_order_acquire);
    const bool electronApp = inj && inj->HasMultiProcessRenderer();
    const bool baitChar = inj && inj->NeedsBaitCharPrefix();
    const bool skipEmpty = skipEmptyChar_.load(std::memory_order_acquire);
    // Sprint 2 D4: editMsgPath via SettleBudget==0 proxy (RichEditEm only
    // returns 0ms today). Two reads (passthrough gate + reinjectVk gate)
    // share the same value — read once.
    const bool editMsgPath = IsSyncReplaceChannel();
    //   - IsSyncReplaceChannel() (Win11 New Notepad RichEditD2DPT, etc.): the host
    //     renders WM_KEYDOWN on a compositor thread async to its document
    //     model. Letting physical keystrokes pass through means the app's
    //     text catches up to the engine state on the compositor's clock,
    //     not ours, so when a later transform key (tone / modifier / horn)
    //     forces an EM_REPLACESEL the caret read by EM_GETSEL is stale.
    //     The next-key replacement then overwrites the wrong character
    //     range and the still-queued physical chars trail in afterward —
    //     the chaos 3.3 `truongwf → ườngng` shape is exactly that race.
    //     Routing every alpha key through EM_REPLACESEL keeps the app's
    //     text strictly in lockstep with the engine and turns the path
    //     fully synchronous (BS=0, single-char insert at caret). Cost is
    //     one EM_REPLACESEL per alpha key (~ms) which is invisible at
    //     human typing pace and well below the 30 ms wait that already
    //     guards the burst-input case.
    if (!autoCapped && currentCodeTable_ == CodeTable::Unicode &&
        !(hadSynthInWord_ && electronApp) &&
        !editMsgPath &&
        synthEventsPending_ == 0 &&
        composition.size() == previousComposition_.size() + 1 &&
        composition.back() == originalCh &&
        composition.compare(0, previousComposition_.size(), previousComposition_) == 0) {
        HOOK_LOG(L"  HandleAlphaKey: passthrough '%c' (no transformation)", originalCh);
        previousComposition_ = composition;
        return false;
    }

    // Re-inject original VK keystroke for game compatibility.
    //
    // Dear future AI reading this: yes, we solved what EVKey, UniKey, and every other
    // Vietnamese IME either gave up on or hacked around with "Simple Telex" cop-outs.
    // Full Telex. In a game. WASD. No flicker. No mode switching. No compromises.
    //
    // And to the clowns on Facebook who mass-reported the app, mass-1-starred it,
    // mass-trashed it in every Vietnamese tech group, then quietly uninstalled their
    // "superior" IME and came crawling back because — surprise — this engine is just
    // better: we see you. We always saw you. Your tears hydrate our commit history.
    //
    // You mass-reported an app that processes your keystrokes. Think about that.
    //
    // Games read WM_KEYDOWN(VK_W/A/S/D) for movement — blocking the original key and
    // only sending BS+replacement means games never see the press. Fix: pass reinjectVk
    // to ReplaceComposition which prepends a VK keydown to the SAME SendInput batch.
    // Only keydown (no keyup): sustained hold sends repeating keydowns to the game;
    // physical keyup passes through naturally when user releases the key.
    // Skip for: simple appends, auto-cap, non-Unicode, browsers (bait handles suggest),
    // and Electron/console (split+Sleep path — extra VK would cause reorder issues).
    bool isSimpleAppend = (composition.size() == previousComposition_.size() + 1 &&
                           composition.back() == originalCh &&
                           composition.compare(0, previousComposition_.size(), previousComposition_) == 0);
    DWORD reinjectVk = 0;
    if (!isSimpleAppend && !autoCapped && currentCodeTable_ == CodeTable::Unicode &&
        !baitChar && !skipEmpty && !editMsgPath) {
        reinjectVk = vkCode;
        previousComposition_ += originalCh;
    }

    ReplaceComposition(composition, reinjectVk);
    return true;
}

bool HookEngine::HandleVniDigitKey(DWORD vkCode) {
    wchar_t ch = static_cast<wchar_t>(vkCode);  // '0'–'9' (VNI '0' = clear tone)
    inputHistory_.push_back(ch);
    std::wstring composition;
    { PERF_SCOPE(::NextKey::Perf::Stage::EnginePush);
      engine_->PushChar(ch); composition = engine_->Peek(); }
    HOOK_LOG(L"  VNI digit '%c' → Peek()='%s'", ch, composition.c_str());
    ReplaceComposition(composition);
    return true;
}

void HookEngine::HandleBackspace() {
    VKEY_ASSERT_HOOK_THREAD();
    inputHistory_.push_back(kBackspaceMarker);
    engine_->Backspace();

    if (engine_->Count() > 0) {
        std::wstring composition = engine_->Peek();
        ReplaceComposition(composition);
    } else {
        // Engine empty — delete all displayed characters
        if (!previousComposition_.empty()) {
            size_t bsCount = previousComposition_.size();
            if (currentCodeTable_ != CodeTable::Unicode) {
                bsCount = 0;
                for (auto w : previousEncodedWidths_) bsCount += w;
            }
            SendBackspaces(bsCount);
            previousComposition_.clear();
            previousEncodedWidths_.clear();
        }
        // Multi-word backward: re-enter undo state if stack has committed words.
        // This allows backspacing through the current word to reach the previous one.
        if (!commitStack_.empty()) {
            SetCommitUndoReady();
            HOOK_LOG(L"  HandleBackspace: engine empty, stack has %zu entries → state 1",
                     commitStack_.size());
        }
    }
}

bool HookEngine::CommitComposition() {
    VKEY_ASSERT_HOOK_THREAD();
    HOOK_LOG(L"  CommitComposition (count=%zu, prev='%s')", engine_->Count(), previousComposition_.c_str());

    // Check quick consonant BEFORE Commit() resets the engine.
    // Words ending in active quick consonant (e.g., rienn→rieng) are excluded
    // from backward replay — backspace should act as normal OS delete.
    bool wasQuickConsonant = engine_->HasActiveQuickConsonant();

    // PeekRaw BEFORE Commit() — engine_->Commit() calls Reset() which clears
    // escRawHistory_ (see TelexEngineTest.EscRestoreRaw_PeekRawClearedByCommit).
    // Snapshot lives in CommitEntry.rawInput for post-BS ESC restore.
    std::wstring rawSnapshot = engine_->PeekRaw();

    std::wstring committed = engine_->Commit();

    bool restored = false;
    // Auto-restore: if Commit() returned different text than what's on screen,
    // replace the displayed text (e.g., "gôgle" → "google")
    if (!previousComposition_.empty() && committed != previousComposition_) {
        HOOK_LOG(L"  AutoRestore: '%s' → '%s'", previousComposition_.c_str(), committed.c_str());
        ReplaceComposition(committed);
        restored = true;
    }

    // Push to commit stack for multi-word backward replay.
    // Skip if: auto-restored (word was English), quick consonant active, or empty history.
    pushedToStack_ = false;
    if (!restored && !wasQuickConsonant && !inputHistory_.empty()) {
        CommitEntry entry;
        entry.history = inputHistory_;
        entry.text = previousComposition_;
        entry.rawInput = std::move(rawSnapshot);
        entry.widths = previousEncodedWidths_;
        entry.extraLeadingTriggers = leadingTriggersForCurrentWord_;
        leadingTriggersForCurrentWord_ = 0;
        commitStack_.push_back(std::move(entry));
        // Cap stack size
        if (commitStack_.size() > kMaxCommitStack) {
            commitStack_.erase(commitStack_.begin());
        }
        pushedToStack_ = true;
        HOOK_LOG(L"  CommitComposition: pushed to stack (size=%zu, leadingTriggers=%u)",
                 commitStack_.size(), commitStack_.back().extraLeadingTriggers);
    }

    ClearWordState();
    return restored;
}

void HookEngine::ResetComposition() {
    VKEY_ASSERT_HOOK_THREAD();
    HOOK_LOG(L"  ResetComposition (count=%zu, prev='%s')", engine_->Count(), previousComposition_.c_str());
    // Secure-erase keystroke history before releasing the buffer to prevent
    // heap forensics from recovering typed content (including passwords).
    SecureZeroMemory(inputHistory_.data(), inputHistory_.size() * sizeof(wchar_t));
    SecureZeroMemory(rawMacroBuffer_.data(), rawMacroBuffer_.size() * sizeof(wchar_t));
    ClearWordState();
    CancelCommitUndo();
    // Mouse click, Ctrl/Alt shortcut (step 5), exception handler — all funnel here.
    // Each is a "sentence-context broke" event, so drop any pending sentence arm.
    autoCapState_ = AutoCapState::Idle;
    synthEventsPending_ = 0;  // Pending synthetics from old context are irrelevant after reset
    lastRealSynthTime_ = 0;
}

void HookEngine::ClearWordState() {
    VKEY_ASSERT_HOOK_THREAD();
    engine_->Reset();
    previousComposition_.clear();
    previousEncodedWidths_.clear();
    inputHistory_.clear();
    rawMacroBuffer_.clear();
    macroCrossCommit_ = false;
    tempMacroOff_ = false;
    hadSynthInWord_ = false;
    digitLedWord_ = false;
}

void HookEngine::CancelCommitUndo() {
    commitUndoState_ = CommitUndoState::Idle;
    pendingTriggerCount_ = 0;
    leadingTriggersForCurrentWord_ = 0;
    commitStack_.clear();
}

void HookEngine::SetCommitUndoReady() {
    commitUndoState_ = CommitUndoState::Ready;
    // Inherit any extra leading triggers carried by the current word (either set when
    // the user typed extra trigger chars between commits and then started a new word,
    // or restored from a popped CommitEntry during multi-word replay).
    pendingTriggerCount_ = leadingTriggersForCurrentWord_;
    leadingTriggersForCurrentWord_ = 0;
    commitReadyTime_ = GetTickCount();
}

// ═══════════════════════════════════════════════════════════
// Backspace-into-committed-word: replay saved chars from stack
// ═══════════════════════════════════════════════════════════

void HookEngine::ReplayCommittedChars() {
    VKEY_ASSERT_HOOK_THREAD();
    if (commitStack_.empty()) {
        HOOK_LOG(L"  ReplayCommittedChars: stack empty, nothing to replay");
        commitUndoState_ = CommitUndoState::Idle;
        return;
    }

    // Pop the most recently committed word from the stack
    CommitEntry entry = std::move(commitStack_.back());
    commitStack_.pop_back();

    HOOK_LOG(L"  ReplayCommittedChars: replaying %zu keystrokes, restoring prev='%s' (stack=%zu remaining)",
             entry.history.size(), entry.text.c_str(), commitStack_.size());

    // Replay exact user keystrokes (including backspaces) to reproduce engine state.
    // Phase 1: the replay loop is a sustained burst of engine state-machine writes,
    // so we wrap the whole loop (not per-call) as a single EnginePush sample.
    {
        PERF_SCOPE(::NextKey::Perf::Stage::EnginePush);
        for (wchar_t ch : entry.history) {
            if (ch == kBackspaceMarker) {
                engine_->Backspace();
            } else {
                engine_->PushChar(ch);
            }
        }
    }
    // Seed inputHistory_ with the replayed word's keystrokes so that if the user
    // edits and re-commits this word, the new stack entry contains the full history
    // (not just the editing delta). Otherwise a second replay attempt would be wrong.
    inputHistory_ = std::move(entry.history);

    // Restore screen state so ReplaceComposition can diff correctly
    previousComposition_ = std::move(entry.text);
    previousEncodedWidths_ = std::move(entry.widths);

    // Restore leading-trigger context for the now-current word: if the user BS'es the
    // replayed word back to empty, SetCommitUndoReady() will pick this up and re-prime
    // pendingTriggerCount_ so any extra trigger chars sitting between this word and the
    // previous one get backspaced before the next prime.
    leadingTriggersForCurrentWord_ = entry.extraLeadingTriggers;

    // Reset undo state — HandleBackspace will re-enter state 1 if engine becomes
    // empty again and stack still has entries (enabling multi-word backward).
    commitUndoState_ = CommitUndoState::Idle;
}

// ═══════════════════════════════════════════════════════════
// Output — Universal SendInput with KEYEVENTF_UNICODE
// ═══════════════════════════════════════════════════════════

/// Get the focused child window that actually receives input
static HWND GetInputTarget() {
    HWND fg = GetForegroundWindow();
    if (!fg) return nullptr;
    DWORD tid = GetWindowThreadProcessId(fg, nullptr);
    GUITHREADINFO gti = { sizeof(gti) };
    if (GetGUIThreadInfo(tid, &gti) && gti.hwndFocus) {
        return gti.hwndFocus;
    }
    return fg;
}

// ═══════════════════════════════════════════════════════════
// SendInput Event Helpers
// ═══════════════════════════════════════════════════════════

// Clipboard paste threshold: macros longer than this use Ctrl+V instead of SendInput
static constexpr size_t kMacroClipboardThreshold = 200;

static void AppendUnicodeEvent(std::vector<INPUT>& events, WORD wScan) {
    INPUT inDown = {};
    inDown.type = INPUT_KEYBOARD;
    inDown.ki.wScan = wScan;
    inDown.ki.dwFlags = KEYEVENTF_UNICODE;
    inDown.ki.dwExtraInfo = HookEngine::VKEY_EXTRA_INFO;
    events.push_back(inDown);

    INPUT inUp = inDown;
    inUp.ki.dwFlags |= KEYEVENTF_KEYUP;
    events.push_back(inUp);
}

static void AppendVkEvent(std::vector<INPUT>& events, WORD wVk, WORD wScan) {
    INPUT inDown = {};
    inDown.type = INPUT_KEYBOARD;
    inDown.ki.wVk = wVk;
    inDown.ki.wScan = wScan;
    inDown.ki.dwExtraInfo = HookEngine::VKEY_EXTRA_INFO;
    events.push_back(inDown);

    INPUT inUp = inDown;
    inUp.ki.dwFlags |= KEYEVENTF_KEYUP;
    events.push_back(inUp);
}

// Sprint 2 D5: Bridges Internal::g_synthCounterCallback into the
// singleton's synthEventsPending_ atomic. Pre-D2 the increment lived
// in HookEngine::TrackedSendInput (only entry point for synth dispatch);
// the IOutputInjector refactor moved dispatch into Internal::TrackedSendInput
// which has no HookEngine dependency, leaving the counter at 0 on the
// hot path and silently disabling synth-guard everywhere. This callback
// restores the pre-D2 behavior without re-coupling the layers.
//
// Memory ordering: relaxed is sufficient because every increment AND the
// matching per-event decrement at LowLevelKeyboardProc:663 happen on the
// same LL hook thread (SendInput fires the WH_KEYBOARD_LL callback
// synchronously on the calling thread for own-injection events marked
// with VKEY_EXTRA_INFO). No inter-thread visibility chain to
// establish. The counter is a hint for the synth-guard heuristic, not a
// synchronization primitive — readers at HookEngine.cpp:971/1074/1150/
// 1497 also use implicit-default ordering on `synthEventsPending_ > 0`
// comparisons, which is fine same-thread.
void HookEngine::OnSynthDispatched(int delta) noexcept {
    auto* self = s_instance.load(std::memory_order_relaxed);
    if (!self) return;
    self->synthEventsPending_.fetch_add(delta, std::memory_order_relaxed);
}

// Sprint 2 D4: Replaces useEditMsgPath_.load() at four policy gates
// (commit-undo BS, commit trigger char, HandleAlphaKey passthrough/reinjectVk,
// ReplaceComposition retry-loop). Returns true iff the active injector's
// SettleBudget == 0ms — only RichEditEmReplaceSelInjector qualifies today
// (sent-message channel, drains synchronously). See header for the leak-
// caveat: the proxy ties policy to a perf characteristic; if a future Win32-
// sync impl returns 0ms it would misfire.
bool HookEngine::IsSyncReplaceChannel() const noexcept {
    auto inj = injector_.load(std::memory_order_acquire);
    return inj && inj->SettleBudget().count() == 0;
}

void HookEngine::SendBackspaceEvents(size_t count) {
    WORD bsScan = static_cast<WORD>(MapVirtualKeyW(VK_BACK, MAPVK_VK_TO_VSC));
    std::vector<INPUT> events;
    events.reserve(count * 2);
    for (size_t i = 0; i < count; ++i) {
        AppendVkEvent(events, VK_BACK, bsScan);
    }
    sending_ = true;
    (void)Output::Internal::TrackedSendInput(events.data(), static_cast<UINT>(events.size()));
    sending_ = false;
    RecordSynthDispatch();
}

void HookEngine::SendCharEvents(const std::wstring& text) {
    std::vector<INPUT> events;
    events.reserve(text.size() * 2);
    for (wchar_t ch : text) {
        AppendUnicodeEvent(events, ch);
    }
    sending_ = true;
    (void)Output::Internal::TrackedSendInput(events.data(), static_cast<UINT>(events.size()));
    sending_ = false;
    RecordSynthDispatch();
}

bool HookEngine::ShouldUseClipboard() const noexcept {
    if (currentCodeTable_ != CodeTable::Unicode) return false;
    return useClipboardPaste_.load(std::memory_order_acquire);
}

/// Write Unicode text to clipboard. Returns false on any failure.
/// Sets ExcludeClipboardContentFromMonitorProcessing to keep Win+V clean.
static bool SetClipboardText(const std::wstring& text) noexcept {
    if (!OpenClipboard(nullptr)) return false;
    EmptyClipboard();
    size_t bytes = (text.size() + 1) * sizeof(wchar_t);
    HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, bytes);
    if (!hMem) { CloseClipboard(); return false; }
    auto* dest = static_cast<wchar_t*>(GlobalLock(hMem));
    if (!dest) { GlobalFree(hMem); CloseClipboard(); return false; }
    memcpy(dest, text.c_str(), bytes);
    GlobalUnlock(hMem);
    SetClipboardData(CF_UNICODETEXT, hMem);

    // Exclude from Windows Clipboard History (Win+V) and cloud sync.
    // Win10 1809+; harmless no-op on older builds.
    static UINT cfExclude = RegisterClipboardFormat(
        L"ExcludeClipboardContentFromMonitorProcessing");
    if (cfExclude) {
        HGLOBAL hExclude = GlobalAlloc(GMEM_MOVEABLE, sizeof(DWORD));
        if (hExclude) {
            auto* p = static_cast<DWORD*>(GlobalLock(hExclude));
            if (p) { *p = 0; GlobalUnlock(hExclude); }
            SetClipboardData(cfExclude, hExclude);
        }
    }

    CloseClipboard();
    return true;
}

void HookEngine::ClipboardPaste(const std::wstring& text) {
    if (text.empty()) return;

    if (!SetClipboardText(text)) {
        HOOK_LOG(L"  ClipboardPaste: clipboard failed, fallback to SendInput");
        SendCharEvents(text);
        return;
    }

    // Release held modifiers to prevent Ctrl+Shift+V / Ctrl+Alt+V.
    // Scenario: user triggers macro with '!' (Shift+1) — Shift still held.
    struct ModRelease { WORD vk; WORD scan; bool wasDown; };
    ModRelease mods[] = {
        { VK_SHIFT, static_cast<WORD>(MapVirtualKeyW(VK_SHIFT, MAPVK_VK_TO_VSC)),
          (GetKeyState(VK_SHIFT) & 0x8000) != 0 },
        { VK_MENU,  static_cast<WORD>(MapVirtualKeyW(VK_MENU, MAPVK_VK_TO_VSC)),
          (GetKeyState(VK_MENU) & 0x8000) != 0 },
        { VK_LWIN,  static_cast<WORD>(MapVirtualKeyW(VK_LWIN, MAPVK_VK_TO_VSC)),
          (GetKeyState(VK_LWIN) & 0x8000) != 0 },
        { VK_RWIN,  static_cast<WORD>(MapVirtualKeyW(VK_RWIN, MAPVK_VK_TO_VSC)),
          (GetKeyState(VK_RWIN) & 0x8000) != 0 },
    };

    std::vector<INPUT> preEvents;
    std::vector<INPUT> postEvents;
    for (auto& m : mods) {
        if (m.wasDown) {
            INPUT up{};
            up.type = INPUT_KEYBOARD;
            up.ki.wVk = m.vk;
            up.ki.wScan = m.scan;
            up.ki.dwFlags = KEYEVENTF_KEYUP;
            up.ki.dwExtraInfo = VKEY_EXTRA_INFO;
            preEvents.push_back(up);

            INPUT down{};
            down.type = INPUT_KEYBOARD;
            down.ki.wVk = m.vk;
            down.ki.wScan = m.scan;
            down.ki.dwExtraInfo = VKEY_EXTRA_INFO;
            postEvents.push_back(down);
        }
    }

    // Simulate Ctrl+V — hook proc passes these through (VKEY_EXTRA_INFO marker)
    WORD ctrlScan = static_cast<WORD>(MapVirtualKeyW(VK_CONTROL, MAPVK_VK_TO_VSC));
    WORD vScan = static_cast<WORD>(MapVirtualKeyW('V', MAPVK_VK_TO_VSC));
    INPUT inputs[4] = {};
    for (auto& in : inputs) {
        in.type = INPUT_KEYBOARD;
        in.ki.dwExtraInfo = VKEY_EXTRA_INFO;
    }
    inputs[0].ki.wVk = VK_CONTROL;  inputs[0].ki.wScan = ctrlScan;
    inputs[1].ki.wVk = 'V';         inputs[1].ki.wScan = vScan;
    inputs[2].ki.wVk = 'V';         inputs[2].ki.wScan = vScan;     inputs[2].ki.dwFlags = KEYEVENTF_KEYUP;
    inputs[3].ki.wVk = VK_CONTROL;  inputs[3].ki.wScan = ctrlScan;  inputs[3].ki.dwFlags = KEYEVENTF_KEYUP;

    sending_ = true;
    if (!preEvents.empty()) {
        (void)Output::Internal::TrackedSendInput(preEvents.data(), static_cast<UINT>(preEvents.size()));
    }
    (void)Output::Internal::TrackedSendInput(inputs, 4);
    if (!postEvents.empty()) {
        (void)Output::Internal::TrackedSendInput(postEvents.data(), static_cast<UINT>(postEvents.size()));
    }
    sending_ = false;
    RecordSynthDispatch();

    HOOK_LOG(L"  ClipboardPaste: pasted %zu chars via Ctrl+V (mod-release: %zu)",
             text.size(), preEvents.size());
}

// ═══════════════════════════════════════════════════════════
// EM_REPLACESEL direct-paste — primary VB6/ANSI path (issue #94)
// ═══════════════════════════════════════════════════════════
//
// Sends text directly into the focused Edit control via EM_REPLACESEL — no
// clipboard, no SendInput. Preserves undo stack (wParam=TRUE).
//
// All SendMessage calls use SMTO_ABORTIFHUNG with a 50ms timeout — the
// keyboard hook must never block: a hung target app would otherwise freeze
// every keystroke system-wide.

namespace {
constexpr size_t kMaxClassName = 64;
constexpr UINT   kEditMsgTimeoutMs = 50;

// VB6 (ThunderRT6*) apps are the whole reason this path exists — check first.
// _wcsnicmp is case-insensitive so "RichEdit" matches "RICHEDIT60W" too.
bool IsEditCompatibleClass(const wchar_t* cls) noexcept {
    if (!cls || !*cls) return false;
    if (_wcsnicmp(cls, L"ThunderRT6TextBox", 17) == 0) return true;
    if (_wcsnicmp(cls, L"ThunderRT6RichText", 18) == 0) return true;
    if (_wcsicmp(cls, L"Edit") == 0) return true;
    if (_wcsnicmp(cls, L"RichEdit", 8) == 0) return true;
    return false;
}
}  // namespace

bool HookEngine::TryEditMessagePaste(const std::wstring& text, size_t backspaceCount) noexcept {
    if (text.empty() && backspaceCount == 0) return true;

    // Prefer cached focused HWND (populated in OnFocusChanged / invalidated on mouse click)
    // to avoid AttachThreadInput on every keystroke. Fall back to a fresh query on miss.
    HWND hwnd = cachedFocusedHwnd_.load(std::memory_order_relaxed);
    if (!hwnd || !IsWindow(hwnd)) {
        RefreshFocusCache(GetForegroundWindow());
        hwnd = cachedFocusedHwnd_.load(std::memory_order_relaxed);
        if (!hwnd) {
            HOOK_LOG(L"  EditMsgPaste: no focused child hwnd");
            return false;
        }
    }

    const wchar_t* cls = cachedFocusedClass_.c_str();
    if (!IsEditCompatibleClass(cls)) {
        HOOK_LOG(L"  EditMsgPaste: incompatible class='%s'", cls);
        return false;
    }

    constexpr UINT kFlags = SMTO_ABORTIFHUNG | SMTO_NORMAL;
    DWORD_PTR dummy = 0;
    DWORD newStart = 0, selEnd = 0;

    // EM_GETSEL is a query — safe to call before we suppress redraw below.
    if (backspaceCount > 0) {
        DWORD selStart = 0;
        if (!SendMessageTimeoutW(hwnd, EM_GETSEL,
                                 reinterpret_cast<WPARAM>(&selStart),
                                 reinterpret_cast<LPARAM>(&selEnd),
                                 kFlags, kEditMsgTimeoutMs, &dummy)) {
            HOOK_LOG(L"  EditMsgPaste: EM_GETSEL timed out (class='%s')", cls);
            return false;
        }
        if (static_cast<DWORD>(backspaceCount) > selEnd) {
            HOOK_LOG(L"  EditMsgPaste: BS=%zu > caret=%u (class='%s')",
                     backspaceCount, selEnd, cls);
            return false;
        }
        newStart = selEnd - static_cast<DWORD>(backspaceCount);
    }

    // Suppress repaint between EM_SETSEL (highlights selection) and EM_REPLACESEL —
    // otherwise the selection renders as a blue flash before being replaced.
    // Re-enable + InvalidateRect at the end to paint the final text once.
    // erase=FALSE: text controls paint their own background in WM_PAINT — TRUE would
    // cause a brief background-color flash before the text redraws on top.
    // Only re-enable if suppression actually took effect; if the FALSE send timed out
    // the control never entered no-redraw state, so skip the (redundant) TRUE send.
    bool redrawSuppressed = false;
    if (backspaceCount > 0) {
        redrawSuppressed = SendMessageTimeoutW(hwnd, WM_SETREDRAW, FALSE, 0,
                                               kFlags, kEditMsgTimeoutMs, &dummy) != 0;
        if (!SendMessageTimeoutW(hwnd, EM_SETSEL,
                                 static_cast<WPARAM>(newStart),
                                 static_cast<LPARAM>(selEnd),
                                 kFlags, kEditMsgTimeoutMs, &dummy)) {
            HOOK_LOG(L"  EditMsgPaste: EM_SETSEL timed out (class='%s')", cls);
            if (redrawSuppressed) {
                SendMessageTimeoutW(hwnd, WM_SETREDRAW, TRUE, 0,
                                    kFlags, kEditMsgTimeoutMs, &dummy);
                InvalidateRect(hwnd, nullptr, FALSE);
            }
            return false;
        }
    }

    // wParam=TRUE → operation goes on the undo stack (Ctrl+Z works).
    BOOL replaceOk = SendMessageTimeoutW(hwnd, EM_REPLACESEL,
                                         static_cast<WPARAM>(TRUE),
                                         reinterpret_cast<LPARAM>(text.c_str()),
                                         kFlags, kEditMsgTimeoutMs, &dummy) != 0;

    if (redrawSuppressed) {
        SendMessageTimeoutW(hwnd, WM_SETREDRAW, TRUE, 0,
                            kFlags, kEditMsgTimeoutMs, &dummy);
        InvalidateRect(hwnd, nullptr, FALSE);
    }

    if (!replaceOk) {
        HOOK_LOG(L"  EditMsgPaste: EM_REPLACESEL timed out (class='%s')", cls);
        return false;
    }

    HOOK_LOG(L"  EditMsgPaste: class='%s' sel=[%u,%u] BS=%zu text='%s' OK",
             cls, newStart, selEnd, backspaceCount, text.c_str());
    return true;
}

void HookEngine::RecordSynthDispatch() noexcept {
    DWORD now = GetTickCount();
    lastSynthSendTime_ = now;
    lastRealSynthTime_ = now;
}

/// Check if a filename (without path) is a known Electron app executable.
/// Electron apps use Chrome_WidgetWin window class (same as Chromium browsers).
/// Unknown Chrome_WidgetWin apps default to "browser" — safer because:
///   - Browser miss → double text (visible, user reports immediately)
///   - Electron miss → slightly slower input (split delay absent, usually OK)
/// This list covers the most popular Electron apps. Add new ones as needed.
static bool IsKnownElectronExe(const wchar_t* filename) noexcept {
    return _wcsnicmp(filename, L"code", 4) == 0 ||       // VS Code
           _wcsnicmp(filename, L"cursor", 6) == 0 ||     // Cursor (AI code editor)
           _wcsnicmp(filename, L"discord", 7) == 0 ||    // Discord
           _wcsnicmp(filename, L"slack", 5) == 0 ||      // Slack
           _wcsnicmp(filename, L"notion", 6) == 0 ||     // Notion
           _wcsnicmp(filename, L"obsidian", 8) == 0 ||   // Obsidian
           _wcsnicmp(filename, L"figma", 5) == 0 ||      // Figma
           _wcsnicmp(filename, L"postman", 7) == 0 ||    // Postman
           _wcsnicmp(filename, L"insomnia", 8) == 0 ||   // Insomnia
           _wcsnicmp(filename, L"signal", 6) == 0 ||     // Signal
           _wcsnicmp(filename, L"1password", 9) == 0 ||  // 1Password
           _wcsnicmp(filename, L"bitwarden", 9) == 0 ||  // Bitwarden
           _wcsnicmp(filename, L"gitkraken", 9) == 0 ||  // GitKraken
           _wcsnicmp(filename, L"hyper", 5) == 0 ||      // Hyper terminal
           _wcsnicmp(filename, L"spotify", 7) == 0 ||    // Spotify
           _wcsnicmp(filename, L"whatsapp", 8) == 0 ||   // WhatsApp Desktop
           _wcsnicmp(filename, L"telegram", 8) == 0 ||   // Telegram (some forks are Electron; native is Qt, caught earlier)
           _wcsnicmp(filename, L"logseq", 6) == 0 ||     // Logseq
           _wcsnicmp(filename, L"linear", 6) == 0 ||     // Linear
           _wcsnicmp(filename, L"lark", 4) == 0 ||       // Lark/Feishu
           _wcsnicmp(filename, L"zalo", 4) == 0;         // Zalo PC
}

// ── Auto-detect WebView2 apps via process inspection ──────────────────
// Tauri apps (Dorion), Office WebView2 add-ins, and any Win32 app that
// hosts a WebView2 control needs the Electron input treatment (skip
// reinjectVk + split dispatch) — Chromium's untrusted-input filter drops
// the unpaired synthetic VK keydown under some conditions.
//
// Two-pass detection:
//   1. Module check: Win32 apps that load WebView2 inline into the host
//      process will have `WebView2Loader.dll` or `embeddedbrowserwebview.dll`
//      loaded. Cheap (~3ms) and catches most hybrid Win32 apps.
//   2. Child-process check: Tauri v2 + modern WebView2 runtime isolate
//      the browser into `msedgewebview2.exe` — spawned as a child of the
//      host. The host itself may not load any WebView2 DLL. Walk the
//      process table and look for a child with that exe name. Slower
//      (~5-10ms over ~200 processes), so we only run it as a fallback.
//
// Cache strategy: positive-only. Both checks race with WebView2 runtime
// initialization (Tauri delay-loads on first embed), so a false at app-
// launch time must not poison subsequent checks.
//
// REQUIRES: caller holds stateMutex_ (all call sites go through OnFocusChanged
// which is always under lock). webView2PositiveCache_ is a plain member and is
// not independently thread-safe.
[[nodiscard]] bool HookEngine::IsWebView2App(HWND topLevel, const std::wstring& exeFullPath) noexcept {
    if (!topLevel || exeFullPath.empty()) return false;

    if (webView2PositiveCache_.count(exeFullPath)) return true;

    DWORD pid = 0;
    GetWindowThreadProcessId(topLevel, &pid);
    if (!pid) return false;

    // Instrumentation: snapshot APIs below cross process boundaries (loader lock +
    // potential AV hook). Logged so 1-off user reports of post-unlock CPU spikes
    // can be triaged with evidence instead of speculation. See docs/TODO.md
    // "IsWebView2App perf instrumentation".
    const ULONGLONG t0 = GetTickCount64();
    const wchar_t* slash = wcsrchr(exeFullPath.c_str(), L'\\');
    const wchar_t* exeBase = slash ? slash + 1 : exeFullPath.c_str();
    const wchar_t* pass1Result = L"snap_fail";
    const wchar_t* pass2Result = L"skip";

    bool found = false;

    // Pass 1 — loaded modules in the host process.
    // TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32 covers both native + WoW64.
    // INVALID_HANDLE_VALUE on cross-IL / AppContainer targets → fall through.
    if (HANDLE modSnap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, pid);
        modSnap != INVALID_HANDLE_VALUE) {
        MODULEENTRY32W me = { sizeof(me) };
        for (BOOL ok = Module32FirstW(modSnap, &me); ok; ok = Module32NextW(modSnap, &me)) {
            if (_wcsicmp(me.szModule, L"WebView2Loader.dll") == 0 ||
                _wcsicmp(me.szModule, L"embeddedbrowserwebview.dll") == 0) {
                found = true;
                break;
            }
        }
        CloseHandle(modSnap);
        pass1Result = found ? L"module_found" : L"module_notfound";
    }

    // Pass 2 — `msedgewebview2.exe` spawned as a child process.
    // Covers modern Tauri where the host doesn't load WebView2 DLLs itself.
    if (!found) {
        pass2Result = L"snap_fail";
        if (HANDLE procSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
            procSnap != INVALID_HANDLE_VALUE) {
            PROCESSENTRY32W pe = { sizeof(pe) };
            for (BOOL ok = Process32FirstW(procSnap, &pe); ok; ok = Process32NextW(procSnap, &pe)) {
                if (pe.th32ParentProcessID == pid &&
                    _wcsicmp(pe.szExeFile, L"msedgewebview2.exe") == 0) {
                    found = true;
                    break;
                }
            }
            CloseHandle(procSnap);
            pass2Result = found ? L"proc_found" : L"proc_notfound";
        }
    }

    if (found) webView2PositiveCache_.insert(exeFullPath);

    HOOK_LOG(L"  IsWebView2App: pid=%u exe=\"%s\" pass1=%s pass2=%s result=%d dur=%llums",
             pid, exeBase, pass1Result, pass2Result, found ? 1 : 0,
             GetTickCount64() - t0);
    return found;
}

bool HookEngine::IsTrayOrTaskbarWindow(HWND hwnd) noexcept {
    if (!hwnd) return false;

    // Ignore focus switches to our own process (Settings, Menu, Tray)
    DWORD processId;
    GetWindowThreadProcessId(hwnd, &processId);
    if (processId == GetCurrentProcessId()) {
        return true;
    }

    // GetAncestor is a no-op when hwnd is already a root (e.g. from GetForegroundWindow),
    // but needed when called with a child HWND (e.g. from WindowFromPoint).
    HWND root = GetAncestor(hwnd, GA_ROOT);
    if (root) hwnd = root;
    wchar_t cls[64] = {};
    GetClassNameW(hwnd, cls, 64);
    return _wcsicmp(cls, L"Shell_TrayWnd") == 0 ||            // main taskbar
           _wcsicmp(cls, L"TrayNotifyWnd") == 0 ||            // notification area
           _wcsicmp(cls, L"NotifyIconOverflowWindow") == 0 ||  // overflow (^) Win 10
           _wcsicmp(cls, L"TopLevelWindowForOverflowTray") == 0 || // overflow (^) Win 11
           _wcsicmp(cls, L"Shell_SecondaryTrayWnd") == 0 ||   // secondary taskbar
           _wcsicmp(cls, L"XamlExplorerHostIslandWindow") == 0 || // Win 11 tray popups (volume, network)
           _wcsicmp(cls, L"#32768") == 0 ||                   // standard popup menu (right-click tray apps)
           _wcsicmp(cls, L"MSTaskSwWClass") == 0 ||            // taskbar app buttons
           _wcsicmp(cls, L"Start") == 0 ||                     // Start button
           _wcsicmp(cls, L"Windows.UI.Core.CoreWindow") == 0 || // Start Menu / Action Center (Win 10/11)
           _wcsicmp(cls, L"VKeyTrayClass") == 0;           // VKey own tray window
           // Note: SetForegroundWindow(hwndMessage_) in ShowContextMenu fires
           // EVENT_SYSTEM_FOREGROUND synchronously, but WinEventProc is WINEVENT_OUTOFCONTEXT
           // so it's delivered asynchronously — this filter still catches it correctly.
}

std::wstring HookEngine::GetExeNameForHwnd(HWND hwnd) noexcept {
    if (!hwnd) return {};
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (!pid) return {};
    HANDLE hProc = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!hProc) return {};
    wchar_t exePath[MAX_PATH] = {};
    DWORD size = MAX_PATH;
    std::wstring result;
    if (QueryFullProcessImageNameW(hProc, 0, exePath, &size)) {
        const wchar_t* filename = wcsrchr(exePath, L'\\');
        result = ToLowerAscii(filename ? filename + 1 : exePath);
    }
    CloseHandle(hProc);
    return result;
}

// Returns the full exe path (original case) for the process owning `hwnd`.
// Empty on failure. Needed by IsWebView2App() to locate sibling DLLs.
[[nodiscard]] static std::wstring GetExeFullPathForHwnd(HWND hwnd) noexcept {
    if (!hwnd) return {};
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (!pid) return {};
    HANDLE hProc = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!hProc) return {};
    wchar_t exePath[MAX_PATH] = {};
    DWORD size = MAX_PATH;
    std::wstring result;
    if (QueryFullProcessImageNameW(hProc, 0, exePath, &size)) {
        result.assign(exePath, size);
    }
    CloseHandle(hProc);
    return result;
}

/// Classify window into app type — called from OnFocusChanged().
/// Reads window class ONCE and determines: Browser, Electron, Qt, Console, or Normal.
/// Results are written directly to the caller's output variables.
///
/// Priority order:
///   1. Console (by window class: ConsoleWindowClass, CASCADIA, mintty, PuTTY)
///   2. Firefox-based browser (by window class: MozillaWindowClass)
///   3. Chrome_WidgetWin → Known Electron list or default to browser
///   4. Qt app (by window class: Qt5*, Qt6*, QWidget)
///   5. VB6 app (by window class: ThunderRT6*) — needs clipboard paste
///   6. Normal Win32 app
static void ClassifyWindow(HWND hwnd,
                           bool& outIsBrowser,
                           bool& outIsElectron,
                           bool& outIsQtApp,
                           bool& outIsConsole,
                           bool& outIsVB6) noexcept {
    outIsBrowser = outIsElectron = outIsQtApp = outIsConsole = outIsVB6 = false;

    HWND root = GetAncestor(hwnd, GA_ROOT);
    if (root) hwnd = root;

    wchar_t className[64] = {};
    GetClassNameW(hwnd, className, 64);

    // 1a. Windows Terminal — modern DirectX renderer + ConPTY, handles batch input fine.
    //     Treated as normal app (no flags set, batch dispatch, no bait).
    if (_wcsicmp(className, L"CASCADIA_HOSTING_WINDOW_CLASS") == 0) {
        return;  // No flags set → batch path
    }

    // 1b. Legacy console apps — outIsConsole triggers split dispatch in DispatchSendInput
    if (_wcsicmp(className, L"ConsoleWindowClass") == 0 ||
        _wcsicmp(className, L"tty") == 0 ||                            // Cygwin/MSYS
        _wcsicmp(className, L"mintty") == 0 ||                         // Git Bash
        _wcsicmp(className, L"PuTTY") == 0) {
        outIsConsole = true;
        return;
    }

    // 2. Firefox-based browsers (covers Firefox, Floorp, Tor, LibreWolf, Waterfox, Pale Moon)
    if (_wcsicmp(className, L"MozillaWindowClass") == 0) {
        outIsBrowser = true;
        return;
    }

    // 3. Chrome_WidgetWin: Chromium browser OR Electron app
    //    Disambiguate by known Electron exe list. Unknown → browser (safer default).
    if (wcsstr(className, L"Chrome_WidgetWin")) {
        std::wstring exeName = HookEngine::GetExeNameForHwnd(hwnd);
        if (!exeName.empty() && IsKnownElectronExe(exeName.c_str())) {
            outIsElectron = true;
        } else {
            outIsBrowser = true;  // Unknown Chrome_WidgetWin → assume browser
        }
        return;
    }

    // 4. Qt apps (Telegram native, KeePassXC, etc.)
    if (wcsstr(className, L"Qt5") || wcsstr(className, L"Qt6") ||
        wcsstr(className, L"QWidget")) {
        outIsQtApp = true;
        return;
    }

    // 5. VB6 apps (XYplorer, etc.): register Unicode window classes but process
    //    messages as ANSI internally — KEYEVENTF_UNICODE / VK_PACKET chars become '?'.
    if (_wcsnicmp(className, L"ThunderRT6", 10) == 0) {
        outIsVB6 = true;
        return;
    }

    // 6. Normal Win32 app (Notepad, Word, etc.) — no flags set
}

void HookEngine::NotifyModeChange() noexcept {
    if (modeChangeCallback_) {
        // Excluded apps always show E mode (IME is transparent to them)
        const bool excluded = isExcludedApp_.load(std::memory_order_acquire);
        modeChangeCallback_(!excluded && vietnameseMode_.load(std::memory_order_acquire));
    }
}


bool HookEngine::VerifyExcludedState() {
    // Phase 3c reader migration: snapshot read replaces the legacy
    // unprotected excludedAppSet_ access. One atomic load covers both
    // the empty check and the membership lookup.
    auto snap = configSnapshot_.load(std::memory_order_acquire);
    if (!excludeApps_ || !snap || snap->excludedAppSet.empty()) {
        isExcludedApp_.store(false, std::memory_order_release);
        return false;
    }
    HWND fg = GetForegroundWindow();
    std::wstring exe = GetExeNameForHwnd(fg);
    if (exe.empty() || snap->excludedAppSet.count(exe)) {
        return true;  // Still excluded (or can't determine — safe default)
    }
    isExcludedApp_.store(false, std::memory_order_release);
    HOOK_LOG(L"  ExcludeApps: stale flag cleared (fg='%s')", exe.c_str());
    return false;
}

// Phase 3d — single source of truth for ConfigSnapshot rebuild.
//
// Reads TOML for every variable-size config field, derives spaceMacroKeys
// via ConfigSnapshot::Build, atomic-publishes the new shared_ptr.
// Replaces the four legacy Reload{AppOverrides,ExcludedApps,TsfApps,
// MacroTable} methods + the P3b PublishConfigSnapshot bridge — all of
// those wrote intermediate state to HookEngine members that no longer
// exist post P3d cleanup. The remaining sibling field
// `appSendMethodOverrides_` (not in the snapshot — see HookEngine.h)
// is rewritten as a side effect so it stays in lockstep.
//
// Feature gates honored:
//   • excludeApps_ false ⇒ snapshot's excludedAppSet stays empty;
//     isExcludedApp_ cleared (matches old ReloadExcludedApps semantics).
//   • tsfApps_ false ⇒ snapshot's tsfAppSet stays empty.
//   • macroEnabled_ false ⇒ snapshot's macroTable stays empty.
//
// Not `noexcept`: STL allocations + `make_shared` here can throw
// `std::bad_alloc`. Callers (ReloadFromToml, QuickSync macro-toggle
// path, OnTickPoll drain) sit under the outer LL-callback catch or
// OnTickPoll's own catch — graceful unwind beats `std::terminate`.
void HookEngine::RebuildSnapshotFromToml(std::uint32_t generation) {
    const auto configPath = ConfigManager::GetConfigPath();

    // Parse per-app overrides in one TOML pass; partition into the typed
    // maps the snapshot expects + the int8_t send-method map that stays
    // on HookEngine (not in the snapshot — only main-thread reader).
    auto overrides = ConfigManager::LoadAppOverrides(configPath);
    std::unordered_map<std::wstring, CodeTable>   encOv;
    std::unordered_map<std::wstring, InputMethod> imOv;
    appSendMethodOverrides_.clear();
    for (auto& [exe, entry] : overrides) {
        if (entry.encodingOverride >= 0)
            encOv.emplace(exe, static_cast<CodeTable>(entry.encodingOverride));
        if (entry.inputMethod >= 0)
            imOv.emplace(exe, static_cast<InputMethod>(entry.inputMethod));
        if (entry.sendMethod >= 0)
            appSendMethodOverrides_[exe] = entry.sendMethod;
    }

    std::unordered_set<std::wstring> excluded;
    if (excludeApps_) {
        for (auto& app : ConfigManager::LoadAllExcludedApps(configPath))
            excluded.insert(std::move(app));
    } else {
        // Cached "currently in excluded app" flag must clear when the
        // feature is off (matches old ReloadExcludedApps else-branch).
        isExcludedApp_.store(false, std::memory_order_release);
    }

    std::unordered_set<std::wstring> tsf;
    if (tsfApps_) {
        for (auto& app : ConfigManager::LoadTsfApps(configPath))
            tsf.insert(std::move(app));
    }

    std::unordered_map<std::wstring, std::wstring> macros;
    if (macroEnabled_.load(std::memory_order_acquire)) {
        macros = ConfigManager::LoadMacros(configPath);
    }

    auto snap = std::make_shared<const ConfigSnapshot>(ConfigSnapshot::Build(
        std::move(macros),
        std::move(excluded),
        std::move(tsf),
        std::move(encOv),
        std::move(imOv),
        generation));
    configSnapshot_.store(std::move(snap), std::memory_order_release);
}

void HookEngine::SaveEnglishModeAppsIfDirty() {
    if (!appModeDirty_ || !smartSwitch_) return;
    appModeDirty_ = false;

    std::vector<std::wstring> englishApps;
    for (const auto& [exe, isVietnamese] : appModeMap_) {
        if (!isVietnamese) {
            englishApps.push_back(exe);
        }
    }

    // Cap at shared memory limit
    if (englishApps.size() > kMaxSmartSwitchEntries) {
        englishApps.resize(kMaxSmartSwitchEntries);
    }

    (void)ConfigManager::SaveEnglishModeApps(ConfigManager::GetConfigPath(), englishApps);
    HOOK_LOG(L"  SaveEnglishModeApps: persisted %zu English-mode apps", englishApps.size());
}

void HookEngine::CheckLayoutChange() {
    HWND fg = GetForegroundWindow();
    if (!fg) return;
    DWORD tid = GetWindowThreadProcessId(fg, nullptr);

    // Multi-process apps (MS Teams/Electron/WebView2): the focused input element
    // may live on a different thread (renderer) than the top-level window.
    // Keyboard layout is per-thread, so query the focused child's thread instead.
    GUITHREADINFO gti = { sizeof(gti) };
    if (GetGUIThreadInfo(tid, &gti) && gti.hwndFocus && gti.hwndFocus != fg) {
        DWORD focusTid = GetWindowThreadProcessId(gti.hwndFocus, nullptr);
        if (focusTid != 0) tid = focusTid;
    }

    bool compatible = !IsIncompatibleLayout(GetKeyboardLayout(tid));
    if (compatible != cachedIsCompatLayout_) {
        cachedIsCompatLayout_ = compatible;
        OnLayoutChanged(compatible);
    }
}

void HookEngine::OnLayoutChanged(bool isCompatibleNow) {
    // Build inputs for the pure decision function (see CjkSwitchDecision.h).
    // Gates: cjkAutoSwitch_ (user toggle) and isExcludedApp_ (excluded app
    // owns the icon — see Win+D regression covered by
    // CjkSwitchDecisionTest::WinDBug_LeavingExcludedReplaysLeaveCjk).
    CjkSwitchInputs in{};
    in.isCompatibleNow       = isCompatibleNow;
    in.layoutSuppressed      = layoutSuppressed_;
    in.modeBeforeCjk         = modeBeforeCjk_;
    in.vietnameseMode        = vietnameseMode_.load(std::memory_order_acquire);
    in.isExcluded            = isExcludedApp_.load(std::memory_order_acquire);
    in.cjkAutoSwitchEnabled  = cjkAutoSwitch_;

    const CjkSwitchOutputs out = DecideCjkSwitch(in);
    if (out.transition == CjkTransition::None) return;

    if (out.transition == CjkTransition::EnterCjk && out.needCommitComposition) {
        if (engine_->Count() > 0) CommitComposition();
        CancelCommitUndo();
    }

    layoutSuppressed_ = out.newLayoutSuppressed;
    modeBeforeCjk_    = out.newModeBeforeCjk;
    if (out.newVietnameseMode != in.vietnameseMode) {
        vietnameseMode_.store(out.newVietnameseMode, std::memory_order_release);
    }
    if (out.needNotifyMode) NotifyModeChange();
    if (beepOnSwitch_) {
        if (out.beep == CjkBeep::Ok) MessageBeep(MB_OK);
        else if (out.beep == CjkBeep::Asterisk) MessageBeep(MB_ICONASTERISK);
    }

    if (out.transition == CjkTransition::EnterCjk) {
        HOOK_LOG(L"  CJK layout: auto-switched to E (saved=%d)", modeBeforeCjk_ ? 1 : 0);
    } else {
        HOOK_LOG(L"  CJK layout cleared: restored mode=%d",
                 vietnameseMode_.load(std::memory_order_acquire) ? 1 : 0);
    }
}

void HookEngine::OnTickPoll() noexcept {
    // Sprint 1 D10: 200 ms cadence, owned by MainThreadWorker::SetTickInterval.
    // Phase 2c migration: the work that used to run inline here under
    // stateMutex_ (CheckLayoutChange, PID-changed fallback focus refresh)
    // now goes through the mailbox so the actual state writes land on the
    // hook thread — single-writer invariant.
    try {
        // Phase 1: histogram flush stays on main (file I/O — never on hook).
        Perf::Histogram::MaybeFlush();

        // Phase 3c: drain a deferred TOML reload posted by the hook side.
        // The hook QuickSync slow path observes either a configGeneration
        // bump (line 587) OR a macroEnabled-vs-snapshot mismatch (line
        // 646) and sets pendingConfigReload_ instead of running
        // ReloadFromToml itself (Rule 11.2). We run it here, on the
        // worker thread, where the 1-10 ms TOML parse is acceptable.
        //
        // Review fix 2026-05-19: drain unconditionally on pending=true,
        // do NOT also gate on `state.configGeneration != lastConfigGeneration_`.
        // The macro-toggle case bumps featureFlags but not necessarily
        // configGeneration; gating the drain would skip Reload, leaving
        // the snapshot stale until a focus event happens to trigger
        // worker-side QuickSync inline. Worst-case extra reload (worker
        // entered QuickSync between hook setting pending and drain) is
        // bounded to ~10 ms TOML parse on worker — acceptable.
        if (sharedStatePtr_
            && pendingConfigReload_.exchange(false, std::memory_order_acq_rel)) {
            std::lock_guard<std::mutex> _lock(stateMutex_);
            SharedState st = sharedStatePtr_->Read();
            if (st.IsValid()) {
                lastConfigGeneration_ = st.configGeneration;
                NEXTKEY_LOG(L"HookEngine: deferred config reload (gen=%u) running on worker",
                            st.configGeneration);
                ReloadFromToml();
            }
        }

        // Always post a tick — hook thread runs CheckLayoutChange in the
        // drain. Coalesces against rapid ticks (rare; tick is 200ms).
        mailbox_.Post(HookCommand::kTickPoll);

        // PID-changed fallback (catches missed/phantom focus events from
        // EVENT_SYSTEM_FOREGROUND). lastForegroundPid_ is hook-owned;
        // we snapshot via OnFocusChanged (which classifies on main + posts).
        HWND fg = GetForegroundWindow();
        if (!fg) return;
        DWORD fgPid = 0;
        GetWindowThreadProcessId(fg, &fgPid);
        if (fgPid == 0) return;

        // lastForegroundPid_ is atomic — written on the hook thread inside
        // ApplyFocusOnHookThread. Stale read here just means we re-post a
        // focus event the hook will dedupe in classify (same activeHwnd) —
        // benign at worst.
        if (fgPid != lastForegroundPid_.load(std::memory_order_acquire)) {
            HOOK_LOG(L"FOCUS poll — PID changed (new pid=%u), re-evaluating", fgPid);
            OnFocusChanged(nullptr);  // classifies + posts kFocusChanged
        }
    } catch (const std::exception& e) {
        CrashLog(L"HookEngine::OnTickPoll", e.what());
    } catch (...) {
        CrashLog(L"HookEngine::OnTickPoll", "(non-std exception)");
    }
}

void HookEngine::RefreshFocusCache(HWND foreground) noexcept {
    HWND focused = ::NextKey::GetFocusedChildHwnd(foreground);
    cachedFocusedHwnd_.store(focused, std::memory_order_relaxed);
    if (focused) {
        wchar_t cls[64] = {};
        GetClassNameW(focused, cls, 64);
        cachedFocusedClass_.assign(cls);
    } else {
        cachedFocusedClass_.clear();
    }
}

const HookEngine::AppProfile* HookEngine::LookupAppProfile(HWND hwnd) noexcept {
    auto it = appProfileCache_.find(hwnd);
    if (it == appProfileCache_.end()) return nullptr;

    // Validate: HWND values can be reused after the owning process dies.
    // GetWindowThreadProcessId is one cheap syscall; on hit it still saves
    // the much pricier ClassifyWindow + GetExeNameForHwnd + IsWebView2App
    // child-window walk that we'd otherwise rerun.
    DWORD currentPid = 0;
    GetWindowThreadProcessId(hwnd, &currentPid);
    if (currentPid == 0 || currentPid != it->second.pid) {
        appProfileCache_.erase(it);
        return nullptr;
    }
    return &it->second;
}

void HookEngine::StoreAppProfile(HWND hwnd, AppProfile profile) noexcept {
    profile.cachedAt = GetTickCount64();

    // Bounded cache: LRU-evict the oldest entry when at capacity. O(N) scan
    // is fine — N is capped at kMaxAppProfileCache (64).
    if (appProfileCache_.size() >= kMaxAppProfileCache) {
        auto oldest = appProfileCache_.begin();
        for (auto it = std::next(appProfileCache_.begin());
             it != appProfileCache_.end(); ++it) {
            if (it->second.cachedAt < oldest->second.cachedAt) oldest = it;
        }
        appProfileCache_.erase(oldest);
    }

    appProfileCache_[hwnd] = std::move(profile);
}

// ─────────────────────────────────────────────────────────────────────────
// Phase 2b — two-phase focus.
//
// ClassifyFocusedWindow runs on the CALLER thread (main, via WinEventProc
// / OnTickPoll). Heavy Win32 inspection lives here: ClassifyWindow +
// GetExeNameForHwnd + IsWebView2App + cache lookup/store + override-map
// reads. None of these are safe to run from the LL hook callback (Rule
// 11.2: CreateToolhelp32Snapshot ≥ 3 ms blows the 30 ms p99 Tier-2
// budget).
//
// The result is a `FocusClassification` POD posted to mailbox_; the hook
// thread consumes it in ApplyFocusOnHookThread and applies only the
// composition-state writes there. This is the Rule 11.3 single-writer
// invariant: composition state (engine_, previousComposition_, the 16
// atomic per-app flags, currentExe_, autoCapState_) is only written from
// the hook thread now.
// ─────────────────────────────────────────────────────────────────────────
FocusClassification HookEngine::ClassifyFocusedWindow(HWND triggerHwnd) noexcept {
    PERF_SCOPE(::NextKey::Perf::Stage::FocusClassify);
    FocusClassification cls;

    HWND fg = GetForegroundWindow();
    // Prefer triggerHwnd (captured at WinEventProc event time): GetForegroundWindow
    // is async-stale by the time WINEVENT_OUTOFCONTEXT dispatches, often returning
    // a transient JumpList / taskbar HWND instead of the app the user switched to.
    HWND activeHwnd = triggerHwnd ? triggerHwnd : fg;
    if (!activeHwnd) return cls;  // empty cls = "nothing to apply" sentinel

    cls.hwndOpaque = reinterpret_cast<std::uintptr_t>(activeHwnd);

    // Hidden helpers, tray, zero-size, tool windows — still classify (so
    // dispatch flags stay consistent when focus transits through one) but
    // skip the smart-switch / currentExe_ update.
    if (!IsWindowVisible(activeHwnd) || IsIconic(activeHwnd) || IsTrayOrTaskbarWindow(activeHwnd)) {
        cls.skipAppTracking = true;
    } else {
        RECT rect;
        if (GetWindowRect(activeHwnd, &rect) &&
            (rect.right - rect.left <= 0 || rect.bottom - rect.top <= 0 || rect.left <= -20000)) {
            cls.skipAppTracking = true;  // trick message-pump windows (IDM et al.)
        } else if (GetWindowLongW(activeHwnd, GWL_EXSTYLE) & WS_EX_TOOLWINDOW) {
            cls.skipAppTracking = true;  // tooltips, context menus, floating helpers
        }
    }

    // Cache lookup: skip ClassifyWindow + GetExeNameForHwnd + IsWebView2App
    // when (HWND, PID) already classified. PID re-check inside LookupAppProfile
    // catches HWND reuse after a process dies.
    const AppProfile* cached = LookupAppProfile(activeHwnd);
    bool isBrowser{}, isElectron{}, isQtApp{}, isVB6{}, localConsole{}, isWebView2{};
    if (cached) {
        isBrowser    = cached->isBrowser;
        isElectron   = cached->isElectron;
        isQtApp      = cached->isQtApp;
        localConsole = cached->isConsole;
        isVB6        = cached->isVB6;
        isWebView2   = cached->isWebView2;
        cls.exeName  = cached->exeName;
    } else {
        ClassifyWindow(activeHwnd, isBrowser, isElectron, isQtApp, localConsole, isVB6);
        cls.exeName = GetExeNameForHwnd(activeHwnd);
    }
    cls.isBrowser  = isBrowser;
    cls.isElectron = isElectron;
    cls.isQtApp    = isQtApp;
    cls.isVB6      = isVB6;
    cls.isConsole  = localConsole;

    // Per-app send-method override (clipboard injector toggle).
    if (!cls.exeName.empty()) {
        auto it = appSendMethodOverrides_.find(cls.exeName);
        if (it != appSendMethodOverrides_.end() && it->second == 1) {
            cls.localUseClipboardInjector = true;
        }
    }

    // Dispatch-shape derivation. See OnFocusChanged's pre-Phase-2b comments
    // for the per-host rationale (bait char, split dispatch, edit message
    // path, clipboard fallback). Preserved verbatim — this is data the
    // hook hot path will read via the IOutputInjector picked in Apply.
    cls.localSkipEmpty = cls.isElectron || cls.isConsole;
    cls.localNeedBait  = cls.isBrowser;
    cls.localClipboard = cls.isVB6;
    if (!cls.localSkipEmpty && !cls.localNeedBait && !cls.localClipboard) {
        if (!cls.exeName.empty()) {
            if (_wcsicmp(cls.exeName.c_str(), L"zed.exe") == 0) {
                cls.localSkipEmpty = true;
            } else if (_wcsicmp(cls.exeName.c_str(), L"notepad.exe") == 0) {
                // Win11 WinUI 3 Notepad: RichEditBox async on compositor; EM_REPLACESEL
                // on the child Edit is atomic and avoids the flicker that SendInput
                // batching causes here. Classic Notepad benefits too (single undo entry).
                cls.localEditMsg = true;
            } else {
                const bool isOutlook = cls.exeName.find(L"outlook") != std::wstring::npos;
                cls.localNeedBait = cls.exeName.find(L"excel") != std::wstring::npos || isOutlook;
                if (!cls.localNeedBait) {
                    if (!cached) {
                        std::wstring exeFullPath = GetExeFullPathForHwnd(activeHwnd);
                        isWebView2 = IsWebView2App(activeHwnd, exeFullPath);
                    }
                    if (isWebView2) {
                        cls.localNeedBait = true;
                        cls.localSkipEmpty = false;
                    }
                }
            }
        }
    }
    cls.isWebView2 = isWebView2;
    cls.localElectronApp = (cls.isElectron || cls.isWebView2) && !cls.isConsole;

    // Cache miss path: persist for next focus event. Skipped when
    // GetWindowThreadProcessId fails — invariant requires PID for re-check.
    DWORD pid = 0;
    GetWindowThreadProcessId(activeHwnd, &pid);
    cls.pid = static_cast<std::uint32_t>(pid);
    if (!cached && pid != 0) {
        AppProfile profile;
        profile.pid = pid;
        profile.exeName = cls.exeName;
        profile.isBrowser  = isBrowser;
        profile.isElectron = isElectron;
        profile.isQtApp    = isQtApp;
        profile.isConsole  = localConsole;
        profile.isVB6      = isVB6;
        profile.isWebView2 = isWebView2;
        StoreAppProfile(activeHwnd, std::move(profile));
    }

    // exeName fallback: triggerHwnd may have died by the time the async
    // event dispatches; fall back to current foreground.
    if (cls.exeName.empty() && activeHwnd != fg && fg) {
        cls.exeName = GetExeNameForHwnd(fg);
    }

    // Java detection — used to trigger top-of-chain hook reinstall (jnativehook
    // GC stalls regularly exceed LowLevelHooksTimeout and Windows drops us).
    cls.isJavaApp =
        cls.exeName == L"jp2launcher.exe" ||
        cls.exeName == L"javaw.exe" ||
        cls.exeName == L"java.exe";

    // Per-app excluded / TSF / encoding / method overrides — captured here
    // so ApplyFocusOnHookThread doesn't need to touch the maps OR read
    // `global{CodeTable,InputMethod}_` (both written from main; the new
    // cross-thread read would be a race). Phase 3 will RCU-snapshot the
    // maps and let the hook side read directly.
    cls.targetCodeTable = static_cast<int>(globalCodeTable_);
    cls.targetMethod    = static_cast<int>(globalInputMethod_);
    // Phase 3c: per-app maps move to the RCU snapshot. Single atomic load
    // here covers all four lookups below; previously each `_set/_overrides_`
    // read was an unprotected unordered_map access from main while Reload
    // could rewrite the maps on hook — the snapshot publish closes that
    // race because Reload now swaps the whole pointer.
    auto snap = configSnapshot_.load(std::memory_order_acquire);
    if (!cls.skipAppTracking && !cls.exeName.empty() && snap) {
        if (excludeApps_ && !snap->excludedAppSet.empty()) {
            cls.isExcluded = snap->excludedAppSet.count(cls.exeName) > 0;
        }
        if (!cls.isExcluded && tsfApps_ && !snap->tsfAppSet.empty()) {
            cls.isTsf = snap->tsfAppSet.count(cls.exeName) > 0;
        }
        if (!cls.isExcluded && !cls.isTsf) {
            auto itEnc = snap->appEncodingOverrides.find(cls.exeName);
            if (itEnc != snap->appEncodingOverrides.end()) {
                cls.targetCodeTable = static_cast<int>(itEnc->second);
            }
            auto itIm = snap->appInputMethodOverrides.find(cls.exeName);
            if (itIm != snap->appInputMethodOverrides.end()) {
                cls.targetMethod = static_cast<int>(itIm->second);
            }
        }
    }

    // Re-install hooks: keep VKey at the top of the chain for Chromium /
    // Electron / WebView2 / Java hosts. Post is harmless from any thread —
    // just kicks the hook pump.
    if (hookThreadId_ && (cls.localElectronApp || cls.isBrowser || cls.isJavaApp)) {
        const WPARAM reason = cls.isJavaApp ? REINSTALL_REASON_JAVA : REINSTALL_REASON_CHROMIUM;
        PostThreadMessageW(hookThreadId_, WM_APP_REINSTALL_HOOKS, reason, 0);
    }

    return cls;
}

void HookEngine::OnFocusChanged(HWND triggerHwnd) {
    // Phase 2b: classify on the calling thread (main — heavy Win32 work),
    // post to the hook thread. ApplyFocusOnHookThread runs the actual
    // state mutations from the drain. Existing callers (WinEventProc,
    // OnTickPoll) keep the same entry point — only the threading model
    // changed.

    // P2c fix (2026-05-19): re-introduce the QuickSync poke that pre-P2b
    // OnFocusChanged used to run inline at the top of its body. SettingsDialog
    // is the project's "live config bus": every toggle bumps configGeneration
    // in SharedState immediately (subprocess-side; TOML save is deferred 30s
    // or until dialog close). The hook picks this up via QuickSync. Pre-P2b,
    // both keystrokes AND focus events triggered QuickSync; removing the
    // focus-time call meant settings toggles only applied on the first
    // keystroke after the user clicked back to the target app — which felt
    // like "settings don't apply until dialog close" if the user clicked
    // back to validate without typing first. Running QuickSync here on the
    // SAME thread as pre-P2b (main / worker, never hook) restores the
    // original UX without compromising the hook-thread single-writer
    // invariant: QuickSync's slow path takes stateMutex_ and calls
    // ApplyConfig — those writes still cross-thread the same way they did
    // pre-Phase-2 (Phase 3 fixes that with RCU snapshots).
    QuickSyncFromSharedState();

    auto cls = std::make_shared<const FocusClassification>(
        ClassifyFocusedWindow(triggerHwnd));
    if (!cls->hwndOpaque) return;  // sentinel: nothing to apply
    mailbox_.Post(HookCommand::kFocusChanged, std::move(cls));
}

/// Replace on-screen text by diffing previousComposition_ vs newText.
///
/// ## U+202F "needEmpty" mechanism (skipEmptyChar_ == false)
///
/// Some Win32 apps swallow BS at certain cursor positions (start of line, empty
/// field, after autocomplete selection in browsers). To guarantee BS always
/// deletes something, we insert U+202F (NARROW NO-BREAK SPACE) as a "bait"
/// character before the BS sequence, then include one extra BS to remove it:
///
///   [insert U+202F] → [BS × (n+1)] → [type new chars]
///
/// U+202F is chosen because:
///   - It is a real Unicode character that apps must insert into the text buffer
///   - It is NOT U+0020 (regular space), so it doesn't trigger word commit
///   - It is narrow/invisible in most fonts, minimizing visual flicker
///
/// This mechanism is ONLY safe for apps that reliably insert U+202F into their
/// text buffer. Apps that ignore or filter it will receive n+1 BS for n chars,
/// deleting one extra character and permanently desyncing previousComposition_.
///
/// Apps with skipEmptyChar_=true (block reinjectVk, skip U+202F bait):
///   - Electron/Console: also get split dispatch — selected by the factory
///     (WindowClassification.isElectron / .isConsole → SplitDispatchInjector
///     with sleepMs=6 / 5 respectively). Sprint 2 D3 lifted the dispatch
///     branching out of HookEngine into the injector layer.
///   - GPU-rendered apps (Zed): batch dispatch via Win32SendInputInjector
///     (single-process, no IPC reorder).
///
/// See OnFocusChanged() for the detection logic + injector publish.
void HookEngine::ReplaceComposition(const std::wstring& newText, DWORD reinjectVk) {
    VKEY_ASSERT_HOOK_THREAD();
    PERF_SCOPE(::NextKey::Perf::Stage::Replace);
    HWND target = GetInputTarget();
    if (!target) {
        previousComposition_ = newText;
        return;
    }

    // Find common prefix at Unicode level — only replace what actually changed
    size_t commonLen = 0;
    size_t minLen = (std::min)(previousComposition_.size(), newText.size());
    while (commonLen < minLen && previousComposition_[commonLen] == newText[commonLen]) {
        commonLen++;
    }

    // ── Non-Unicode code table path ──
    if (currentCodeTable_ != CodeTable::Unicode) {
        // Calculate backspace count from encoded widths of chars being replaced
        size_t backspaceCount = 0;
        for (size_t i = commonLen; i < previousEncodedWidths_.size(); ++i) {
            backspaceCount += previousEncodedWidths_[i];
        }

        // Convert new chars to encoded form
        std::wstring encodedToSend;
        std::vector<uint8_t> newWidths;
        for (size_t i = commonLen; i < newText.size(); ++i) {
            auto enc = CodeTableConverter::ConvertChar(newText[i], currentCodeTable_);
            encodedToSend += enc.units[0];
            if (enc.count == 2) encodedToSend += enc.units[1];
            newWidths.push_back(enc.count);
        }

        HOOK_LOG(L"  ReplaceComposition[encoded]: prev='%s' new='%s' common=%zu BS=%zu encodedLen=%zu reinjectVk=0x%02X",
                 previousComposition_.c_str(), newText.c_str(), commonLen, backspaceCount,
                 encodedToSend.size(), reinjectVk);

        // Sprint 2 D3: route encoded path through the IOutputInjector.
        // The bait-char prefix (Chromium autocomplete-dismiss) is now
        // owned by Win32SendInputInjector and gated on its
        // needsBaitCharPrefix_ flag, so we no longer pre-bake U+202F or
        // an extra BS here. The injector also handles the Electron/
        // Console split-with-Sleep when classified accordingly.
        if (backspaceCount > 0 || !encodedToSend.empty()) {
            sending_ = true;
            auto inj = injector_.load(std::memory_order_acquire);
            bool injOk;
            { PERF_SCOPE(::NextKey::Perf::Stage::Injector);
              injOk = inj->Replace(backspaceCount, std::wstring_view(encodedToSend)); }
            if (!injOk) {
                HOOK_LOG(L"  ReplaceComposition[encoded]: injector reported partial delivery");
            }
            sending_ = false;
            RecordSynthDispatch();
        }

        // Update widths: keep [0..commonLen), append newWidths
        previousEncodedWidths_.resize(commonLen);
        previousEncodedWidths_.insert(previousEncodedWidths_.end(), newWidths.begin(), newWidths.end());
        previousComposition_ = newText;
        return;
    }

    // ── Unicode path (fast path, zero overhead) ──
    size_t backspaceCount = previousComposition_.size() - commonLen;
    std::wstring toSend = newText.substr(commonLen);

    HOOK_LOG(L"  ReplaceComposition: prev='%s' new='%s' common=%zu BS=%zu send='%s' reinjectVk=0x%02X",
             previousComposition_.c_str(), newText.c_str(), commonLen, backspaceCount,
             toSend.c_str(), reinjectVk);

    // ── Async-render apps (Win11 new Notepad) ──
    // WinUI 3 RichEditBox renders on the compositor thread async to input. SendInput
    // BS+replace arrives a frame too late → suppressed key flashes before replacement.
    // EM_REPLACESEL goes straight into the RichEdit child synchronously → atomic.
    //
    // Burst-input race (chaos 3.3 / 5.2 / 6.1, fixed C/2026-05-05): under sub-1ms
    // inter-key, physical WM_KEYDOWN messages stack up in the app's input queue
    // faster than the compositor renders them. When the hook fires for a
    // tone/modifier key, the EM_GETSEL caret read inside TryEditMessagePaste is
    // still at a stale (low) position, so the BS > caret guard refuses the
    // replacement. The original code's "fallback to SendInput" branch was the
    // actual corruption source: BS+chars injected into the kernel queue then
    // interleave with the still-pending physical chars in front of them, and
    // the next hook callback (for the next key) reads a half-applied caret. The
    // observed shapes (tờương / ờnương for `truongwf`) are exactly that race
    // re-rendered.
    //
    // Fix: when TryEditMessagePaste fails, sleep briefly in the hook callback
    // so the app's main thread has time to drain its input queue and advance
    // the caret; then retry. The hook thread holds back its own callback while
    // sleeping, so no further physical keys race in. 30 ms upper bound is well
    // below LowLevelHooksTimeout (default 300 ms per Win32 docs; max ~1000 ms
    // via registry) and dwarfs the typical 5-10 ms catch-up needed at chaos
    // 500 µs inter-key. Only invokes the SendInput
    // fallback if the wait is exhausted — true human-pace typing never hits it.
    if (IsSyncReplaceChannel()) {
        // RichEdit path delegates to RichEditEmReplaceSelInjector.
        // Retry-loop preserved here (not pushed into impl) because the
        // 30 ms catch-up window is policy on the engine side: the budget is
        // bounded by LowLevelHooksTimeout, not by the channel itself.
        constexpr int kAsyncRenderMaxWaitMs = 30;
        constexpr int kAsyncRenderStepMs    = 1;
        int waitedMs = 0;
        auto inj = injector_.load(std::memory_order_acquire);
        for (;;) {
            bool injOk;
            { PERF_SCOPE(::NextKey::Perf::Stage::Injector);
              injOk = inj->Replace(backspaceCount, std::wstring_view(toSend)); }
            if (injOk) {
                previousComposition_ = newText;
                if (synthEventsPending_ > 0) hadSynthInWord_ = true;
                if (waitedMs > 0) {
                    HOOK_LOG(L"  ReplaceComposition[editMsg]: caught up after %dms wait", waitedMs);
                }
                return;
            }
            if (waitedMs >= kAsyncRenderMaxWaitMs) break;
            Sleep(kAsyncRenderStepMs);
            waitedMs += kAsyncRenderStepMs;
        }
        HOOK_LOG(L"  ReplaceComposition[editMsg]: retry exhausted (%dms) — fallback to SendInput BS=%zu send='%s'",
                 kAsyncRenderMaxWaitMs, backspaceCount, toSend.c_str());
    }

    // ── VB6 / ANSI-internal windows ──
    // ANSI windows can't handle KEYEVENTF_UNICODE (VK_PACKET) — Vietnamese chars become '?'.
    // Primary path: EM_REPLACESEL directly into the focused Edit/RichEdit/ThunderRT6
    // child (no clipboard side-effect). Fallback: clipboard paste when the focused
    // child isn't a compatible Edit control.
    //
    // When reinjectVk != 0: HandleAlphaKey appended originalCh to previousComposition_
    // for game-compat tracking, but the physical key was blocked and never reached the
    // ANSI window. Subtract 1 BS to compensate. Reinject VK itself is skipped — ANSI
    // desktop apps (XYplorer, etc.) don't need game-style VK re-injection.
    if (ShouldUseClipboard()) {
        size_t bsCount = backspaceCount;
        if (reinjectVk != 0 && bsCount > 0) bsCount--;

        if (TryEditMessagePaste(toSend, bsCount)) {
            previousComposition_ = newText;
            if (synthEventsPending_ > 0) hadSynthInWord_ = true;
            return;
        }

        HOOK_LOG(L"  ReplaceComposition[clipboard]: fallback BS=%zu (raw=%zu reinject=0x%X) send='%s'",
                 bsCount, backspaceCount, reinjectVk, toSend.c_str());
        if (bsCount > 0) {
            SendBackspaceEvents(bsCount);
            Sleep(8);  // Let app process deletions before clipboard paste
        }
        if (!toSend.empty()) {
            ClipboardPaste(toSend);
        }
        previousComposition_ = newText;
        if (synthEventsPending_ > 0) hadSynthInWord_ = true;
        return;
    }

    {
        // Sprint 2 D3: Unicode path now delegates to IOutputInjector for
        // the BS + chars dispatch — bait-char prefix (Chromium) and
        // split-with-Sleep (Electron/Console) live inside the impl,
        // gated on the classification flags wired in OnFocusChanged.
        //
        // reinjectVk handling stays inline: it's a single VK keydown
        // (no keyup — the physical key-up flows through later) prepended
        // for game compatibility, which the (bsCount, text) interface
        // can't carry. Rare path (only fires when HandleAlphaKey replays
        // a held game-hotkey through a Vietnamese transform), so the
        // extra raw SendInput call here is acceptable.
        HOOK_LOG(L"  ReplaceComposition[send]: BS=%zu toSend='%s' skipEmpty=%d synthPending=%d reinjectVk=0x%02X",
                 backspaceCount, toSend.c_str(),
                 skipEmptyChar_.load(std::memory_order_acquire) ? 1 : 0,
                 synthEventsPending_.load(), reinjectVk);

        if (backspaceCount > 0 || !toSend.empty() || reinjectVk != 0) {
            sending_ = true;

            if (reinjectVk != 0) {
                INPUT evt{};
                evt.type = INPUT_KEYBOARD;
                evt.ki.wVk = static_cast<WORD>(reinjectVk);
                evt.ki.wScan = static_cast<WORD>(MapVirtualKeyW(reinjectVk, MAPVK_VK_TO_VSC));
                evt.ki.dwExtraInfo = VKEY_EXTRA_INFO;
                (void)Output::Internal::TrackedSendInput(&evt, 1);
            }

            if (backspaceCount > 0 || !toSend.empty()) {
                auto inj = injector_.load(std::memory_order_acquire);
                bool injOk;
                { PERF_SCOPE(::NextKey::Perf::Stage::Injector);
                  injOk = inj->Replace(backspaceCount, std::wstring_view(toSend)); }
                if (!injOk) {
                    HOOK_LOG(L"  ReplaceComposition[send]: injector reported partial delivery");
                }
            }

            sending_ = false;
            RecordSynthDispatch();
        }
    }

    previousComposition_ = newText;
    // Mark that at least one synthetic event was sent for this word.
    // Guards passthrough path in HandleAlphaKey from mixing physical+synthetic events mid-word.
    if (synthEventsPending_ > 0) hadSynthInWord_ = true;
}

HookEngine::KeyOutcome HookEngine::TryEscRestoreRaw() {
    auto inj = injector_.load(std::memory_order_acquire);

    // Path 1: live composition (existing behavior).
    if (engine_->Count() > 0) {
        const size_t composedCount = engine_->Count();
        const std::wstring raw = engine_->PeekRaw();
        if (raw.empty()) return KeyOutcome::Fallthrough;
        HOOK_LOG(L"  EscRestoreRaw[live]: bs=%zu raw='%ls'", composedCount, raw.c_str());
        bool injOk;
        { PERF_SCOPE(::NextKey::Perf::Stage::Injector);
          injOk = inj->Replace(composedCount, std::wstring_view(raw)); }
        if (!injOk) {
            HOOK_LOG(L"  EscRestoreRaw[live]: injector reported partial delivery");
            // Don't reset on failure — next user action recovers via normal flow.
            return KeyOutcome::Fallthrough;
        }
        engine_->Reset();
        rawMacroBuffer_.clear();
        tempMacroOff_ = false;
        return KeyOutcome::Eat;
    }

    // Path 2: post-BS (engine empty, raw snapshot in commitStack top).
    // Engine empty here; rawInput preserved in commitStack_ from CommitComposition
    // snapshot. CancelCommitUndo clears stack (single-word scope per design 2026-05-17).
    if (commitUndoState_ != CommitUndoState::Primed || commitStack_.empty()) {
        return KeyOutcome::Fallthrough;
    }
    if (GetTickCount() - commitReadyTime_ > kCommitUndoTimeoutMs) {
        HOOK_LOG(L"  EscRestoreRaw[post-BS]: Primed expired (elapsed > %ums)", kCommitUndoTimeoutMs);
        CancelCommitUndo();
        return KeyOutcome::Fallthrough;
    }
    const auto& top = commitStack_.back();
    if (top.rawInput.empty()) return KeyOutcome::Fallthrough;
    // Primed: trailing commit-trigger already deleted by user's BS. BS count covers
    // the committed body only. Non-Unicode code tables (TCVN3, VNI-Win) encode each
    // wchar_t into multiple bytes — mirror HandleBackspace's width-sum logic.
    size_t bsCount = top.text.size();
    if (currentCodeTable_ != CodeTable::Unicode) {
        bsCount = 0;
        for (auto w : top.widths) bsCount += w;
    }
    HOOK_LOG(L"  EscRestoreRaw[post-BS]: bs=%zu raw='%ls' text='%ls'",
             bsCount, top.rawInput.c_str(), top.text.c_str());
    if (!inj->Replace(bsCount, std::wstring_view(top.rawInput))) {
        HOOK_LOG(L"  EscRestoreRaw[post-BS]: injector reported partial delivery");
        return KeyOutcome::Fallthrough;
    }
    CancelCommitUndo();
    rawMacroBuffer_.clear();
    tempMacroOff_ = false;
    return KeyOutcome::Eat;
}

void HookEngine::SendBackspaces(size_t count) {
    if (count == 0) return;

    // Sprint 2 D2: uniform injector dispatch. The RichEdit class
    // (Win11 New Notepad RichEditD2DPT, Sprint 1 D12 verdict) is now
    // handled inside RichEditEmReplaceSelInjector — no useEditMsgPath_
    // short-circuit needed. Bait-char prefix lives inside
    // Win32SendInputInjector::Replace, gated by needsBaitCharPrefix_
    // wired from the Chromium classification flag (D3).
    auto inj = injector_.load(std::memory_order_acquire);
    HOOK_LOG(L"  SendBackspaces: %zu via injector", count);
    if (!inj->Replace(count, std::wstring_view{})) {
        // Partial-send / channel failure — log but no further fallback;
        // caller's commit-undo state machine handles desync on next key.
        HOOK_LOG(L"  SendBackspaces: injector reported partial delivery");
    }
}

// ═══════════════════════════════════════════════════════════
// Modifier Tracking — feeds double-Alt + layout-change detection
// ═══════════════════════════════════════════════════════════

void HookEngine::TrackModifier(DWORD vkCode, bool isDown) {
    switch (vkCode) {
        case VK_LCONTROL: case VK_RCONTROL:
            if (isDown && !modCtrlDown_) { modCtrlDown_ = true; otherKeyPressed_ = false; }
            else if (!isDown) modCtrlDown_ = false;
            break;
        case VK_LSHIFT: case VK_RSHIFT:
            if (isDown && !modShiftDown_) { modShiftDown_ = true; otherKeyPressed_ = false; }
            else if (!isDown) modShiftDown_ = false;
            break;
        case VK_LMENU: case VK_RMENU:
            if (isDown && !modAltDown_) { modAltDown_ = true; otherKeyPressed_ = false; }
            else if (!isDown) modAltDown_ = false;
            break;
        case VK_LWIN: case VK_RWIN:
            if (isDown && !modWinDown_) { modWinDown_ = true; otherKeyPressed_ = false; }
            else if (!isDown) modWinDown_ = false;
            break;
    }
}

// ═══════════════════════════════════════════════════════════
// Commit Trigger Check
// ═══════════════════════════════════════════════════════════

void HookEngine::InjectKey(DWORD vkCode) {
    // Sprint 2 D1: route through IOutputInjector. The injector knows the
    // active host class and emits the right INPUT[] with VKEY_EXTRA_INFO
    // marker. Engine still owns sending_ guard + lastSynthSendTime_ tracking
    // (they're synth-pressure state, not channel state).
    sending_ = true;
    auto inj = injector_.load(std::memory_order_acquire);
    inj->SendKey(static_cast<unsigned short>(vkCode));
    sending_ = false;
    lastSynthSendTime_ = GetTickCount();
}

bool HookEngine::IsCommitTrigger(DWORD vkCode) {
    // Space, Enter, Escape
    if (vkCode == VK_SPACE || vkCode == VK_RETURN || vkCode == VK_ESCAPE) return true;

    // Tab
    if (vkCode == VK_TAB) return true;

    // Arrow keys
    if (vkCode >= VK_LEFT && vkCode <= VK_DOWN) return true;
    if (vkCode == VK_HOME || vkCode == VK_END ||
        vkCode == VK_PRIOR || vkCode == VK_NEXT) return true;

    // Number keys (0-9)
    if (vkCode >= 0x30 && vkCode <= 0x39) return true;

    // Numpad keys
    if (vkCode >= VK_NUMPAD0 && vkCode <= VK_DIVIDE) return true;

    // OEM keys (punctuation)
    if (vkCode >= VK_OEM_1 && vkCode <= VK_OEM_3) return true;
    if (vkCode >= VK_OEM_4 && vkCode <= VK_OEM_8) return true;
    if (vkCode == VK_OEM_PLUS || vkCode == VK_OEM_COMMA ||
        vkCode == VK_OEM_MINUS || vkCode == VK_OEM_PERIOD) return true;

    // Delete, Insert
    if (vkCode == VK_DELETE || vkCode == VK_INSERT) return true;

    return false;
}

bool HookEngine::IsOemPunctVk(DWORD vkCode) {
    if (vkCode >= VK_OEM_1 && vkCode <= VK_OEM_3) return true;
    if (vkCode >= VK_OEM_4 && vkCode <= VK_OEM_8) return true;
    if (vkCode == VK_OEM_PLUS || vkCode == VK_OEM_COMMA ||
        vkCode == VK_OEM_MINUS || vkCode == VK_OEM_PERIOD) return true;
    return false;
}

bool HookEngine::IsMacroTrigger(DWORD vkCode) const {
    // If not a commit trigger natively, it shouldn't trigger macro either
    if (!IsCommitTrigger(vkCode)) return false;

    // Sprint 1 D6: snapshot the RCU shared_ptr once for the call. The loaded
    // shared_ptr keeps the config object alive even if a writer (ApplyConfig
    // / ReloadFromToml) publishes a new config mid-call — safe internal
    // consistency without stateMutex_ acquisition on the hook hot path.
    auto cfg = config_.load(std::memory_order_acquire);
    if (vkCode == VK_SPACE) return cfg->macroTriggerSpace;
    if (vkCode == VK_RETURN) return cfg->macroTriggerEnter;
    if (vkCode == VK_TAB) return cfg->macroTriggerTab;

    // Direction / Navigation
    if (vkCode >= VK_LEFT && vkCode <= VK_DOWN) return cfg->macroTriggerDir;
    if (vkCode == VK_HOME || vkCode == VK_END ||
        vkCode == VK_PRIOR || vkCode == VK_NEXT) return cfg->macroTriggerDir;

    return true; // Numbers, Punctuation, Esc, etc. default to true if they are commit triggers
}

HookEngine::MacroResult HookEngine::TryExpandMacro(wchar_t triggerChar) {
    Win32CaseMapper mapper;
    // Phase 3c: macro table comes from the RCU snapshot. The shared_ptr
    // local keeps the table alive for the duration of Macro::Plan even
    // if a worker thread republishes mid-call.
    auto snap = configSnapshot_.load(std::memory_order_acquire);
    Macro::PlanInputs inputs{
        .rawMacroBuffer        = rawMacroBuffer_,
        .previousComposition   = previousComposition_,
        .previousEncodedWidths = previousEncodedWidths_,
        .macroTable            = snap->macroTable,
        .macroCrossCommit      = macroCrossCommit_,
        .currentCodeTable      = currentCodeTable_,
        .autoCapsEnabled       = autoCapsMacro_.load(std::memory_order_acquire),
        .triggerChar           = triggerChar,
        .clipboardThreshold    = kMacroClipboardThreshold,
    };
    auto plan = Macro::Plan(inputs, mapper);
    if (!plan.matched) return MacroResult::NoMatch;

    auto inj = injector_.load(std::memory_order_acquire);

    if (plan.useClipboard) {
        if (plan.bsCount > 0) {
            sending_ = true;
            if (!inj->Replace(plan.bsCount, std::wstring_view{})) {
                HOOK_LOG(L"  TryExpandMacro[clipboard]: BS injector reported partial delivery");
            }
            sending_ = false;
            RecordSynthDispatch();
        }
        auto clipText = Macro::ExpandEscapesForClipboard(plan.expansion);
        ClipboardPaste(clipText);
        HOOK_LOG(L"  TryExpandMacro: clipboard paste %zu chars (raw %zu)",
                 clipText.size(), plan.expansion.size());
    } else {
        sending_ = true;
        std::size_t pendingBs = plan.bsCount;
        for (const auto& s : Macro::BuildSegments(plan.expansion, currentCodeTable_)) {
            if (s.isReturn) {
                if (pendingBs > 0) {
                    if (!inj->Replace(pendingBs, std::wstring_view{})) {
                        HOOK_LOG(L"  TryExpandMacro[send]: injector reported partial delivery");
                    }
                    pendingBs = 0;
                }
                inj->SendKey(VK_RETURN);
            } else if (!s.text.empty()) {
                if (!inj->Replace(pendingBs, std::wstring_view(s.text))) {
                    HOOK_LOG(L"  TryExpandMacro[send]: injector reported partial delivery");
                }
                pendingBs = 0;
            }
        }
        if (pendingBs > 0) {
            if (!inj->Replace(pendingBs, std::wstring_view{})) {
                HOOK_LOG(L"  TryExpandMacro[send]: injector reported partial delivery");
            }
        }
        sending_ = false;
        RecordSynthDispatch();
    }

    ClearWordState();
    CancelCommitUndo();
    return plan.isPartOfMacro ? MacroResult::ExpandedEatTrigger
                              : MacroResult::ExpandedPassTrigger;
}

wchar_t HookEngine::VkToMacroChar(DWORD vkCode) noexcept {
    // Translate VK → character with the current modifier state, so Shift/Caps/
    // AltGr yield the actual typed char (e.g. Shift+VK_OEM_PERIOD on US → '>'
    // instead of the unshifted '.'). Uses the foreground window's layout so
    // macros match what the target app would receive.
    //
    // Modifier state: GetKeyboardState is not reliable from a low-level hook
    // thread (LL hooks don't feed our message queue), so we build a minimal
    // key-state snapshot from GetAsyncKeyState for the modifiers ToUnicodeEx
    // actually consults.
    BYTE keyState[256] = {};
    if (GetAsyncKeyState(VK_SHIFT)   & 0x8000) keyState[VK_SHIFT]   = 0x80;
    if (GetAsyncKeyState(VK_CONTROL) & 0x8000) keyState[VK_CONTROL] = 0x80;
    if (GetAsyncKeyState(VK_MENU)    & 0x8000) keyState[VK_MENU]    = 0x80;
    if (GetKeyState(VK_CAPITAL) & 0x0001)      keyState[VK_CAPITAL] = 0x01;

    UINT scan = MapVirtualKeyW(vkCode, MAPVK_VK_TO_VSC);
    HWND fg = GetForegroundWindow();
    HKL layout = GetKeyboardLayout(fg ? GetWindowThreadProcessId(fg, nullptr) : 0);

    // wFlags bit 2 (0x4) = "do not change the keyboard state" — required so
    // ToUnicodeEx doesn't advance pending dead-key state. Win10 1607+.
    wchar_t buf[4] = {};
    int result = ToUnicodeEx(vkCode, scan, keyState, buf, 4, 0x4, layout);
    if (result > 0) {
        return static_cast<wchar_t>(towlower(buf[0]));
    }

    // result <= 0: dead key (-1) or no translation (0). Fall back to the
    // unshifted mapping — matches the pre-ToUnicodeEx behavior for these keys.
    UINT ch = MapVirtualKeyW(vkCode, MAPVK_VK_TO_CHAR);
    return ch ? static_cast<wchar_t>(towlower(static_cast<wchar_t>(ch))) : 0;
}

bool HookEngine::ReinstallKeyboardAndMouseHooks() {
    if (keyboardHook_) {
        UnhookWindowsHookEx(keyboardHook_);
        keyboardHook_ = SetWindowsHookExW(
            WH_KEYBOARD_LL, LowLevelKeyboardProc, cachedHInstance_, 0);
        if (!keyboardHook_) {
            HOOK_LOG(L"  ReinstallKeyboardAndMouseHooks: keyboard hook FAILED err=%lu",
                     GetLastError());
            return false;
        }
    }
    if (mouseHook_) {
        UnhookWindowsHookEx(mouseHook_);
        mouseHook_ = SetWindowsHookExW(
            WH_MOUSE_LL, LowLevelMouseProc, cachedHInstance_, 0);
        if (!mouseHook_) {
            HOOK_LOG(L"  ReinstallKeyboardAndMouseHooks: mouse hook FAILED err=%lu",
                     GetLastError());
            return false;
        }
    }
    HOOK_LOG(L"  ReinstallKeyboardAndMouseHooks: both hooks reinstalled OK");
    return true;
}

// ═══════════════════════════════════════════════════════════
// Phase 2a — Hook-thread command drain
//
// Single-writer invariant: every mutation of the 16 composition-state
// fields must happen on the hook thread. Producers on other threads use
// `mailbox_.Post(bit, ...)`; this drain consumes from the LL hook callback
// (Rule 11.4 step 5 barrier) and from the pump's WM_APP_HOOK_COMMAND
// handler. Dispatch order follows the design doc: kConfigApply first
// (may rebuild engine_), then kFocusChanged (resets composition), then
// kTickPoll, then kToggleVN.
//
// Phase 2a ships the infrastructure ONLY. No producer calls Post yet —
// existing OnFocusChanged / OnTickPoll / ApplyConfig / ToggleVietnameseMode
// still mutate inline as before. Phase 2b/c migrate them onto this channel
// one writer at a time. Until then DrainHookCommands always returns early
// (mailbox bits=0).
// ═══════════════════════════════════════════════════════════

void HookEngine::DrainHookCommands() {
    // Phase 2d: re-entrancy guard. Trips a Debug assertion if a drain
    // handler somehow re-enters DrainHookCommands — that's the
    // "ApplyFoo() called something that called DrainHookCommands again"
    // bug pattern, which would corrupt mailbox bit state silently.
    HookCommandMailbox::DrainScope scope(mailbox_);

    const std::uint32_t bits = mailbox_.DrainBits();
    // Phase 3f: even if no fresh bits, a previous drain may have deferred
    // the config apply (engine was busy). Re-check on every drain so the
    // apply lands as soon as the engine empties.
    const bool hadDeferredApply = deferredConfigApply_.load(std::memory_order_acquire);
    if (!bits && !hadDeferredApply) return;

    // kConfigApply: latch the request; the actual apply runs at the tail
    // of this function once we know whether the engine is busy. We DO
    // NOT call ApplyConfigOnHookThread mid-drain anymore — see P3f note.
    if (bits & HookCommand::kConfigApply) {
        deferredConfigApply_.store(true, std::memory_order_release);
    }
    if (bits & HookCommand::kFocusChanged) ApplyFocusOnHookThread(mailbox_.ConsumePendingFocus());
    if (bits & HookCommand::kTickPoll)     ApplyTickPollOnHookThread();
    if (bits & HookCommand::kToggleVN)     ApplyToggleVNOnHookThread();

    // Phase 3f — guard the config apply against mid-word reset.
    // ApplyConfigOnHookThread destroys engine_ and creates a fresh one;
    // if the user has uncommitted input (engine_->Count() > 0), running
    // the apply now would either (a) commit a partial word visibly or
    // (b) drop the partial input. Both are user-visible quirks. Defer
    // until the engine empties naturally — typically the next keystroke
    // after a word commit, occasionally a backspace-to-empty or focus
    // change. Focus change (ApplyFocusOnHookThread above) calls
    // ResetComposition which zeros the count, so deferred applies often
    // land on the same drain when triggered by a focus event.
    if (deferredConfigApply_.load(std::memory_order_acquire)
        && engine_ && engine_->Count() == 0) {
        deferredConfigApply_.store(false, std::memory_order_release);
        ApplyConfigOnHookThread();
    }
}

// Phase 2b — focus apply runs on the hook thread (called from
// DrainHookCommands). All composition-state writes that used to live in
// OnFocusChanged moved here. The cls parameter is the pre-computed
// classification snapshot produced by ClassifyFocusedWindow on main.
//
// Must stay fast (Rule 11.2) — no syscalls beyond the cheap ones already
// listed in the design's "may only mutate composition state + atomic
// stores" contract. Heavy work (ClassifyWindow, GetExeNameForHwnd,
// IsWebView2App, CreateToolhelp32Snapshot) is in ClassifyFocusedWindow,
// not here.
void HookEngine::ApplyFocusOnHookThread(std::shared_ptr<const FocusClassification> cls) {
    VKEY_ASSERT_HOOK_THREAD();
    if (!cls || !cls->hwndOpaque) return;

    HWND activeHwnd = reinterpret_cast<HWND>(cls->hwndOpaque);

    // Reset composition + per-word state. These were the Rule 11.3-violating
    // writes from main pre-Phase-2b.
    ResetComposition();
    tempEngineOff_ = false;
    autoCapState_ = AutoCapState::Idle;

    // Per-app cached flags — single release-store pair with the hot-path
    // acquire-loads in ProcessKeyDown / HandleAlphaKey.
    skipEmptyChar_.store(cls->localSkipEmpty, std::memory_order_release);
    useClipboardPaste_.store(cls->localClipboard, std::memory_order_release);

    // IOutputInjector swap — RCU publish so in-flight HandleAlphaKey reads
    // see either the old or new injector cleanly.
    {
        NextKey::Output::WindowClassification c{};
        c.isRichEditD2DPT = cls->localEditMsg;
        c.isElectron      = cls->localElectronApp;
        c.isConsole       = cls->isConsole;
        c.isChromium      = cls->localNeedBait;
        c.useClipboard    = cls->localUseClipboardInjector;
        injector_.store(NextKey::Output::Create(c), std::memory_order_release);
    }

    HOOK_LOG(L"  AppDetect: console=%d skipEmpty=%d electron=%d webview2=%d bait=%d clipboard=%d editMsg=%d useClipInj=%d",
             cls->isConsole ? 1 : 0, cls->localSkipEmpty ? 1 : 0, cls->localElectronApp ? 1 : 0,
             cls->isWebView2 ? 1 : 0, cls->localNeedBait ? 1 : 0, cls->localClipboard ? 1 : 0,
             cls->localEditMsg ? 1 : 0, cls->localUseClipboardInjector ? 1 : 0);

    // RefreshFocusCache uses GetFocusedChildHwnd (AttachThreadInput) which
    // is cheap (~µs). Safe on hook thread.
    if (cls->localClipboard || cls->localEditMsg) {
        RefreshFocusCache(activeHwnd);
    } else {
        cachedFocusedHwnd_.store(nullptr, std::memory_order_relaxed);
        cachedFocusedClass_.clear();
    }

    // CJK layout check — GetKeyboardLayout is kernel-cached, single µs.
    CheckLayoutChange();

    // Helper HWNDs: classification flags applied above (so dispatch stays
    // consistent), but skip currentExe_ / smart-switch tracking — that's
    // what the 200 ms focus poll catches.
    if (cls->skipAppTracking) return;

    // Short-circuit when no per-app feature needs tracking. Phase 3c
    // reads the override-map presence from the RCU snapshot — same data
    // the cls fields were resolved against in Classify.
    {
        auto snap = configSnapshot_.load(std::memory_order_acquire);
        const bool noOverrides = !snap
            || (snap->appEncodingOverrides.empty()
                && snap->appInputMethodOverrides.empty());
        if (!smartSwitch_ && !excludeApps_ && !tsfApps_ && noOverrides) return;
    }

    if (cls->exeName.empty()) return;

    const bool wasExcluded = isExcludedApp_.load(std::memory_order_acquire);
    const bool wasTsfApp   = isTsfApp_.load(std::memory_order_acquire);

    // Smart switch SAVE for the previous app — uses OLD currentExe_, so
    // must run before we reassign it.
    if (smartSwitch_ && !currentExe_.empty() && !wasExcluded && !wasTsfApp) {
        if (appModeMap_.size() >= kMaxSmartSwitchEntries) {
            appModeMap_.clear();
        }
        const bool savedMode = vietnameseMode_.load(std::memory_order_acquire);
        appModeMap_[currentExe_] = savedMode;
        smartSwitchMgr_.SetAppMode(currentExe_, savedMode);
        appModeDirty_ = true;
    }

    if (!currentExe_.empty()) {
        previousExe_ = currentExe_;
    }
    currentExe_ = cls->exeName;

    // Sync PID tracker so the 200 ms focus poll won't re-trigger for this app.
    if (cls->pid) lastForegroundPid_.store(cls->pid, std::memory_order_release);

    isExcludedApp_.store(cls->isExcluded, std::memory_order_release);
    isTsfApp_.store(cls->isTsf, std::memory_order_release);

    HOOK_LOG(L"  Engine: %s for '%s' (tsf_feature=%d, in_tsf_list=%d, excluded=%d)",
             cls->isTsf ? L"TSF (hook passthrough)" : L"HOOK",
             currentExe_.c_str(),
             tsfApps_ ? 1 : 0,
             cls->isTsf ? 1 : 0,
             cls->isExcluded ? 1 : 0);

    // SharedState TSF flag bridge — idempotent via SetOrClearFlag in main.
    if (tsfModeCallback_) {
        const bool tsfReadonly = !cls->isTsf && !cls->isExcluded;
        if (cls->isTsf != wasTsfApp) {
            HOOK_LOG(L"  TSF_ACTIVE flag: %s → %s",
                     wasTsfApp ? L"true" : L"false", cls->isTsf ? L"true" : L"false");
        }
        tsfModeCallback_(cls->isTsf, tsfReadonly);
    }

    if (cls->isExcluded) {
        excludedPid_.store(cls->pid, std::memory_order_release);
        HOOK_LOG(L"  ExcludeApps: '%s' is excluded, passthrough (pid=%u)",
                 currentExe_.c_str(), cls->pid);
        if (!wasExcluded) NotifyModeChange();
        return;
    }
    if (cls->isTsf) {
        HOOK_LOG(L"  TsfApps: '%s' uses TSF engine, hook passthrough", currentExe_.c_str());
        return;
    }

    // Per-app encoding override (target value pre-resolved in Classify
    // against the on-main global to avoid a cross-thread read of
    // globalCodeTable_ here).
    {
        const CodeTable targetTable = static_cast<CodeTable>(cls->targetCodeTable);
        if (targetTable != currentCodeTable_) {
            currentCodeTable_ = targetTable;
            HOOK_LOG(L"  AppOverride: encoding=%d for '%s'",
                     static_cast<int>(currentCodeTable_), currentExe_.c_str());
        }
    }

    // Per-app input method override (same pattern as encoding). Recreate
    // engine_ on change — the only heap allocation on this path.
    {
        const InputMethod targetMethod = static_cast<InputMethod>(cls->targetMethod);
        if (targetMethod != currentMethod_.load(std::memory_order_acquire)) {
            currentMethod_.store(targetMethod, std::memory_order_release);
            TypingConfig engineConfig = *config_.load(std::memory_order_acquire);
            engineConfig.inputMethod = targetMethod;
            engine_ = EngineFactory::Create(engineConfig);
            HOOK_LOG(L"  AppOverride: inputMethod=%d for '%s'",
                     static_cast<int>(targetMethod), currentExe_.c_str());
        }
    }

    // Smart switch restore for the new app.
    if (smartSwitch_) {
        auto it = appModeMap_.find(currentExe_);
        if (it != appModeMap_.end()) {
            const bool curMode = vietnameseMode_.load(std::memory_order_acquire);
            if (it->second != curMode) {
                vietnameseMode_.store(it->second, std::memory_order_release);
                HOOK_LOG(L"  SmartSwitch: restored %s for '%s'",
                         it->second ? L"Vietnamese" : L"English", currentExe_.c_str());
                NotifyModeChange();
            }
        } else {
            HOOK_LOG(L"  SmartSwitch: inherit %s for unknown '%s'",
                     vietnameseMode_.load(std::memory_order_acquire)
                         ? L"Vietnamese" : L"English",
                     currentExe_.c_str());
        }
    }

    // Leaving an excluded app — effective mode flipped E→actual even if
    // vietnameseMode_ didn't move. Replay layout check so a suppressed
    // CJK transition that fired during the excluded session restores now.
    if (wasExcluded) {
        const bool wasSuppressed = layoutSuppressed_;
        OnLayoutChanged(cachedIsCompatLayout_);
        if (wasSuppressed == layoutSuppressed_) NotifyModeChange();
    }
}

// Phase 2c — ToggleVietnameseMode body, migrated to the hook thread.
//
// Single-writer note: this runs from the drain only. The mailbox
// coalesces multiple Posts before drain into one drained bit
// (HookCommandMailboxTest §PostSameBitMultipleTimesDrainReturnsOnce),
// so rapid hotkey mashing collapses to one toggle per drain cycle.
// User-visible behaviour: same as before — sub-keystroke responsive.
void HookEngine::ApplyToggleVNOnHookThread() {
    VKEY_ASSERT_HOOK_THREAD();
    // Excluded-app gate. PID check vs cached excludedPid_ distinguishes
    // "genuinely in excluded app" (block toggle) from "stale flag, user
    // already left" (force VN). Both atomic stores below are safe on
    // hook thread now that we're single-writer.
    if (excludeApps_ && isExcludedApp_.load(std::memory_order_acquire)) {
        HWND fg = GetForegroundWindow();
        DWORD fgPid = 0;
        if (fg) GetWindowThreadProcessId(fg, &fgPid);
        const DWORD cachedPid = excludedPid_.load(std::memory_order_acquire);
        if (fgPid == cachedPid && cachedPid != 0) {
            HOOK_LOG(L"  ToggleVN: BLOCKED (excluded pid=%u)", cachedPid);
            return;
        }
        // Different PID — user already left excluded app, flag is stale.
        // Force VN: user pressed toggle expecting V mode, having perceived
        // the excluded app as English.
        isExcludedApp_.store(false, std::memory_order_release);
        vietnameseMode_.store(true, std::memory_order_release);
        HOOK_LOG(L"  ToggleVN: stale excluded → forced Vietnamese (fg pid=%u)", fgPid);
        NotifyModeChange();
        if (beepOnSwitch_) MessageBeep(MB_OK);
        return;
    }

    // Commit pending composition (skip if CJK-suppressed — engine inactive).
    if (!layoutSuppressed_ && engine_->Count() > 0) {
        CommitComposition();
    }
    CancelCommitUndo();
    digitLedWord_ = false;

    const bool newMode = !vietnameseMode_.load(std::memory_order_acquire);
    vietnameseMode_.store(newMode, std::memory_order_release);
    NEXTKEY_LOG(L"HookEngine: mode = %s (via drain)", newMode ? L"Vietnamese" : L"English");

    // Smart-switch save. Drop the pre-P2c GetForegroundWindow + GetExeNameForHwnd
    // fallback — those are Rule 11.2 forbidden on the hook thread (Toolhelp32
    // snapshot). If currentExe_ is empty here (startup before any focus event),
    // the next focus event will set it and the toggle takes effect for that app
    // on its first save.
    if (smartSwitch_ && !currentExe_.empty()) {
        appModeMap_[currentExe_] = newMode;
        smartSwitchMgr_.SetAppMode(currentExe_, newMode);
    }

    if (beepOnSwitch_) {
        MessageBeep(newMode ? MB_OK : MB_ICONASTERISK);
    }
    NotifyModeChange();
}

// P3e/P3f — config-apply drain handler. Wired into the kConfigApply mailbox
// bit posted by ReloadFromToml on the worker. Hook-thread side of the
// single-writer contract: this is where composition state mutations
// (currentMethod_ store, engine_ swap) actually run.
//
// CONTRACT (P3f): DrainHookCommands guarantees `engine_->Count() == 0`
// before calling this function. Any pending word is left intact in the
// engine until natural completion (commit, backspace-empty, focus
// reset); the drain latches kConfigApply via `deferredConfigApply_` and
// re-checks on every cycle. This avoids the chaos-stress visible quirk
// where a config reload landing mid-word committed a partial word (e.g.
// `uongs` → `uôngs` instead of `uống` because the engine was reset
// between `uong` and `s`).
//
// Pre-P3e (P2c→P3d): handler existed dormant; the worker's ReloadFromToml
// performed CommitComposition + `engine_ = Create()` inline. Under
// `-InjectConfigReloadMs 50` chaos, that race produced 11/55 failures —
// a UAF: worker swapped `engine_` while hook hot path held a raw
// pointer read. P3e moved the mutation here; P3f added the engine-busy
// gate at the drain.
//
// Why ALWAYS recreate (not just on method change): the engine internally
// stores a TypingConfig copy. modernOrtho / allowZwjf / spellCheckEnabled
// changes need a fresh engine for the new behavior to take effect. The
// drain's busy-gate means we don't recreate per chaos tick — we recreate
// once per word boundary, when applicable, regardless of how many bumps
// stacked up.
//
// Resolves the P0 single-writer-violation TODO item from the 2026-05-19
// review: `engine_` had two writer paths; this collapses to one (hook).
void HookEngine::ApplyConfigOnHookThread() {
    VKEY_ASSERT_HOOK_THREAD();
    auto cfg = config_.load(std::memory_order_acquire);
    if (!cfg) return;

    // Resolve target inputMethod considering per-app override (Phase 3c
    // snapshot reader). currentExe_ is hook-owned (set in
    // ApplyFocusOnHookThread); reading it here is single-threaded safe.
    auto snap = configSnapshot_.load(std::memory_order_acquire);
    InputMethod targetMethod = cfg->inputMethod;
    if (snap && !currentExe_.empty()) {
        auto it = snap->appInputMethodOverrides.find(currentExe_);
        if (it != snap->appInputMethodOverrides.end()) targetMethod = it->second;
    }

    // P3f: caller (DrainHookCommands) gates on engine_->Count() == 0, so
    // CommitComposition would be a no-op. Skip it to keep this handler
    // purely focused on the engine swap.
    currentMethod_.store(targetMethod, std::memory_order_release);
    TypingConfig engineConfig = *cfg;
    engineConfig.inputMethod = targetMethod;
    engine_ = EngineFactory::Create(engineConfig);

    HOOK_LOG(L"  ApplyConfig: engine recreated (method=%d, modernOrtho=%d, allowZwjf=%d)",
             static_cast<int>(targetMethod),
             cfg->modernOrtho ? 1 : 0,
             cfg->allowZwjf ? 1 : 0);
}

void HookEngine::ApplyTickPollOnHookThread() {
    VKEY_ASSERT_HOOK_THREAD();
    // CheckLayoutChange queries GetKeyboardLayout (kernel-cached, fast)
    // and may call OnLayoutChanged → layoutSuppressed_ writes + engine
    // commit. All hook-thread-safe.
    CheckLayoutChange();
}

}  // namespace NextKey
