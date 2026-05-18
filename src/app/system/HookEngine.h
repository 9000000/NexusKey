// VKey - Keyboard Hook Engine
// SPDX-License-Identifier: GPL-3.0-only
//
// Single-process Vietnamese input using WH_KEYBOARD_LL.
// Replaces TSF DLL for MVP — no COM registration, no admin elevation.

#pragma once

#include "core/engine/IInputEngine.h"
#include "core/engine/CodeTableConverter.h"
#include "core/config/TypingConfig.h"
#include "core/hotkey/HotkeyRegistry.h"
#include "core/AutoCapStateTransition.h"
#include "core/SmartSwitchManager.h"
#include <Windows.h>
#include <functional>
#include <atomic>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

// Forward declaration so HookEngine.h stays Linux-friendly (output/ folder
// is Win32-only). Sprint 2 T3 — IOutputInjector is the output channel
// strategy interface (see src/app/output/IOutputInjector.h, doc:
// docs/plans/sprint-2-output-injector.md §2.1).
namespace NextKey::Output { class IOutputInjector; }

namespace NextKey {

class SharedStateManager;  // Forward declaration (defined in core/ipc/SharedStateManager.h)

/// Callback when Vietnamese/English mode changes
using ModeChangeCallback = std::function<void(bool vietnamese)>;

/// Keyboard hook engine — intercepts keystrokes, processes Vietnamese input,
/// outputs via SendInput backspace+retype. Absorbs HotkeyManager logic.
class HookEngine {
public:
    HookEngine();
    ~HookEngine();

    HookEngine(const HookEngine&) = delete;
    HookEngine& operator=(const HookEngine&) = delete;

    /// Start the hook engine (installs keyboard hook + focus hook)
    bool Start(HINSTANCE hInstance, const TypingConfig& config,
               bool initialVietnamese = true, uint8_t startupMode = 0);

    /// Stop and unhook everything
    void Stop();

    /// Toggle Vietnamese/English mode
    void ToggleVietnameseMode();

    /// Commit any pending composition — called by hotkey callbacks before firing actions
    /// (e.g., Quick Convert) so the text in the document reflects what's on screen.
    void CommitPending();

    /// Set callback for mode changes (to update tray icon)
    void SetModeChangeCallback(ModeChangeCallback callback) { modeChangeCallback_ = std::move(callback); }

    /// Set callback for config reload (notifies main to update QuickConvert etc.)
    void SetConfigReloadCallback(std::function<void()> callback) { configReloadCallback_ = std::move(callback); }

    /// Callback fired on focus changes. Args: (tsfActive, tsfReadonly).
    ///   tsfActive   = foreground app is in TSF list (full TIP consumes keys).
    ///   tsfReadonly = Hook handles keys; TSF DLL should publish doc anchor.
    /// Fires on every focus change (SetOrClearFlag is idempotent).
    void SetTsfModeCallback(std::function<void(bool, bool)> callback) {
        tsfModeCallback_ = std::move(callback);
    }

    /// Re-read SharedState and reload TOML if configGeneration changed.
    /// Safe cross-process: uses the configGeneration counter, not the Named Event
    /// (which is auto-reset and reserved for the TSF DLL).
    void SyncConfigFromSharedState();

    /// Periodic poll: CJK layout change detection + foreground PID
    /// fallback (catches missed/phantom focus events). Sprint 1 D10
    /// migrated this off `SetTimer(200ms, FocusPollTimerProc)` and onto
    /// `MainThreadWorker`'s tick branch. The body is unchanged from the
    /// retired `FocusPollTimerProc`; the call site is the only difference.
    /// Safe to call from any thread that is not the LL hook thread —
    /// stateMutex_ serializes against main-thread writers.
    void OnTickPoll() noexcept;

    /// Set SharedState pointer for direct reading (must be the global instance from main.cpp)
    void SetSharedStateReader(SharedStateManager* ptr) { sharedStatePtr_ = ptr; }

    /// Change code table (commits pending composition, updates per-app map)
    void SetCodeTable(CodeTable ct);

    /// Get effective code table (checks manual per-app override map)
    [[nodiscard]] CodeTable GetCodeTable() const noexcept;

    [[nodiscard]] bool IsVietnameseMode() const noexcept {
        return vietnameseMode_.load(std::memory_order_acquire);
    }
    [[nodiscard]] bool IsRunning() const noexcept { return keyboardHook_ != nullptr; }

    // Magic number to mark our own SendInput events (prevents other hooks from processing them)
    static constexpr ULONG_PTR VKEY_EXTRA_INFO = 0x4E4B;  // "NK"

