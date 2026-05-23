// VKey - Hotkey Manager
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "core/config/TypingConfig.h"
#include <Windows.h>
#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <vector>

namespace NextKey {

/// Multi-slot keyboard hotkey manager. Each slot holds a HotkeyConfig + callback.
/// VKey is a single-layout TIP on English — no layout switching needed.
///
/// Implemented via WH_KEYBOARD_LL (no RegisterHotKey) so we can:
///   1. Eat DOWN/UP of the target key to prevent double-activation.
///   2. Suppress auto-repeat while the combo is held.
///   3. Inject a dummy key tagged with VKEY_EXTRA_INFO to break Windows
///      "Alt/Win tapped alone" detection (browser menu activation bug).
///
/// Wave 1 (2026-05-23) — RCU + cross-thread dispatch:
///   * Slot bindings (config + callback) live in an RCU-published shared_ptr<vector>.
///     The LL hook callback reads lock-free; mutators (AddHotkey/UpdateHotkey/
///     Uninstall) publish new snapshots under `mutationMutex_`.
///   * `comboKeyDown` moves to a parallel `slotState_` vector. Single-writer (LL
///     thread) after Initialize() — no lock needed on the LL hot path.
///   * Matched slots route via PostThreadMessage(hookThreadId_, WM_APP_HOTKEY_FIRED,
///     slotId) → HookEngine pump invokes DispatchHotkeyFromHookThread on the
///     hook thread, restoring the single-writer invariant for callbacks like
///     `hookEngine.CommitPending()` that touch engine state.
class HotkeyManager {
public:
    using Callback = std::function<void()>;
    using SlotId = size_t;

    HotkeyManager() = default;
    ~HotkeyManager();

    HotkeyManager(const HotkeyManager&) = delete;
    HotkeyManager& operator=(const HotkeyManager&) = delete;

    /// Register a hotkey slot. MUST be called before Initialize(). Returns
    /// the slot id for later UpdateHotkey calls. Callback runs on the hook
    /// thread (post-Wave 1) — keep it quick.
    SlotId AddHotkey(const HotkeyConfig& config, Callback callback);

    /// Replace an existing slot's config (used on config reload). Safe to
    /// call from any thread; serialized by mutationMutex_.
    void UpdateHotkey(SlotId slot, const HotkeyConfig& config);

    /// Set the hook thread id to route matched-slot dispatch into. MUST
    /// be called AFTER HookEngine::Start() returns and BEFORE Initialize()
    /// installs the LL hook — otherwise matched slots posted before the
    /// id is set will be dropped by PostThreadMessage(0, ...).
    void SetHookThreadId(DWORD tid) noexcept {
        hookThreadId_.store(tid, std::memory_order_release);
    }

    /// Install the LL keyboard hook. All AddHotkey() calls must happen first.
    void Initialize(HINSTANCE hInstance);

    /// Unhook and clear slots.
    void Uninstall();

    /// Hook-thread dispatch entry point. Called by HookEngine's pump on receipt
    /// of WM_APP_HOTKEY_FIRED. Loads the binding snapshot via RCU and invokes
    /// the per-slot callback. Public + static so the pump can reach it without
    /// holding a HotkeyManager pointer (uses s_instance).
    static void DispatchHotkeyFromHookThread(SlotId slot);

private:
    struct SlotBinding {
        HotkeyConfig config{};
        Callback callback;
    };
    struct SlotState {
        bool comboKeyDown = false;
    };

    void InstallKeyboardHook(HINSTANCE hInstance);
    static LRESULT CALLBACK LowLevelKeyboardProc(int nCode, WPARAM wParam, LPARAM lParam);
    static void InjectDummyKey() noexcept;

    // RCU-published binding snapshot. The LL hook callback reads via
    // atomic load (~5 ns, lock-free). Mutators publish a new shared_ptr
    // under `mutationMutex_`; the pointee vector is treated as immutable
    // after publish. Initialized lazily in AddHotkey / Uninstall.
    std::atomic<std::shared_ptr<std::vector<SlotBinding>>> bindings_;

    // Per-slot transient state. LL thread is the only writer; mutators only
    // grow this vector (in AddHotkey, under mutationMutex_) BEFORE Initialize()
    // installs the LL hook, so no read race exists. UpdateHotkey doesn't touch
    // it (preserves comboKeyDown across config reloads).
    std::vector<SlotState> slotState_;

    // Serializes mutators (AddHotkey/UpdateHotkey/Uninstall) with each other.
    // LL callback NEVER acquires this — uses atomic load on bindings_ instead.
    std::mutex mutationMutex_;

    // Set via SetHookThreadId(). Read by LL callback as the PostThreadMessage
    // target. Zero before SetHookThreadId is called — match drops silently.
    std::atomic<DWORD> hookThreadId_{0};

    HHOOK keyboardHook_ = nullptr;

    // Modifier tracking — LL thread is the single reader/writer. No external
    // access, so plain bool is fine (no atomicity required).
    bool modCtrlDown_ = false;
    bool modShiftDown_ = false;
    bool modAltDown_ = false;
    bool modWinDown_ = false;
    bool otherKeyPressed_ = false;

    static std::atomic<HotkeyManager*> s_instance;
};

}  // namespace NextKey
