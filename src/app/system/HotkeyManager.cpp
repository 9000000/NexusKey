// VKey - Hotkey Manager Implementation
// SPDX-License-Identifier: GPL-3.0-only

#include "HotkeyManager.h"
#include "HookEngine.h"  // VKEY_EXTRA_INFO tag
#include "core/CrashLog.h"
#include "core/Debug.h"
#include <cassert>

namespace NextKey {

// Same prefix-only pattern as HOOK_LOG / NEXTKEY_LOG. Args not evaluated when
// the runtime logger gate is off — safe on the LL hook hot path.
#define HOTKEY_LOG(fmt, ...) do {                                              \
    if (::NextKey::Logger::IsEnabled())                                        \
        ::NextKey::Logger::Log(L"[Hotkey] " fmt, ##__VA_ARGS__);               \
} while (0)

std::atomic<HotkeyManager*> HotkeyManager::s_instance{nullptr};

HotkeyManager::~HotkeyManager() {
    Uninstall();
}

HotkeyManager::SlotId HotkeyManager::AddHotkey(const HotkeyConfig& config, Callback callback) {
    // Contract: AddHotkey must precede Initialize() — slotState_.resize below
    // races with LL callback's slotState_[i] read if the hook is already up.
    // Debug-only enforcement; release builds rely on call-site discipline.
    assert(!keyboardHook_ && "AddHotkey called after Initialize — slotState_ resize would race with LL callback");
    std::lock_guard lk(mutationMutex_);
    auto oldBindings = bindings_.load(std::memory_order_acquire);
    auto newBindings = std::make_shared<std::vector<SlotBinding>>(*oldBindings);
    newBindings->push_back(SlotBinding{config, std::move(callback)});
    const SlotId id = newBindings->size() - 1;
    slotState_.resize(id + 1);
    bindings_.store(std::move(newBindings), std::memory_order_release);
    HOTKEY_LOG(L"AddHotkey slot=%zu vk=0x%02X ctrl=%d shift=%d alt=%d win=%d (%s)",
               id, config.vk, config.ctrl, config.shift, config.alt, config.win,
               config.vk == 0 ? L"modifier-only" : L"combo");
    return id;
}

void HotkeyManager::UpdateHotkey(SlotId slot, const HotkeyConfig& config) {
    std::lock_guard lk(mutationMutex_);
    auto oldBindings = bindings_.load(std::memory_order_acquire);
    if (slot >= oldBindings->size()) return;
    // Skip if config unchanged — preserves comboKeyDown across spurious reloads.
    // Without this, a config reload while the user holds the combo would (after
    // Wave 1) be a no-op on slotState_ anyway, but skipping also avoids
    // publishing a redundant snapshot.
    if ((*oldBindings)[slot].config == config) return;
    auto newBindings = std::make_shared<std::vector<SlotBinding>>(*oldBindings);
    (*newBindings)[slot].config = config;
    // comboKeyDown lives in slotState_[slot] and stays put across UpdateHotkey
    // by design — its in-flight DOWN should still be paired with the matching
    // UP regardless of config rebinding.
    bindings_.store(std::move(newBindings), std::memory_order_release);
    HOTKEY_LOG(L"UpdateHotkey slot=%zu vk=0x%02X ctrl=%d shift=%d alt=%d win=%d (%s)",
               slot, config.vk, config.ctrl, config.shift, config.alt, config.win,
               config.vk == 0 ? L"modifier-only" : L"combo");
}

void HotkeyManager::Initialize(HINSTANCE hInstance) {
    s_instance.store(this, std::memory_order_release);
    InstallKeyboardHook(hInstance);
    const size_t count = bindings_.load(std::memory_order_acquire)->size();
    NEXTKEY_LOG(L"HotkeyManager installed (%zu slot%s, hookTid=%lu)",
                count, count == 1 ? L"" : L"s",
                hookThreadId_.load(std::memory_order_acquire));
}

void HotkeyManager::Uninstall() {
    if (keyboardHook_) {
        UnhookWindowsHookEx(keyboardHook_);
        keyboardHook_ = nullptr;
        HOTKEY_LOG(L"Keyboard hook uninstalled");
    }
    if (s_instance.load(std::memory_order_acquire) == this) {
        s_instance.store(nullptr, std::memory_order_release);
    }
    std::lock_guard lk(mutationMutex_);
    bindings_.store(std::make_shared<std::vector<SlotBinding>>(),
                    std::memory_order_release);
    slotState_.clear();
}

void HotkeyManager::InstallKeyboardHook(HINSTANCE hInstance) {
    keyboardHook_ = SetWindowsHookExW(WH_KEYBOARD_LL, LowLevelKeyboardProc, hInstance, 0);
    if (keyboardHook_) {
        NEXTKEY_LOG(L"Keyboard hook installed for hotkey");
    } else {
        NEXTKEY_LOG(L"Keyboard hook failed (error: %lu)", GetLastError());
    }
}

// Inject VK_LCONTROL down+up tagged with VKEY_EXTRA_INFO to break Windows
// "Alt/Win tapped alone" detection. Without this, releasing Alt before the key
// in combos like Alt+Z activates the browser menu bar (Firefox) or steals focus
// (Chrome). HookEngine::LowLevelKeyboardProc passes through events with this tag.
void HotkeyManager::InjectDummyKey() noexcept {
    static const WORD scan = static_cast<WORD>(MapVirtualKeyW(VK_LCONTROL, MAPVK_VK_TO_VSC));
    INPUT inputs[2] = {};

    inputs[0].type = INPUT_KEYBOARD;
    inputs[0].ki.wVk = VK_LCONTROL;
    inputs[0].ki.wScan = scan;
    inputs[0].ki.dwExtraInfo = HookEngine::VKEY_EXTRA_INFO;

    inputs[1].type = INPUT_KEYBOARD;
    inputs[1].ki.wVk = VK_LCONTROL;
    inputs[1].ki.wScan = scan;
    inputs[1].ki.dwFlags = KEYEVENTF_KEYUP;
    inputs[1].ki.dwExtraInfo = HookEngine::VKEY_EXTRA_INFO;

    SendInput(2, inputs, sizeof(INPUT));
}

void HotkeyManager::DispatchHotkeyFromHookThread(SlotId slot) {
    HotkeyManager* inst = s_instance.load(std::memory_order_acquire);
    if (!inst) return;
    auto bindings = inst->bindings_.load(std::memory_order_acquire);
    if (slot >= bindings->size()) return;
    const auto& binding = (*bindings)[slot];
    if (binding.callback) binding.callback();
}

LRESULT CALLBACK HotkeyManager::LowLevelKeyboardProc(int nCode, WPARAM wParam, LPARAM lParam) {
    try {
        HotkeyManager* inst = s_instance.load(std::memory_order_acquire);
        if (nCode != HC_ACTION || !inst) {
            return CallNextHookEx(nullptr, nCode, wParam, lParam);
        }

        auto& self = *inst;
        auto* pKey = reinterpret_cast<KBDLLHOOKSTRUCT*>(lParam);

        // Pass through our own injected dummy events untouched.
        if (pKey->dwExtraInfo == HookEngine::VKEY_EXTRA_INFO) {
            return CallNextHookEx(nullptr, nCode, wParam, lParam);
        }

        const bool isDown = (wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN);
        const bool isUp = (wParam == WM_KEYUP || wParam == WM_SYSKEYUP);
        const DWORD vk = pKey->vkCode;

        const bool isCtrl = (vk == VK_LCONTROL || vk == VK_RCONTROL);
        const bool isShift = (vk == VK_LSHIFT || vk == VK_RSHIFT);
        const bool isAlt = (vk == VK_LMENU || vk == VK_RMENU);
        const bool isWin = (vk == VK_LWIN || vk == VK_RWIN);
        const bool isModifier = isCtrl || isShift || isAlt || isWin;

        // Snapshot pre-update state so modifier-only release checks see the modifier
        // as "still held" (match the original per-modifier semantics).
        const bool preCtrl = self.modCtrlDown_;
        const bool preShift = self.modShiftDown_;
        const bool preAlt = self.modAltDown_;
        const bool preWin = self.modWinDown_;
        const bool preOtherKey = self.otherKeyPressed_;

        // Update modifier state. Reset otherKeyPressed_ on modifier down-transition.
        if (isCtrl) {
            if (isDown && !self.modCtrlDown_) { self.modCtrlDown_ = true; self.otherKeyPressed_ = false; }
            else if (isUp) self.modCtrlDown_ = false;
        } else if (isShift) {
            if (isDown && !self.modShiftDown_) { self.modShiftDown_ = true; self.otherKeyPressed_ = false; }
            else if (isUp) self.modShiftDown_ = false;
        } else if (isAlt) {
            if (isDown && !self.modAltDown_) { self.modAltDown_ = true; self.otherKeyPressed_ = false; }
            else if (isUp) self.modAltDown_ = false;
        } else if (isWin) {
            if (isDown && !self.modWinDown_) { self.modWinDown_ = true; self.otherKeyPressed_ = false; }
            else if (isUp) self.modWinDown_ = false;
        } else if (isDown) {
            self.otherKeyPressed_ = true;
        }

        // Strict XOR: required modifiers must be held AND non-required modifiers
        // must NOT be held. Prevents Alt+Z hotkey from firing on Ctrl+Alt+Z.
        auto matchCombo = [&](const HotkeyConfig& cfg) noexcept {
            return cfg.ModifiersMatch(self.modCtrlDown_, self.modShiftDown_,
                                      self.modAltDown_, self.modWinDown_);
        };

        auto matchModifierOnlyRelease = [&](const HotkeyConfig& cfg) noexcept {
            return cfg.ModifiersMatch(preCtrl, preShift, preAlt, preWin);
        };

        // RCU snapshot read — lock-free. The pointee vector is immutable; mutators
        // publish a fresh shared_ptr. slotState_ is owned by `self` (LL-thread-only
        // mutator after Initialize), indexed in parallel with `bindings`. bindings_
        // is never null after default ctor — no null check needed.
        const auto bindings = self.bindings_.load(std::memory_order_acquire);
        const auto& slots = *bindings;
        const DWORD hookTid = self.hookThreadId_.load(std::memory_order_acquire);

        // ─── Combo hotkey: target key DOWN fires, UP is eaten ───
        if (!isModifier && (isDown || isUp)) {
            for (size_t i = 0; i < slots.size(); ++i) {
                const auto& slot = slots[i];
                if (slot.config.vk == 0) continue;  // Modifier-only slot
                if (vk != slot.config.vk) continue;

                // Mirror old index; slotState_ is sized in lock-step with bindings_.
                // Defensive bound — should never trigger but cheap.
                if (i >= self.slotState_.size()) continue;
                auto& state = self.slotState_[i];

                if (isDown) {
                    if (state.comboKeyDown) return 1;  // Eat auto-repeat
                    if (matchCombo(slot.config)) {
                        state.comboKeyDown = true;
                        HOTKEY_LOG(L"combo fire slot=%zu vk=0x%02X mods=C%dS%dA%dW%d",
                                   i, vk,
                                   self.modCtrlDown_, self.modShiftDown_,
                                   self.modAltDown_, self.modWinDown_);
                        // Post to hook thread when available; callback runs there
                        // (hook-thread context, safe for engine-state writes like
                        // HookEngine::CommitPending). Fallback: TSF-only mode runs
                        // without a HookEngine hook thread (hookTid==0), so the
                        // callback dispatches inline on the LL thread — that path
                        // is safe because TSF-only callbacks are PostMessage-style
                        // (see main.cpp:704-711, no engine-state writes).
                        if (hookTid) {
                            PostThreadMessageW(hookTid, WM_APP_HOTKEY_FIRED,
                                               static_cast<WPARAM>(i), 0);
                        } else if (slot.callback) {
                            slot.callback();
                        }
                        if (slot.config.alt || slot.config.win) InjectDummyKey();
                        return 1;  // Eat DOWN
                    } else {
                        // Diagnostic: vk matched a bound slot but modifier set
                        // didn't. Catches "Ctrl+Shift+J bound but user pressed
                        // just J" — distinguishes "key never reached us" from
                        // "key reached us, modifiers wrong".
                        HOTKEY_LOG(L"combo miss slot=%zu vk=0x%02X want=C%dS%dA%dW%d got=C%dS%dA%dW%d",
                                   i, vk,
                                   slot.config.ctrl, slot.config.shift,
                                   slot.config.alt, slot.config.win,
                                   self.modCtrlDown_, self.modShiftDown_,
                                   self.modAltDown_, self.modWinDown_);
                    }
                } else {  // isUp
                    if (state.comboKeyDown) {
                        state.comboKeyDown = false;
                        return 1;  // Eat matching UP
                    }
                }
            }
        }

        // ─── Modifier-only hotkey: fires on modifier UP if no non-modifier was pressed ───
        if (isModifier && isUp) {
            for (size_t i = 0; i < slots.size(); ++i) {
                const auto& slot = slots[i];
                if (slot.config.vk != 0) continue;  // Combo slot
                const auto& c = slot.config;
                if (!c.HasAny()) continue;  // Empty config would match everything
                if (!matchModifierOnlyRelease(c)) continue;

                if (preOtherKey) {
                    // Diagnostic: modifier set matched but a non-modifier key
                    // was pressed between modifier-down and this modifier-up.
                    // Distinguishes "user pressed modifier-only correctly but
                    // hook missed it" from "user accidentally tapped another
                    // key during the modifier hold".
                    HOTKEY_LOG(L"modifier-only miss slot=%zu vk=0x%02X (otherKey was pressed)",
                               i, vk);
                    continue;
                }
                HOTKEY_LOG(L"modifier-only fire slot=%zu released=0x%02X pre=C%dS%dA%dW%d",
                           i, vk,
                           preCtrl, preShift, preAlt, preWin);
                if (hookTid) {
                    PostThreadMessageW(hookTid, WM_APP_HOTKEY_FIRED,
                                       static_cast<WPARAM>(i), 0);
                } else if (slot.callback) {
                    slot.callback();
                }
            }
        }
    } catch (const std::exception& e) {
        CrashLog(L"HotkeyManager::LowLevelKeyboardProc", e.what());
    } catch (...) {
        CrashLog(L"HotkeyManager::LowLevelKeyboardProc", "(non-std exception)");
    }

    return CallNextHookEx(nullptr, nCode, wParam, lParam);
}

}  // namespace NextKey