    // Get exe name (lowercase) from window handle — used by ClassifyWindow() and smart switch
    [[nodiscard]] static std::wstring GetExeNameForHwnd(HWND hwnd) noexcept;

private:
    // Hook callbacks (static → instance dispatch)
    static LRESULT CALLBACK LowLevelKeyboardProc(int nCode, WPARAM wParam, LPARAM lParam);
    static void CALLBACK WinEventProc(HWINEVENTHOOK hHook, DWORD event, HWND hwnd,
                                       LONG idObject, LONG idChild,
                                       DWORD dwEventThread, DWORD dwmsEventTime);
    static LRESULT CALLBACK LowLevelMouseProc(int nCode, WPARAM wParam, LPARAM lParam);

    // Config application (shared between Start and SyncConfigFromSharedState)
    void ApplyConfig(const TypingConfig& config);

    /// Publish a new hotkey registry to the hook hot path. Atomic RCU swap —
    /// caller may call from main thread; the hook callback picks up the new
    /// snapshot on its next ProcessKeyDown. The previous registry stays alive
    /// until any in-flight key event finishes its load().
    void ApplyHotkeyRegistry(HotkeyRegistry registry);

    // Core processing
    bool ProcessKeyDown(DWORD vkCode, DWORD scanCode, DWORD flags);
    bool ProcessKeyUp(DWORD vkCode, DWORD flags);

    // Outcome of ProcessKeyDown step extracts (H1a/H1b/H1c).
    //   Eat         → ProcessKeyDown returns true (key consumed by step).
    //   Pass        → ProcessKeyDown returns false (key passes through to app).
    //   Fallthrough → continue with subsequent ProcessKeyDown steps.
    enum class KeyOutcome : uint8_t { Eat, Pass, Fallthrough };

    // H1b (extracted from ProcessKeyDown steps 0/0b/1/1b/1c): top-of-pipeline
    // guards — QuickSync from SharedState, TSF early-out, modifier tracking,
    // toggle-key passthrough (Caps/Num/Scroll), excluded-app passthrough with
    // PID verification. Falls through only when the keystroke should reach
    // the Vietnamese pipeline. Bookkeeping (otherKeyPressed_/modTapCount_/
    // synth watchdog) stays in ProcessKeyDown after the switch — only runs
    // on Fallthrough by design (excluded-app's same-PID/still-excluded paths
    // set otherKeyPressed_ themselves before returning Pass).
    [[nodiscard]] KeyOutcome RunTopGuards(DWORD vkCode);

    // H1a (extracted from ProcessKeyDown step 2d): commit-undo state machine
    // (Idle/Ready/Primed) — handles backspace-into-committed-word replay.
    // Mutates commitUndoState_/pendingTriggerCount_/macroCrossCommit_/rawMacroBuffer_
    // and may call HandleAlphaKey/HandleVniDigitKey/HandleBackspace/InjectKey.
    [[nodiscard]] KeyOutcome HandleCommitUndo(DWORD vkCode, bool vnMode);

    // H1c (extracted from ProcessKeyDown steps 3 / 3a-3d): English-mode short
    // circuit + Vietnamese-mode pre-dispatch tracking. When !vnMode, runs the
    // English-mode macro tracking block and returns Pass. When vnMode, updates
    // auto-caps FSM, accumulates the macro buffer, handles temp-off-by-Esc, and
    // attempts macro expansion on commit triggers — returning Eat/Pass on
    // expansion match, Fallthrough otherwise so the dispatch chain runs next.
    /// Esc-restore-raw: end composition with raw keys and inject them (víu → virus).
    /// Returns KeyOutcome::Eat on success, Fallthrough if buffer empty / disabled.
    [[nodiscard]] KeyOutcome TryEscRestoreRaw();

    [[nodiscard]] KeyOutcome HandlePreDispatch(DWORD vkCode, bool vnMode, bool macroOn,
                                                bool macroEng, bool tempOffMacroEsc,
                                                bool escRestoreRaw,
                                                bool cachedShift, bool cachedCapsLock,
                                                bool cachedCtrl, bool cachedAlt,
                                                bool cachedWin);

    // H1c (extracted from ProcessKeyDown steps 4b-10): action dispatch chain.
    // Late guards (tempEngineOff_ bypass, Ctrl/Alt/Win shortcut skip), then
    // alpha / Telex bracket / VNI digit / backspace / commit-trigger / "other
    // key with pending composition" / synth-pending BS re-inject. Returns Eat
    // or Pass for every keystroke (final fallthrough returns Pass — original
    // ProcessKeyDown's tail `return false`).
    [[nodiscard]] KeyOutcome DispatchKeyAction(DWORD vkCode, bool cachedShift,
                                                bool cachedCapsLock, bool cachedCtrl,
                                                bool cachedAlt, bool cachedWin, bool macroOn);

