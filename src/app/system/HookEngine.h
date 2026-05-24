// VKey - Keyboard Hook Engine
// SPDX-License-Identifier: GPL-3.0-only
//
// Single-process Vietnamese input using WH_KEYBOARD_LL.
// Replaces TSF DLL for MVP — no COM registration, no admin elevation.

#pragma once

#include "core/engine/IInputEngine.h"
#include "core/engine/CodeTableConverter.h"
#include "core/config/TypingConfig.h"
#include "core/config/ConfigSnapshot.h"
#include "core/hotkey/HotkeyRegistry.h"
#include "core/AutoCapStateTransition.h"
#include "core/SmartSwitchManager.h"
#include "app/system/HookCommandMailbox.h"
#include "app/system/HookLifecycle.h"
#include "app/system/FocusOwner.h"
#include "app/system/OutputDispatcher.h"
#include "core/pipeline/IBackwardEditExecutor.h"
#include "core/pipeline/ICommitUndoExecutor.h"
#include "core/pipeline/IEscRestoreRawExecutor.h"
#include "core/pipeline/IMacroExecutor.h"
#include "core/pipeline/Coordinator.h"
#include "core/pipeline/OutputChannel.h"
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

// Wave 3 PR 3.3 — IOutputInjector forward declaration removed; the type is
// pulled in transitively via OutputDispatcher.h (which directly includes
// output/IOutputInjector.h since it owns std::atomic<shared_ptr<IOI>>).
// HookEngine no longer touches IOI directly — all dispatch goes through
// dispatcher_.

