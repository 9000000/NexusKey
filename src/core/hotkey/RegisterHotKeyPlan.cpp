// VKey - portable legacy RegisterHotKey registration planning
// SPDX-License-Identifier: GPL-3.0-only

#include "core/hotkey/RegisterHotKeyPlan.h"

namespace NextKey {

namespace {

// Win32 VK constants reproduced here so this planner remains Linux-portable.
constexpr std::uint32_t kVkControl = 0x11;
constexpr std::uint32_t kVkShift = 0x10;
constexpr std::uint32_t kVkMenu = 0x12;
constexpr std::uint32_t kVkLwin = 0x5B;

}  // namespace

std::vector<HotkeyConfig> BuildRegisterHotKeyPlan(const HotkeyConfig& config) {
    if (!config.HasAny()) return {};
    if (config.vk != 0) return {config};

    std::vector<HotkeyConfig> plan;
    plan.reserve(4);

    if (config.ctrl) {
        auto chord = config;
        chord.ctrl = false;
        chord.vk = kVkControl;
        plan.push_back(chord);
    }
    if (config.shift) {
        auto chord = config;
        chord.shift = false;
        chord.vk = kVkShift;
        plan.push_back(chord);
    }
    if (config.alt) {
        auto chord = config;
        chord.alt = false;
        chord.vk = kVkMenu;
        plan.push_back(chord);
    }
    if (config.win) {
        auto chord = config;
        chord.win = false;
        chord.vk = kVkLwin;
        plan.push_back(chord);
    }
    return plan;
}

}  // namespace NextKey