    // Input engine interaction
    [[nodiscard]] bool HandleAlphaKey(DWORD vkCode, bool shift, bool capsLock);
    [[nodiscard]] bool HandleVniDigitKey(DWORD vkCode); // VNI/UserDefined digit 0-9: push to engine, replace composition
    void HandleBackspace();
    bool CommitComposition();  // Returns true if auto-restore changed text
    void ResetComposition();
    void CancelCommitUndo();   // commitUndoState_ = Idle + commitStack_.clear()
    void RecordSynthDispatch() noexcept;  // Update both lastSynthSendTime_ and lastRealSynthTime_
    void SetCommitUndoReady(); // commitUndoState_ = Ready + timestamp

    // Output — universal SendInput with KEYEVENTF_UNICODE
    void ReplaceComposition(const std::wstring& newText, DWORD reinjectVk = 0);
    // Sprint 2 D3 removed DispatchSendInput callers; D4 deleted body+decl.
    // Split-vs-batch lives inside the IOutputInjector impls now. Synth dispatch
    // goes through Output::Internal::TrackedSendInput, which routes the
    // synth-counter via g_synthCounterCallback → OnSynthDispatched.
    void SendBackspaces(size_t count);
    void SendBackspaceEvents(size_t count);
    void SendCharEvents(const std::wstring& text);

    // Sprint 2 D5: Routes Internal::g_synthCounterCallback into the
    // singleton's synthEventsPending_ atomic. Static so it can be
    // wired as a plain function pointer (no captures); friends-of-the-
    // class access via s_instance is sufficient. Wired in Start, no-op
    // when s_instance is null (defensive — Start is the only writer).
    static void OnSynthDispatched(int delta) noexcept;

    // Sprint 2 D4: Returns true when the current injector publishes a
    // synchronous channel (RichEditEmReplaceSelInjector — SettleBudget=0ms).
    // Replaces the legacy useEditMsgPath_ atomic-bool flag for the four
    // policy-gate sites in HandleAlphaKey / commit-undo / commit-trigger /
    // ReplaceComposition retry-loop. Cheap: 1 atomic_load(injector_) + 1
    // virtual call + 1 compare. Coupling caveat: relies on the contract that
    // only RichEdit returns 0ms; if a future Win32-sync impl also returns 0ms
    // it would misfire. D5 SettleBudget integration may revisit.
    [[nodiscard]] bool IsSyncReplaceChannel() const noexcept;

    // Clipboard paste fallback for VB6/ANSI-internal apps (can't handle KEYEVENTF_UNICODE)
    [[nodiscard]] bool ShouldUseClipboard() const noexcept;
    void ClipboardPaste(const std::wstring& text);

    // Direct EM_REPLACESEL into focused Edit/RichEdit/VB6 TextBox — no clipboard touched.
    // Primary VB6/ANSI-window path. Returns false if the focused control isn't a
    // compatible Edit class; caller falls back to ClipboardPaste.
    [[nodiscard]] bool TryEditMessagePaste(const std::wstring& text, size_t backspaceCount) noexcept;

    // Modifier state tracking (used by double-Alt and layout change detection)
    void TrackModifier(DWORD vkCode, bool isDown);

    // Backspace-into-committed-word: replay saved chars to restore engine state
    void ReplayCommittedChars();

    // Commit trigger check
    static bool IsCommitTrigger(DWORD vkCode);
    bool IsMacroTrigger(DWORD vkCode) const;

    // OEM punctuation subset of IsCommitTrigger (DispatchKeyAction step 6d).
    static bool IsOemPunctVk(DWORD vkCode);

    // Convert VK code to macro-usable char (for special-char macro keys).
    // Uses MapVirtualKeyW — returns unshifted character only (Shift state ignored).
    [[nodiscard]] static wchar_t VkToMacroChar(DWORD vkCode) noexcept;

    // Result of TryExpandMacro
    enum class MacroResult { NoMatch, ExpandedEatTrigger, ExpandedPassTrigger };

    // Shared macro expansion logic (used by both English and Vietnamese mode paths).
    [[nodiscard]] MacroResult TryExpandMacro(wchar_t triggerChar);

    // Re-inject a key after auto-restore replacement
    void InjectKey(DWORD vkCode);

    // Clear per-word engine state (shared by CommitComposition, ResetComposition, TryExpandMacro)
    void ClearWordState();

