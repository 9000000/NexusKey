// VKey - Passive Hotkey Manager Implementation
// SPDX-License-Identifier: GPL-3.0-only

#include "HotkeyManager.h"

#include <cassert>
#include <utility>

namespace NextKey {

HotkeyManager::HotkeyManager(DeferredFireSink deferredFireSink,
                             void* deferredFireContext) noexcept
    : deferredFireSink_(deferredFireSink),
      deferredFireContext_(deferredFireContext) {}

HotkeyManager::SlotId HotkeyManager::AddHotkey(const HotkeyConfig& config,
                                                Callback callback,
                                                PassThrough passThrough) {
    assert(!finalized_ && "AddHotkey must precede FinalizeBindings");
    assert(config.vk <= kVkMask && "Hotkey virtual key does not fit packed storage");
    if (finalized_ || config.vk > kVkMask) return 0;

    const SlotId slot = slots_.size();
    slots_.push_back({std::move(callback), passThrough});
    pendingConfigs_.push_back(PackConfig(config));
    return slot;
}

void HotkeyManager::FinalizeBindings() {
    assert(!finalized_ && "FinalizeBindings may only be called once");
    if (finalized_) return;

    slotCount_ = slots_.size();
    liveConfigs_ = std::make_unique<std::atomic<std::uint32_t>[]>(slotCount_);
    slotState_ = std::make_unique<SlotState[]>(slotCount_);
    for (SlotId slot = 0; slot < slotCount_; ++slot) {
        assert(liveConfigs_[slot].is_lock_free());
        liveConfigs_[slot].store(pendingConfigs_[slot], std::memory_order_relaxed);
    }
    pendingConfigs_.clear();
    pendingConfigs_.shrink_to_fit();
    finalized_ = true;
}

void HotkeyManager::UpdateHotkey(SlotId slot, const HotkeyConfig& config) noexcept {
    assert(finalized_ && "UpdateHotkey requires FinalizeBindings");
    assert(config.vk <= kVkMask && "Hotkey virtual key does not fit packed storage");
    if (!finalized_ || slot >= slotCount_ || config.vk > kVkMask) return;

    liveConfigs_[slot].store(PackConfig(config), std::memory_order_release);
}

HotkeyManager::MatchResult HotkeyManager::Match(const KeyEvent& event) noexcept {
    if (!finalized_) return {};

    const PhysicalModifierState before{
        modCtrlDown_, modShiftDown_, modAltDown_, modWinDown_};
    const bool otherKeyBefore = otherKeyPressed_;

    if (event.modifier != ModifierKey::None) {
        UpdateModifierState(event.modifier, event.type);
    } else if (event.type == KeyEventType::Down) {
        otherKeyPressed_ = true;
    }

    // A latch belongs to the immutable slot, not to its current packed config.
    // This preserves the paired UP when a settings reload rebinding occurs
    // between the matched DOWN and its UP.
    if (event.modifier == ModifierKey::None && event.type == KeyEventType::Up) {
        for (SlotId slot = 0; slot < slotCount_; ++slot) {
            auto& state = slotState_[slot];
            if (!state.comboKeyDown || state.latchedVk != event.vk) continue;

            const bool passedThrough = state.comboPassedThrough;
            state.comboKeyDown = false;
            state.comboPassedThrough = false;
            state.latchedVk = 0;
            return {.consume = !passedThrough};
        }
    }

    if (event.modifier == ModifierKey::None && event.type == KeyEventType::Down) {
        // Repeat suppression belongs to the in-flight latch, rather than the
        // current config. A rebind can change the live virtual key between a
        // matched DOWN and its repeated DOWN/UP events.
        for (SlotId slot = 0; slot < slotCount_; ++slot) {
            const auto& state = slotState_[slot];
            if (state.comboKeyDown && state.latchedVk == event.vk) {
                return {.consume = true};
            }
        }

        for (SlotId slot = 0; slot < slotCount_; ++slot) {
            const HotkeyConfig config = UnpackConfig(
                liveConfigs_[slot].load(std::memory_order_acquire));
            if (config.vk == 0 || config.vk != event.vk) continue;

            auto& state = slotState_[slot];
            if (state.comboKeyDown) return {.consume = true};
            if (!ModifiersMatch(config)) continue;

            state.comboKeyDown = true;
            state.latchedVk = event.vk;
            state.comboPassedThrough = slots_[slot].passThrough.AllowsEvent();
            if (state.comboPassedThrough) return {};

            QueueDeferredFire(slot);
            return {
                .consume = true,
                .cancelSystemMenu = config.alt || config.win,
            };
        }
    }

    MatchResult result;
    if (event.modifier != ModifierKey::None && event.type == KeyEventType::Up) {
        for (SlotId slot = 0; slot < slotCount_; ++slot) {
            const HotkeyConfig config = UnpackConfig(
                liveConfigs_[slot].load(std::memory_order_acquire));
            if (config.vk != 0 || !config.HasAny()) continue;
            if (!config.ModifiersMatch(before.ctrl, before.shift, before.alt, before.win)) continue;
            if (otherKeyBefore || slots_[slot].passThrough.AllowsEvent()) continue;

            QueueDeferredFire(slot);
            result.cancelSystemMenu = result.cancelSystemMenu || config.alt || config.win;
        }
    }
    return result;
}

void HotkeyManager::Dispatch(SlotId slot) {
    if (!finalized_ || slot >= slotCount_) return;
    if (const auto& callback = slots_[slot].callback) callback();
}

void HotkeyManager::ReconcileModifiers(const PhysicalModifierState& physical) noexcept {
    if (!physical.ctrl) modCtrlDown_ = false;
    if (!physical.shift) modShiftDown_ = false;
    if (!physical.alt) modAltDown_ = false;
    if (!physical.win) modWinDown_ = false;
    otherKeyPressed_ = false;
}

HotkeyManager::ModifierKey HotkeyManager::NormalizeModifier(std::uint32_t vk) noexcept {
    switch (vk) {
    case 0x11: case 0xA2: case 0xA3: return ModifierKey::Control;
    case 0x10: case 0xA0: case 0xA1: return ModifierKey::Shift;
    case 0x12: case 0xA4: case 0xA5: return ModifierKey::Alt;
    case 0x5B: case 0x5C: return ModifierKey::Win;
    default: return ModifierKey::None;
    }
}

std::uint32_t HotkeyManager::PackConfig(const HotkeyConfig& config) noexcept {
    std::uint32_t modifiers = 0;
    if (config.ctrl) modifiers |= kCtrlBit;
    if (config.shift) modifiers |= kShiftBit;
    if (config.alt) modifiers |= kAltBit;
    if (config.win) modifiers |= kWinBit;
    return (config.vk << 4u) | modifiers;
}

HotkeyConfig HotkeyManager::UnpackConfig(std::uint32_t packed) noexcept {
    return {
        .ctrl = (packed & kCtrlBit) != 0,
        .shift = (packed & kShiftBit) != 0,
        .alt = (packed & kAltBit) != 0,
        .win = (packed & kWinBit) != 0,
        .vk = packed >> 4u,
    };
}

bool HotkeyManager::ModifiersMatch(const HotkeyConfig& config) const noexcept {
    return config.ModifiersMatch(modCtrlDown_, modShiftDown_, modAltDown_, modWinDown_);
}

bool HotkeyManager::HasModifiers() const noexcept {
    return modCtrlDown_ || modShiftDown_ || modAltDown_ || modWinDown_;
}

void HotkeyManager::UpdateModifierState(ModifierKey modifier, KeyEventType type) noexcept {
    bool* state = nullptr;
    switch (modifier) {
    case ModifierKey::Control: state = &modCtrlDown_; break;
    case ModifierKey::Shift: state = &modShiftDown_; break;
    case ModifierKey::Alt: state = &modAltDown_; break;
    case ModifierKey::Win: state = &modWinDown_; break;
    case ModifierKey::None: return;
    }

    if (type == KeyEventType::Down) {
        if (!*state && !HasModifiers()) otherKeyPressed_ = false;
        *state = true;
    } else {
        *state = false;
    }
}

void HotkeyManager::QueueDeferredFire(SlotId slot) const noexcept {
    if (deferredFireSink_) deferredFireSink_(deferredFireContext_, slot);
}

}  // namespace NextKey