namespace NextKey {

class SharedStateManager;  // Forward declaration (defined in core/ipc/SharedStateManager.h)

/// Callback when Vietnamese/English mode changes
using ModeChangeCallback = std::function<void(bool vietnamese)>;

/// Keyboard hook engine — intercepts keystrokes, processes Vietnamese input,
/// outputs via SendInput backspace+retype. Absorbs HotkeyManager logic.
class HookEngine
    : public NextKey::Pipeline::IBackwardEditExecutor
    , public NextKey::Pipeline::ICommitUndoExecutor
    , public NextKey::Pipeline::IEscRestoreRawExecutor
    , public NextKey::Pipeline::IMacroExecutor {
public:
    HookEngine();
    ~HookEngine() override;

    HookEngine(const HookEngine&) = delete;
    HookEngine& operator=(const HookEngine&) = delete;

    // Pipeline::IBackwardEditExecutor — Wave 2 adapter for BackwardEditFeature.
    // Thin wrapper around the existing ReplaceComposition; Wave 3+ will split the
    // pure-diff phase out into the feature and execute via OutputChannel.
    void ExecuteReplace(std::wstring_view newText,
                        std::uint16_t reinjectVk) override;

    // Pipeline::ICommitUndoExecutor — Wave 3 adapter for CommitUndoFeature.
    // Thin wrapper around the existing HandleCommitUndoFsm FSM body. Reads
    // vnMode internally from vietnameseMode_ atomic. Maps KeyOutcome →
    // CommitUndoOutcome at the boundary. Wave N+ will lift the FSM body
    // into CommitUndoFeature::Try for true single-owner state.
    [[nodiscard]] NextKey::Pipeline::CommitUndoOutcome HandleCommitUndo(
        std::uint16_t vkCode) override;

    // Pipeline::IEscRestoreRawExecutor — Wave 4a adapter for EscRestoreRawFeature.
    // Resolves hotkey registry (CancelComposition intent) + live/primed-commit
    // gates internally, then calls the existing TryEscRestoreRaw body if the
    // gate passes. Returns Eat on consume, Fallthrough otherwise.
    [[nodiscard]] NextKey::Pipeline::EscRestoreOutcome TryEscRestore(
        std::uint16_t vkCode,
        bool shift, bool ctrl, bool alt, bool win) override;

    // Pipeline::IMacroExecutor — Wave 4b adapter for MacroFeature.
    // Owns macro tracking (rawMacroBuffer_ accumulation) + dispatch:
    // EN mode (English macro), SkipMacro hotkey, expansion via TryExpandMacro.
    // Body transcribed 1:1 from pre-W4b HandlePreDispatch macro blocks; logic
    // unchanged. Reads vnMode/macroOn/macroEng atomics + RCU configSnapshot
    // internally so the feature interface stays decoupled from those fields.
    [[nodiscard]] NextKey::Pipeline::MacroOutcome HandleMacro(
        std::uint16_t vkCode,
        bool shift, bool capsLock, bool ctrl, bool alt, bool win) override;

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

    /// Wave 1 — return the dedicated hook thread id (target for PostThreadMessage).
    /// Returns 0 before Start() completes the hook-thread handshake; callers
    /// should query AFTER Start() returns. Used by HotkeyManager to route
    /// matched-slot dispatch back onto the hook thread.
    [[nodiscard]] DWORD GetHookThreadId() const noexcept { return lifecycle_.ThreadId(); }

    /// Change code table (commits pending composition, updates per-app map)
    void SetCodeTable(CodeTable ct);

    /// Get effective code table (checks manual per-app override map)
    [[nodiscard]] CodeTable GetCodeTable() const noexcept;

    [[nodiscard]] bool IsVietnameseMode() const noexcept {
        return vietnameseMode_.load(std::memory_order_acquire);
    }
    [[nodiscard]] bool IsRunning() const noexcept { return lifecycle_.IsRunning(); }

    // Magic number to mark our own SendInput events (prevents other hooks from processing them)
    static constexpr ULONG_PTR VKEY_EXTRA_INFO = 0x4E4B;  // "NK"

    // Wave 3 PR 3.2 — `GetExeNameForHwnd` migrated to FocusOwner alongside the
    // rest of the focus-classification subsystem. Callers reach it via
    // `NextKey::FocusOwner::GetExeNameForHwnd(hwnd)`.

private:
    // Hook callbacks (static → instance dispatch). WinEventProc moved to
    // FocusOwner (Wave 3 PR 3.2).
    static LRESULT CALLBACK LowLevelKeyboardProc(int nCode, WPARAM wParam, LPARAM lParam);
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
    [[nodiscard]] KeyOutcome HandleCommitUndoFsm(DWORD vkCode, bool vnMode);

    // H1c (extracted from ProcessKeyDown steps 3 / 3a-3d): English-mode short
    // circuit + Vietnamese-mode pre-dispatch tracking. When !vnMode, runs the
    // English-mode macro tracking block and returns Pass. When vnMode, updates
    // auto-caps FSM, accumulates the macro buffer, handles temp-off-by-Esc, and
    // attempts macro expansion on commit triggers — returning Eat/Pass on
    // expansion match, Fallthrough otherwise so the dispatch chain runs next.
    /// Esc-restore-raw: end composition with raw keys and inject them (víu → virus).
    /// Returns KeyOutcome::Eat on success, Fallthrough if buffer empty / disabled.
    [[nodiscard]] KeyOutcome TryEscRestoreRaw();

    [[nodiscard]] KeyOutcome HandlePreDispatch(DWORD vkCode, bool vnMode,
                                                bool cachedShift,
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
    void SetCommitUndoReady(); // commitUndoState_ = Ready + timestamp

    // Output — universal SendInput with KEYEVENTF_UNICODE. Wave 3 PR 3.3
    // shrank the body to a thin shim: prefix-diff + code-table encoding +
    // previousComposition_/Widths_ update, then dispatcher_.ReplaceUnicode
    // (Unicode path) or dispatcher_.ReplaceRaw (encoded path) handles the
    // RichEdit retry / clipboard fallback / SendInput orchestration.
    void ReplaceComposition(const std::wstring& newText, DWORD reinjectVk = 0);

    // Wave 2 — pipeline dispatch helper. Builds a HookCompositionSession +
    // KeyContext from current HookEngine state, hands them to coordinator_,
    // then drains any inadvertent intents from outputChannel_. In W2 the only
    // registered feature is BackwardEditFeature which delegates synchronously
    // to ExecuteReplace, so the batch is always empty after HandleKey returns.
    void DispatchCoordinator(DWORD vkCode, DWORD reinjectVk,
                              const std::wstring& composition);

    // Wave 3 PR 3.3 — SendBackspaces / SendBackspaceEvents / SendCharEvents,
    // OnSynthDispatched, IsSyncReplaceChannel, ShouldUseClipboard,
    // ClipboardPaste, TryEditMessagePaste, RecordSynthDispatch moved to
    // OutputDispatcher. Public surface via `dispatcher_.X()`.

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

    // Wave 3 PR 3.2 — IsTrayOrTaskbarWindow + IsWebView2App migrated to
    // FocusOwner (focus-classification helpers; no engine state).
    void NotifyModeChange() noexcept;  // Fire modeChangeCallback_ with effective mode
    bool VerifyExcludedState();        // Check if foreground is still excluded; clears stale flag if not
    // Phase 2b — two-phase focus.
    //
    //   FocusOwner::Classify runs on the CALLER thread (today: main, via
    //   WinEventProc / OnTickPoll). Heavy Win32 inspection lives there:
    //   ClassifyWindow + GetExeNameForHwnd + IsWebView2App + cache lookup/
    //   store + override-map reads. Per Rule 11.2 these MUST NOT run from
    //   the LL hook callback (CreateToolhelp32Snapshot violates the 30ms
    //   p99 budget). Returns a fully-populated FocusClassification POD.
    //
    //   OnFocusChanged is now a thin shim on HookEngine: QuickSync +
    //   focus_.Classify(...) + Post(kFocusChanged). It stays on main; the
    //   actual state mutation runs on the hook thread via the drain →
    //   ApplyFocusOnHookThread path.
    void OnFocusChanged(HWND triggerHwnd = nullptr);
    void OnLayoutChanged(bool isCompatibleNow);
    void CheckLayoutChange();  // Query current layout and call OnLayoutChanged if it changed
    void SaveEnglishModeAppsIfDirty();  // Persist English-mode apps to TOML

    // Phase 3d — single source of truth for snapshot rebuild. Parses
    // overrides/excluded/TSF/macros fresh from TOML, derives
    // spaceMacroKeys via ConfigSnapshot::Build, and atomic-publishes the
    // result. Replaces the four legacy Reload* methods + the dual-write
    // PublishConfigSnapshot bridge from P3b. The 2026-05-19 follow-up
    // also folded `appSendMethodOverrides` into the snapshot, closing
    // the last variable-size config map that was racing across threads.
    //
    // NOT `noexcept`: STL container allocations + `std::make_shared`
    // here can throw `std::bad_alloc`. Callers (ReloadFromToml,
    // QuickSync macro-toggle path, OnTickPoll drain) are reached from
    // sites with outer try/catch (LL callback catch for hook-thread
    // path; OnTickPoll's own try for worker path), so a throw unwinds
    // gracefully instead of `std::terminate`-ing the process.
    void RebuildSnapshotFromToml(std::uint32_t generation);

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
    // Phase 3 — RCU snapshot of variable-size config data. Holds the
    // excluded-apps / TSF-apps / macro / per-app override maps that the
    // hook hot path needs to read. Single producer is
    // `RebuildSnapshotFromToml` (worker thread or main; never hook —
    // hook side defers via `pendingConfigReload_`). Readers are
    // lock-free `configSnapshot_.load(acquire)`. Default-init to an
    // empty snapshot so the first reader before any rebuild publishes
    // still gets a dereferenceable pointer.
    std::atomic<std::shared_ptr<const ConfigSnapshot>> configSnapshot_{
        std::make_shared<const ConfigSnapshot>()
    };
    // Sprint 2 T3: Output channel strategy. RCU-published shared_ptr to the
    // active IOutputInjector, same pattern as config_ above. Writers (main
    // thread on focus change): two-phase classify → atomic_store. Readers
    // (hook hot path): atomic load → 1 virtual call (~11 ns total overhead).
    // Initialized in HookEngine ctor via Output::Create({}) so the field is
    // never nullptr — hot path's atomic_load can rely on a usable injector
    // even before any focus event has fired.
    // See docs/plans/sprint-2-output-injector.md §1 for the data-flow contract.
    // Wave 3 PR 3.3 — injector_ moved to OutputDispatcher. Read via
    // dispatcher_.GetInjector(); publish via dispatcher_.SetInjector().
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
    // Wave 3 PR 3.3 — sending_, synthEventsPending_, lastSynthSendTime_,
    // lastRealSynthTime_, hadSynthInWord_ moved to OutputDispatcher.
    // Readers go through dispatcher_.IsSending() / SynthEventsPending() /
    // LastSynthSendTime() / LastRealSynthTime() / HadSynthInWord().
    // Wave 2 (2026-05-23) — formerly cached bool fields (beepOnSwitch_, smartSwitch_,
    // excludeApps_, tsfApps_, cjkAutoSwitch_) deleted. Single source of truth is
    // `config_` RCU. Hot-path readers load once per function via
    //   `const auto cfg = config_.load(std::memory_order_acquire);`
    // then read `cfg->beepOnSwitch` etc. Eliminates the cache/sync surface that
    // forced ApplyConfig to run under stateMutex_.
    // Sprint 1 D5.2: config-derived flags read on the hook callback path
    // (ProcessKeyDown / HandleAlphaKey / TryExpandMacro). Writers: ApplyConfig
    // (main thread). Readers: hook hot path uses .load(acquire); other call
    // sites also use .load(acquire) for uniform pattern (cost = MOV on x86).
    std::atomic<bool> autoCaps_{false};
    std::atomic<bool> autoCapsMacro_{false};
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
    // Phase 3d: legacy `excludedAppSet_` removed — readers go through
    // configSnapshot_.load()->excludedAppSet. Same migration for
    // tsfAppSet_, macroTable_, spaceMacroKeys_, appEncodingOverrides_,
    // appInputMethodOverrides_, and `appSendMethodOverrides_`
    // (2026-05-19 follow-up — all six variable-size config maps now
    // publish through the snapshot).
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
    // (tsfAppSet_ removed — see Phase 3d note above)
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
    // Wave 3 PR 3.2 — webView2PositiveCache_ moved to FocusOwner.
    // Wave 3 PR 3.3 — skipEmptyChar_, useClipboardPaste_ moved to OutputDispatcher.
    // Wave 3 PR 3.2 — lastForegroundPid_, appModeMap_/appModeDirty_/smartSwitchMgr_,
    // currentExe_/previousExe_ moved to FocusOwner. Readers go through
    // focus_.LastForegroundPid()/AppModeMap()/Smart()/CurrentExe()/PreviousExe().
    // Writers: worker thread (ReloadFromToml → ApplyConfig) AND hook thread
    // (SetCodeTable, QuickSyncFromSharedState, focus override). Readers: hook
    // hot path (HandleAlphaKey, CommitComposition, ClassifyFocusedWindow,
    // ReplaceComposition, macro expansion). Atomic load/store with
    // acquire/release ordering — uint8_t underlying is lock-free on x64.
    std::atomic<CodeTable> currentCodeTable_{CodeTable::Unicode};
    std::atomic<CodeTable> globalCodeTable_{CodeTable::Unicode};   // config value, restored when no override
    // (appEncodingOverrides_, appInputMethodOverrides_, and
    // appSendMethodOverrides_ all removed — Phase 3d + 2026-05-19
    // follow-up. Readers go through configSnapshot_.load()->...)
    std::atomic<InputMethod> globalInputMethod_{InputMethod::Telex}; // config value, restored when no override

    // Wave 3 PR 3.2 — AppProfile struct + appProfileCache_ + LookupAppProfile/
    // StoreAppProfile moved to FocusOwner alongside Classify.

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
    // Sprint 1 D5.2: macroEnabled_, macroInEnglish_ migrated to std::atomic —
    // read on hook hot path (ProcessKeyDown step 2c, alpha key path,
    // TryExpandMacro). tempMacroOff_ / macroCrossCommit_ are per-word runtime
    // state on the hook thread only — no atomic needed.
    // tempOffMacroByEsc_ / escRestoreRawEnabled_ removed in v3 cleanup
    // (SkipMacro / CancelComposition triggers now live in HotkeyRegistry).
    std::atomic<bool> macroEnabled_{false};
    std::atomic<bool> macroInEnglish_{false};
    bool tempMacroOff_ = false;       // Runtime: macro disabled for current word; same-thread (hook) only
    bool macroCrossCommit_ = false;   // rawMacroBuffer_ spans multiple engine commits; same-thread (hook) only
    // (macroTable_ + spaceMacroKeys_ removed — Phase 3d. Live in
    // configSnapshot_->macroTable / ->spaceMacroKeys now.)
    std::wstring rawMacroBuffer_;

    // Hooks
    // Wave 3 PR 3.1 — hook handles, thread, mailbox moved to HookLifecycle.
    // Wave 3 PR 3.2 — focusHook_ / minimizeHook_ moved to FocusOwner.
    // Sprint 1 D10: 200 ms focus / CJK poll moved off SetTimer onto
    // MainThreadWorker's tick branch. The body lives in OnTickPoll().

    // Wave 3 PR 3.2 — FocusOwner declared BEFORE HookLifecycle so destruction
    // order (reverse of declaration) runs ~lifecycle_ first (joins hook
    // thread), then ~focus_ (Uninstalls WinEvent hooks on main). Mirrors the
    // Stop() sequence: lifecycle_.Stop() → focus_.Uninstall().
    FocusOwner focus_;

    // Wave 3 PR 3.3 — OutputDispatcher declared AFTER focus_ (uses const
    // ref to focus_ for TryEditMessagePaste's CachedFocusedHwnd/Class
    // reads) and BEFORE lifecycle_ (so destruction order joins the hook
    // thread first, then tears down dispatch state). Stop() mirrors:
    // lifecycle_.Stop() → dispatcher_.Uninstall() → focus_.Uninstall().
    OutputDispatcher dispatcher_{focus_};

    // Wave 3 PR 3.1 — dedicated hook thread + WH_KEYBOARD_LL/WH_MOUSE_LL hook
    // handles + cross-thread mailbox moved into HookLifecycle. HookEngine
    // accesses them via `lifecycle_.ThreadId()` / `lifecycle_.IsRunning()` /
    // `lifecycle_.Mailbox()` / `lifecycle_.PostReinstallHooks()`. Drain
    // callback registered at lifecycle_.Start() points at DrainHookCommands.
    HookLifecycle lifecycle_;

    void DrainHookCommands();                                 // hook thread only
    void ApplyFocusOnHookThread(std::shared_ptr<const FocusClassification> cls);
    void ApplyConfigOnHookThread();
    void ApplyTickPollOnHookThread();
    void ApplyToggleVNOnHookThread();
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

    // Wave 3 PR 3.1 (2026-05-23) — ReinstallKeyboardAndMouseHooks deleted as
    // dead code (no callers). The actual reinstall logic lives in
    // HookLifecycle::ThreadProc on the WM_APP_REINSTALL_HOOKS message; trigger
    // via `lifecycle_.PostReinstallHooks(REINSTALL_REASON_*)`.

    // Modifier tracking state (for double-Alt and layout change detection)
    bool modCtrlDown_ = false;
    bool modShiftDown_ = false;
    bool modAltDown_ = false;
    bool modWinDown_ = false;
    bool otherKeyPressed_ = false;

    // Wave 3 PR 3.2 — CJK layout state (layoutSuppressed_/modeBeforeCjk_/
    // cachedIsCompatLayout_) and focused-child cache (cachedFocusedHwnd_/
    // cachedFocusedClass_) moved to FocusOwner. Readers go through
    // focus_.LayoutSuppressed()/ModeBeforeCjk()/CachedIsCompatLayout()/
    // CachedFocusedHwnd()/CachedFocusedClass().

    // Direct SharedState reader — pointer to the global SharedStateManager (same process)
    SharedStateManager* sharedStatePtr_ = nullptr;
    // Wave 2 (2026-05-23) — atomized so QuickSync slow path + OnTickPoll TOML
    // drain no longer need stateMutex_ to serialize these fields with writers.
    // Reader/writer pairs are all .load(acquire) / .store(release).
    std::atomic<uint32_t> lastFeatureFlags_{0};
    std::atomic<uint8_t> lastSpellCheck_{0};
    std::atomic<uint8_t> lastInputMethod_{0};
    std::atomic<uint8_t> lastCodeTable_{0};
    void QuickSyncFromSharedState();
    void ReloadFromToml();  // Full TOML reload (macros, excluded apps, hotkeys, etc.)
    // Pre-T3 Minor 2 fix (Rule #11.3): atomic for lock-free hot-path read
    // in QuickSyncFromSharedState. Writer (slow path inside stateMutex_):
    // release-store after applying SharedState. Reader (lock-free fast path
    // on hook thread): acquire-load + epoch compare; equal → early-return
    // without ever taking stateMutex_. Initialised to 0 so the first call
    // always enters the slow path (any valid SharedState epoch mismatches).
    std::atomic<uint32_t> lastEpoch_{0};  // Epoch fast path — skip full Read() when unchanged
    std::atomic<uint8_t> lastConfigGeneration_{0};   // Tracks configGeneration from SharedState (Wave 2 atomized)
    // Phase 3c: cross-thread signal from hook slow path to worker tick.
    // When `QuickSyncFromSharedState` is entered on the hook thread and
    // detects a `configGeneration` bump, it MUST NOT run `ReloadFromToml`
    // (Rule 11.2 — TOML parse on hook). Instead, it sets this flag; the
    // next `OnTickPoll` (worker thread, 200ms cadence) drains it and
    // runs `ReloadFromToml` off-hook. Worker / main entries to QuickSync
    // still run Reload inline — they're already on a safe thread.
    std::atomic<bool> pendingConfigReload_{false};
    // Phase 3f: hook-thread latch — set when ApplyConfigOnHookThread can't
    // run yet because engine_->Count() > 0 (user mid-word). Drain checks
    // this every cycle and runs the apply once the engine empties (after
    // commit / backspace-clear / focus reset). Without this, config
    // reloads landing mid-word committed partial words via the engine
    // recreate path (surfaced by chaos `-InjectConfigReloadMs 50` as
    // `uongs` → `uôngs` instead of `uống`).
    std::atomic<bool> deferredConfigApply_{false};

    // Callbacks
    ModeChangeCallback modeChangeCallback_;
    std::function<void()> configReloadCallback_;
    std::function<void(bool, bool)> tsfModeCallback_;

    // Singleton for static callback dispatch (read from hook callback thread)
    static std::atomic<HookEngine*> s_instance;

    // Wave 2 — feature pipeline. Coordinator owns registered gates/features
    // (one of each in W2: EnglishBiasGate + BackwardEditFeature). outputChannel_
    // is the IntentSink for emitted intents — kept as a value member so the
    // batch buffer survives across keystrokes (TakeBatch drains it between
    // calls). Both are constructed in the HookEngine ctor body after the
    // injector is seeded; features are registered there with `*this` as the
    // IBackwardEditExecutor backing.
    NextKey::Pipeline::Coordinator   coordinator_;
    NextKey::Pipeline::OutputChannel outputChannel_;
};

}  // namespace NextKey