    [[nodiscard]] static bool IsTrayOrTaskbarWindow(HWND hwnd) noexcept;
    // WebView2 host detection. Cache is positive-only (see .cpp for rationale).
    // REQUIRES: stateMutex_ held by caller.
    [[nodiscard]] bool IsWebView2App(HWND topLevel, const std::wstring& exeFullPath) noexcept;
    void NotifyModeChange() noexcept;  // Fire modeChangeCallback_ with effective mode
    bool VerifyExcludedState();        // Check if foreground is still excluded; clears stale flag if not
    void OnFocusChanged(HWND triggerHwnd = nullptr);
    // Populate cachedFocusedHwnd_/cachedFocusedClass_ from `foreground` via AttachThreadInput.
    // Called from OnFocusChanged and on-demand from TryEditMessagePaste when cache is stale.
    void RefreshFocusCache(HWND foreground) noexcept;
    void OnLayoutChanged(bool isCompatibleNow);
    void CheckLayoutChange();  // Query current layout and call OnLayoutChanged if it changed
    void ReloadAppOverrides();
    void ReloadExcludedApps();   // Reload excluded app set from TOML
    void ReloadTsfApps();        // Reload TSF app set from TOML
    // Reload macros from TOML with keys lowercased for case-insensitive lookup.
    // Runtime matching already lowercases the typed buffer; normalizing the
    // map keys mirrors that so capitalized TOML keys (e.g. `Chol = "Chôl"`) match.
    void ReloadMacroTable();
    void SaveEnglishModeAppsIfDirty();  // Persist English-mode apps to TOML

    // Engine state
    std::unique_ptr<IInputEngine> engine_;
    // Sprint 1 D6: migrated to std::atomic<std::shared_ptr<const TypingConfig>>
    // (Rule #11.3 RCU pattern). Writers (main thread): Start, QuickSyncFromSharedState,
    // ReloadFromToml — create a fresh shared_ptr with the new config and store with
    // release ordering. Readers (hook hot path): IsMacroTrigger loads once per
    // call and dereferences the loaded shared_ptr. The old config object stays
    // alive while readers hold their loaded shared_ptr, so no use-after-free is
    // possible even when a writer publishes mid-keystroke. Initialized with a
    // default TypingConfig so the field is never nullptr — Start overwrites it
    // before the hook thread is spawned, but the default-init guards against
    // any pre-Start IsMacroTrigger access path.
    std::atomic<std::shared_ptr<const TypingConfig>> config_{
        std::make_shared<const TypingConfig>()
    };  // Last applied config (for per-app engine recreation)

    // Unified hotkey registry (cancel-composition / skip-macro / toggle-enabled).
    // RCU pattern matching config_ above: writers (main thread) call
    // hotkeys_.store(std::make_shared<...>(newRegistry), release); readers
    // (hook hot path) load() once per ProcessKeyDown to dispatch all three
    // intents against the same snapshot. Default = factory bindings
    // (Esc/Esc/Ctrl-alone/2×Alt) so the field is never nullptr and hot path
    // can dereference unconditionally even before ApplyConfig has run.
    std::atomic<std::shared_ptr<const HotkeyRegistry>> hotkeys_{
        std::make_shared<const HotkeyRegistry>(HotkeyRegistry::Defaults())
    };
    // Sprint 2 T3: Output channel strategy. RCU-published shared_ptr to the
    // active IOutputInjector, same pattern as config_ above. Writers (main
    // thread on focus change): two-phase classify → atomic_store. Readers
    // (hook hot path): atomic load → 1 virtual call (~11 ns total overhead).
    // Initialized in HookEngine ctor via Output::Create({}) so the field is
    // never nullptr — hot path's atomic_load can rely on a usable injector
    // even before any focus event has fired.
    // See docs/plans/sprint-2-output-injector.md §1 for the data-flow contract.
    std::atomic<std::shared_ptr<NextKey::Output::IOutputInjector>> injector_;
    // Sprint 1 D5.1: migrated to std::atomic for hook-thread-safe read without
    // stateMutex_ (Rule #11.3 acquire/release). Writers: ApplyConfig (main),
    // QuickSyncFromSharedState (hook — same thread as readers), ReloadFromToml
    // (main), OnFocusChanged app-method override (main, via WinEventProc).
    // Readers: ProcessKeyDown punct branch + HandleAlphaKey VNI/Combined gates.
    std::atomic<InputMethod> currentMethod_{InputMethod::Telex};
    std::wstring previousComposition_;  // What's currently displayed in the app
    std::vector<uint8_t> previousEncodedWidths_;  // Output unit count per Unicode char (for non-Unicode code tables)
    // Sprint 1 D5: migrated to std::atomic for hook-thread-safe read without
    // stateMutex_ (Rule #11.3 acquire/release pattern). Hook callback paths
    // (ProcessKeyDown/Up, CheckLayoutChange) use .load(acquire); main thread
    // (ApplyConfig, ToggleVietnameseMode, SettingsDialog WM_VKEY_MODE_CHANGED
    // → HookEngine via callback) uses .store(release).
    std::atomic<bool> vietnameseMode_{true};
    uint8_t startupMode_ = 0;  // 0=Vietnamese, 1=English, 2=Remember
    std::atomic<bool> sending_{false};  // True while SendInput is in progress (skip re-entrant hook calls)
    std::atomic<int> synthEventsPending_{0};  // Count of synthetic INPUT structs sent but not yet processed by hook
    DWORD lastSynthSendTime_ = 0;  // GetTickCount() of last SendInput call (watchdog: reset if stuck > 500ms)
    DWORD lastRealSynthTime_ = 0;  // GetTickCount() of last typing-related dispatch (not InjectKey re-injection)
    bool hadSynthInWord_ = false;  // True if any synthetic event was sent for the current word (blocks passthrough mixing)
    bool beepOnSwitch_ = false;
    bool smartSwitch_ = false;
    bool excludeApps_ = false;
    bool tsfApps_ = false;
    bool cjkAutoSwitch_ = false;  // Opt-in via UI; ApplyConfig overrides at startup.
    // Sprint 1 D5.2: config-derived flags read on the hook callback path
    // (ProcessKeyDown / HandleAlphaKey / TryExpandMacro). Writers: ApplyConfig
    // (main thread). Readers: hook hot path uses .load(acquire); other call
    // sites also use .load(acquire) for uniform pattern (cost = MOV on x86).
    std::atomic<bool> autoCaps_{false};
    std::atomic<bool> autoCapsMacro_{false};
    std::atomic<uint8_t> tempOffMethod_{0};  // TempOffMethod (see TypingConfig.h)
    // tempEngineOff_ / digitLedWord_: per-word "treat as English" flags.
    // Plain bool — written from hook thread (DispatchKeyAction, ProcessKeyUp)
    // AND main/WinEvent thread (OnFocusChanged, ToggleVietnameseMode, ClearWordState).
    // Benign race accepted: word-aligned bool reads/writes are atomic on x86/x64,
    // and a stale read at most delays the per-word reset by one keystroke (which
    // the next boundary key recovers from). Promoting to std::atomic<bool> would
    // require similar treatment of tempMacroOff_ / autoCapState_ etc. — out of
    // scope for this change.
    bool tempEngineOff_ = false;       // True = Vietnamese bypassed for current word (user-initiated via double-Alt / Ctrl-toggle)
    bool digitLedWord_ = false;        // True = current word started with a digit (VNI/Combined/UserDefined) → auto-bypass (no user action)
    // Per-modifier tap tracking for HotkeyRegistry ToggleEnabled detection.
    // Indexed by ModIdx{Ctrl=0, Shift=1, Alt=2, Win=3}. Generalizes the
    // legacy altTapCount_/lastAltReleaseTime_ pair that only tracked Alt —
    // the unified registry can bind 2× to any modifier, so we need 4 slots.
    static constexpr int kModCount = 4;
    int   modTapCount_[kModCount]   = {0, 0, 0, 0};
    DWORD modTapLastTs_[kModCount]  = {0, 0, 0, 0};
    static constexpr DWORD kDoubleTapTimeoutMs = 400;
    /// Keystroke-based auto-capitalize state machine (used when no TSF anchor truth).
    /// Enum + transition rule live in core/AutoCapStateTransition.h so Linux GTest
    /// can exercise the modifier-gate contract without depending on Win32.
    AutoCapState autoCapState_ = AutoCapState::Idle;
    std::unordered_set<std::wstring> excludedAppSet_;  // excluded apps: force English on focus
    // Sprint 1 D5.2: per-app cached + macro config flags read on hook callback
    // path. Writers: ApplyConfig (main), ReloadFromToml (main),
    // OnFocusChanged + RefreshFocusCache (main, via WinEventProc),
    // VerifyExcludedState (main), ToggleVietnameseMode (main).
    // Readers: ProcessKeyDown / ProcessKeyUp / HandleAlphaKey / output dispatch
    // (DispatchSendInput, SendCharEvents, SendBackspaces) on the hook hot path
    // — all use .load(acquire). Same-thread reads on writer paths use the
    // same idiom for uniformity (cost = MOV on x86).
    std::atomic<bool> isExcludedApp_{false};      // cached: current app is excluded
    std::atomic<DWORD> excludedPid_{0};           // PID of excluded app (fast check in ProcessKeyDown)
    std::unordered_set<std::wstring> tsfAppSet_;  // apps that should use TSF engine instead of hook
    // Sprint 1 D5.1: migrated to std::atomic. Writers: ReloadFromToml (main) +
    // OnFocusChanged (main, via WinEventProc). Readers: ProcessKeyDown +
    // ProcessKeyUp early-return gates on the hook hot path.
    std::atomic<bool> isTsfApp_{false};       // cached: is current foreground app in TSF list?
    // Sprint 2 D3 deleted: dispatch flag isConsoleApp_ — Console hosts now
    // selected via WindowClassification.isConsole → SplitDispatchInjector(5)
    // by the factory; no remaining HookEngine reader. Sprint 2 D4 deleted
    // useEditMsgPath_ — replaced by IsSyncReplaceChannel() (SettleBudget==0
    // proxy). Post-T3 ChannelTraits cleanup deleted isElectronApp_ +
    // needBaitChar_ — both flags moved onto IOutputInjector
    // (HasMultiProcessRenderer() / NeedsBaitCharPrefix()). Single source
    // of truth on the injector itself.
    std::unordered_set<std::wstring> webView2PositiveCache_;  // full exe path → known WebView2 host (positive-only; see IsWebView2App)
    std::atomic<bool> skipEmptyChar_{false};  // Skip U+202F for Qt/Electron and Console apps
    std::atomic<bool> useClipboardPaste_{false};  // VB6 and legacy ANSI-internal apps need clipboard paste
    DWORD lastForegroundPid_ = 0;  // PID of last known foreground (updated by OnFocusChanged + timer)
    std::unordered_map<std::wstring, bool> appModeMap_;  // exe name → vietnamese mode
    bool appModeDirty_ = false;  // True when appModeMap_ changed since last TOML save
    SmartSwitchManager smartSwitchMgr_;  // Shared memory for per-app mode
    std::wstring currentExe_;  // Currently focused app
    std::wstring previousExe_;  // Previously focused app (for tray menu context)
    CodeTable currentCodeTable_ = CodeTable::Unicode;
    CodeTable globalCodeTable_ = CodeTable::Unicode;     // config value, restored when no override
    std::unordered_map<std::wstring, int8_t> appEncodingOverrides_;   // exe → encoding override (-1=inherit)
    std::unordered_map<std::wstring, int8_t> appSendMethodOverrides_; // exe → send method override (-1=inherit)
    InputMethod globalInputMethod_ = InputMethod::Telex; // config value, restored when no override
    std::unordered_map<std::wstring, int8_t> appInputMethodOverrides_; // exe → method override (-1=inherit)

    // Per-HWND classification cache. Each focus change normally calls
    // ClassifyWindow + GetExeNameForHwnd + (sometimes) IsWebView2App, costing
    // 5-10 Win32 syscalls per change. With Alt+Tab between known apps these
    // results are stable for the (HWND, PID) pair; cache them and short-
    // circuit on hit. PID re-check on lookup detects HWND reuse after the
    // owning process dies (Windows can recycle HWND values).
    //
    // Single-threaded: only `OnFocusChanged` and `OnTickPoll` (both on the
    // hook thread per `HookThreadProc`) read/write the cache, so no lock.
    struct AppProfile {
        DWORD pid = 0;
        std::wstring exeName;       // lowercase exe name (matches override-map keys)
        bool isBrowser   = false;
        bool isElectron  = false;
        bool isQtApp     = false;
        bool isConsole   = false;
        bool isVB6       = false;
        bool isWebView2  = false;   // result of IsWebView2App scan (avoids child-window walk on hit)
        uint64_t cachedAt = 0;      // GetTickCount64() — for LRU eviction
    };
    std::unordered_map<HWND, AppProfile> appProfileCache_;
    static constexpr size_t kMaxAppProfileCache = 64;  // bounded; LRU evict on insert

    // Returns pointer into `appProfileCache_` if HWND is cached AND its current
    // PID matches the cached entry. PID-mismatch entries are evicted in place
    // (HWND was reused by a different process). Returns nullptr on miss.
    [[nodiscard]] const AppProfile* LookupAppProfile(HWND hwnd) noexcept;
    // Insert/update the cache entry for `hwnd`. LRU-evicts the oldest entry
    // (by `cachedAt`) when at capacity.
    void StoreAppProfile(HWND hwnd, AppProfile profile) noexcept;

    // Backspace-into-committed-word (re-enter composition after commit + backspace)
    // inputHistory_ records exact user keystrokes (including backspace as '\b')
    // so replay produces identical engine state. This differs from engine's rawInput_
    // which mutates on escape sequences (EraseConsumedRaw).
    static constexpr wchar_t kBackspaceMarker = L'\b';
    static constexpr size_t kMaxCommitStack = 3;  // Max words to remember for backward
    static constexpr size_t kMaxSmartSwitchEntries = 200;  // Cap per-app mode memory
    // Auto-expire the Ready state after this many ms — cheap insurance against any
    // cursor-movement event that bypasses ResetComposition (e.g. future edge cases).
    static constexpr DWORD kCommitUndoTimeoutMs = 4000;
    // Sprint 2 D5: kSynthSettleMs (was 100 ms hardcoded for all hosts) replaced
    // by per-injector budget — `injector_->SettleBudget()` returns 0 ms for
    // RichEdit (sent message drains synchronously), 30 ms for Win32 batch,
    // and 100 ms for Split (Electron/Console). Read inline at the gate sites
    // so a focus change (re-publishing a different injector) takes effect on
    // the next keystroke without staleness.

    enum class CommitUndoState : uint8_t {
        Idle   = 0,  // No pending undo
        Ready  = 1,  // Just committed with Space/Enter — waiting for first BS
        Primed = 2,  // Space deleted — next Alpha/BS triggers replay
    };

    struct CommitEntry {
        std::vector<wchar_t> history;   // User keystrokes for replay
        std::wstring text;              // What was on screen when committed
        std::wstring rawInput;          // engine_->PeekRaw() snapshot — for Esc-restore-raw post-BS (design 2026-05-17)
        std::vector<uint8_t> widths;    // Encoded widths for non-Unicode code tables
        uint8_t extraLeadingTriggers = 0;  // Extra trigger chars typed between previous commit and this word's body — must be backspaced before this entry's commit trigger can be primed during multi-word undo
    };

    std::vector<wchar_t> inputHistory_;         // User keystrokes for current composition
    std::vector<CommitEntry> commitStack_;       // Stack of committed words (LIFO, max kMaxCommitStack)
    bool pushedToStack_ = false;                 // True if last CommitComposition pushed to stack
    CommitUndoState commitUndoState_ = CommitUndoState::Idle;
    uint8_t pendingTriggerCount_ = 0;           // Extra commit triggers typed while Ready (need BS before Primed)
    uint8_t leadingTriggersForCurrentWord_ = 0; // pendingTriggerCount_ snapshot for the in-progress word — survives Ready→Idle and replay pops
    DWORD commitReadyTime_ = 0;                 // GetTickCount() when entering Ready state

    // Macro expansion
    // Sprint 1 D5.2: macroEnabled_, macroInEnglish_, tempOffMacroByEsc_ migrated
    // to std::atomic — read on hook hot path (ProcessKeyDown step 2c, alpha key
    // path, TryExpandMacro). tempMacroOff_ / macroCrossCommit_ are per-word
    // runtime state on the hook thread only — no atomic needed.
    std::atomic<bool> macroEnabled_{false};
    std::atomic<bool> macroInEnglish_{false};
    std::atomic<bool> tempOffMacroByEsc_{false};  // Config: Esc can temp-disable macro
    std::atomic<bool> escRestoreRawEnabled_{false};  // Config: Esc restores raw keys (víu → virus)
    bool tempMacroOff_ = false;       // Runtime: macro disabled for current word; same-thread (hook) only
    bool macroCrossCommit_ = false;   // rawMacroBuffer_ spans multiple engine commits; same-thread (hook) only
    std::unordered_map<std::wstring, std::wstring> macroTable_;
    std::unordered_set<std::wstring> spaceMacroKeys_;  // subset of macroTable_ keys that contain ' '
    std::wstring rawMacroBuffer_;

    // Hooks
    HHOOK keyboardHook_ = nullptr;
    HHOOK mouseHook_ = nullptr;
    HWINEVENTHOOK focusHook_ = nullptr;     // EVENT_SYSTEM_FOREGROUND
    HWINEVENTHOOK minimizeHook_ = nullptr;  // EVENT_SYSTEM_MINIMIZEEND
    // Sprint 1 D10: 200 ms focus / CJK poll moved off SetTimer onto
    // MainThreadWorker's tick branch. The body lives in OnTickPoll().

    // Dedicated hook thread: owns keyboardHook_ + mouseHook_ and runs its own
    // GetMessage pump so LL hook callbacks never block on the main (UI) thread's
    // message queue. Win10 silently removes LL hooks whose installer-thread pump
    // can't service hook events within LowLevelHooksTimeout (max 1000ms). Sciter
    // rendering, SharedState lock contention, and config reloads on main were
    // causing that — isolating the hook thread fixes it.
    std::thread hookThread_;
    DWORD hookThreadId_ = 0;                       // GetCurrentThreadId() of hookThread_ (for PostThreadMessage)
    std::atomic<bool> hookThreadReady_{false};     // true once hooks installed (or failed)
    std::mutex hookStartMutex_;                    // pairs with hookStartCv_ for handshake
    std::condition_variable hookStartCv_;
    HINSTANCE cachedHInstance_ = nullptr;          // captured in Start(), used by HookThreadProc
    // Sprint 1 D11: downgraded from recursive_mutex to plain mutex. After Phase B
    // (D5–D7), all hook-read state is atomic — hook callbacks no longer acquire
    // this mutex for reads. The remaining users are main-thread / worker-thread
    // writers (ApplyConfig requires caller-held; QuickSyncFromSharedState
    // self-locks; ReloadFromToml/Toggle/SetCodeTable/CommitPending
    // lock at their public entry; OnTickPoll — formerly FocusPollTimerProc, now
    // driven by MainThreadWorker per D10 — locks for the layout check + PID
    // update phase, releases before invoking OnFocusChanged so the inner
    // QuickSync self-lock isn't recursive). Pillar #2 (Nhẹ): smaller primitive
    // when recursion is no longer required.
    mutable std::mutex stateMutex_;
    void HookThreadProc();                         // runs on hookThread_

    // Reinstall both keyboard and mouse LL hooks. Used by WM_APP_REINSTALL_HOOKS
    // path (proactive reinstall on focus → Chromium / Electron / Java). Returns
    // false if either SetWindowsHookExW call fails (caller logs GetLastError).
    [[nodiscard]] bool ReinstallKeyboardAndMouseHooks();

    // Modifier tracking state (for double-Alt and layout change detection)
    bool modCtrlDown_ = false;
    bool modShiftDown_ = false;
    bool modAltDown_ = false;
    bool modWinDown_ = false;
    bool otherKeyPressed_ = false;

    // CJK layout auto-toggle: auto-switch to E mode when CJK detected, restore on return
    bool layoutSuppressed_     = false;  // True when CJK layout active
    bool modeBeforeCjk_        = true;   // Saved vietnameseMode_ before CJK auto-switch
    bool cachedIsCompatLayout_ = true;   // Last known layout compatibility (updated in OnFocusChanged + key-up)

    // Focused child HWND + class name, cached to avoid AttachThreadInput per keystroke
    // (used by TryEditMessagePaste for VB6/ANSI apps). Refreshed in OnFocusChanged and
    // invalidated on mouse click (within-app focus change).
    //
    // Cross-thread: written by LowLevelMouseProc (mouse hook thread) AND
    // WinEventProc (window event thread); read by key thread. HWND is atomic
    // (compiler-fence + intent doc; x64 hardware already torn-read-safe for
    // 8B aligned pointers). Class wstring is NOT atomic — the resulting
    // tuple race is benign: a brief stale-class read causes at worst a
    // 1-keystroke filter miss, which the next focus change recovers.
    std::atomic<HWND> cachedFocusedHwnd_{nullptr};
    std::wstring cachedFocusedClass_;

    // Direct SharedState reader — pointer to the global SharedStateManager (same process)
    SharedStateManager* sharedStatePtr_ = nullptr;
    uint32_t lastFeatureFlags_ = 0;
    uint8_t lastSpellCheck_ = 0;
    uint8_t lastInputMethod_ = 0;
    uint8_t lastCodeTable_ = 0;
    uint8_t lastTempOffMethod_ = 0;
    void QuickSyncFromSharedState();
    void ReloadFromToml();  // Full TOML reload (macros, excluded apps, hotkeys, etc.)
    // Pre-T3 Minor 2 fix (Rule #11.3): atomic for lock-free hot-path read
    // in QuickSyncFromSharedState. Writer (slow path inside stateMutex_):
    // release-store after applying SharedState. Reader (lock-free fast path
    // on hook thread): acquire-load + epoch compare; equal → early-return
    // without ever taking stateMutex_. Initialised to 0 so the first call
    // always enters the slow path (any valid SharedState epoch mismatches).
    std::atomic<uint32_t> lastEpoch_{0};  // Epoch fast path — skip full Read() when unchanged
    uint8_t lastConfigGeneration_ = 0;   // Tracks configGeneration from SharedState

    // Callbacks
    ModeChangeCallback modeChangeCallback_;
    std::function<void()> configReloadCallback_;
    std::function<void(bool, bool)> tsfModeCallback_;

    // Singleton for static callback dispatch (read from hook callback thread)
    static std::atomic<HookEngine*> s_instance;
};

}  // namespace NextKey
