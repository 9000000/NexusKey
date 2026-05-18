// VKey — Unified hotkey registry implementation.
// SPDX-License-Identifier: GPL-3.0-only

#include "core/hotkey/HotkeyRegistry.h"

#include <algorithm>
#include <string>
#include <string_view>

#include <toml.hpp>

namespace NextKey {

namespace {

// Win32 VK constants reproduced here so this file stays Linux-portable.
constexpr uint32_t kVkShift   = 0x10;
constexpr uint32_t kVkControl = 0x11;
constexpr uint32_t kVkMenu    = 0x12;  // Alt
constexpr uint32_t kVkLwin    = 0x5B;
constexpr uint32_t kVkRwin    = 0x5C;
constexpr uint32_t kVkEscape  = 0x1B;

[[nodiscard]] std::string_view IntentToString(Intent intent) noexcept {
    switch (intent) {
    case Intent::CancelComposition: return "cancel-composition";
    case Intent::SkipMacro:         return "skip-macro";
    case Intent::ToggleEnabled:     return "toggle-enabled";
    }
    return "";
}

[[nodiscard]] bool ParseIntent(std::string_view s, Intent& out) noexcept {
    if (s == "cancel-composition") { out = Intent::CancelComposition; return true; }
    if (s == "skip-macro")         { out = Intent::SkipMacro;         return true; }
    if (s == "toggle-enabled")     { out = Intent::ToggleEnabled;     return true; }
    return false;
}

}  // namespace

bool IsModifierKey(uint32_t vk) noexcept {
    return vk == kVkShift || vk == kVkControl || vk == kVkMenu
        || vk == kVkLwin  || vk == kVkRwin;
}

bool HotkeyRegistry::Matches(Intent   intent,
                           uint32_t vk,
                           uint32_t mods,
                           bool     isDoubleTap,
                           bool     keyUp) const noexcept {
    // Contract: caller frames each event. Tap/Chord callers ask on DOWN
    // (keyUp=false, isDoubleTap=false). Modifier-alone callers ask only after
    // verifying a "clean" modifier up (keyUp=true, isDoubleTap=false).
    // Double-tap callers ask after detecting the 2nd tap within the timing
    // window (isDoubleTap=true; existing NexusKey UX fires on the 2nd release,
    // so keyUp=true in practice — but Matches() doesn't require it).
    const auto it = triggers_.find(intent);
    if (it == triggers_.end()) return false;

    for (const Trigger& t : it->second) {
        if (t.vk != vk) continue;

        if (t.doubleTap) {
            // Double-tap fires only on caller's explicit isDoubleTap signal.
            if (isDoubleTap && mods == t.mods) return true;
            continue;
        }

        if (IsModifierKey(t.vk) && t.mods == 0) {
            // Implicit modifier-alone fires on UP (caller verified the window
            // was clean — no other key pressed). Must not collide with a
            // double-tap signal for the same vk.
            if (keyUp && !isDoubleTap) return true;
            continue;
        }

        // Plain tap or chord: fire on DOWN with exact mods match.
        if (!keyUp && !isDoubleTap && mods == t.mods) return true;
    }
    return false;
}

HotkeyRegistry HotkeyRegistry::Defaults() {
    HotkeyRegistry cfg;
    cfg.AddTrigger(Intent::CancelComposition, Trigger{kVkEscape, 0, false});
    cfg.AddTrigger(Intent::SkipMacro,         Trigger{kVkEscape, 0, false});
    cfg.AddTrigger(Intent::ToggleEnabled,     Trigger{kVkControl, 0, false});  // implicit modifier-alone
    cfg.AddTrigger(Intent::ToggleEnabled,     Trigger{kVkMenu,    0, true});   // 2×Alt
    return cfg;
}

HotkeyRegistry HotkeyRegistry::FromLegacyFields(
    bool    escRestoreRawEnabled,
    bool    tempOffMacroByEsc,
    uint8_t tempOffMethodValue) noexcept {
    HotkeyRegistry cfg;
    if (escRestoreRawEnabled) {
        cfg.AddTrigger(Intent::CancelComposition, Trigger{kVkEscape, 0, false});
    }
    if (tempOffMacroByEsc) {
        cfg.AddTrigger(Intent::SkipMacro, Trigger{kVkEscape, 0, false});
    }
    // tempOffMethod: 0=None, 1=DupAlt, 2=Ctrl (must match TempOffMethod enum
    // in core/config/TypingConfig.h)
    switch (tempOffMethodValue) {
    case 1:  // DupAlt
        cfg.AddTrigger(Intent::ToggleEnabled, Trigger{kVkMenu, 0, /*doubleTap=*/true});
        break;
    case 2:  // Ctrl
        cfg.AddTrigger(Intent::ToggleEnabled, Trigger{kVkControl, 0, /*doubleTap=*/false});
        break;
    case 0:  // None — no toggle binding
    default:
        break;
    }
    return cfg;
}

const std::vector<Trigger>& HotkeyRegistry::TriggersFor(Intent intent) const noexcept {
    static const std::vector<Trigger> kEmpty;
    const auto it = triggers_.find(intent);
    return it == triggers_.end() ? kEmpty : it->second;
}

void HotkeyRegistry::AddTrigger(Intent intent, Trigger trigger) {
    triggers_[intent].push_back(trigger);
}

void HotkeyRegistry::Clear() noexcept {
    triggers_.clear();
}

void HotkeyRegistry::Load(const toml::array& cfg) {
    triggers_.clear();
    for (const auto& node : cfg) {
        const toml::table* row = node.as_table();
        if (!row) continue;

        const auto intentNode = row->get("intent");
        const auto triggerNode = row->get("trigger");
        if (!intentNode || !triggerNode) continue;

        const auto* intentStr = intentNode->as_string();
        const auto* triggerTbl = triggerNode->as_table();
        if (!intentStr || !triggerTbl) continue;

        Intent intent;
        if (!ParseIntent(intentStr->get(), intent)) continue;  // unknown intent — skip

        Trigger t;
        if (const auto* vk = triggerTbl->get_as<int64_t>("vk")) {
            t.vk = static_cast<uint32_t>(vk->get());
        } else {
            continue;  // vk required
        }
        if (t.vk == 0) continue;  // reject vk=0

        if (const auto* mods = triggerTbl->get_as<int64_t>("mods")) {
            t.mods = static_cast<uint32_t>(mods->get());
        }
        if (const auto* dt = triggerTbl->get_as<bool>("double_tap")) {
            t.doubleTap = dt->get();
        }

        AddTrigger(intent, t);
    }
}

void HotkeyRegistry::Save(toml::array& cfg) const {
    for (Intent intent : kAllIntents) {
        const auto it = triggers_.find(intent);
        if (it == triggers_.end()) continue;
        for (const Trigger& t : it->second) {
            toml::table row;
            row.insert("intent", std::string(IntentToString(intent)));
            toml::table trig;
            trig.insert("vk",   static_cast<int64_t>(t.vk));
            trig.insert("mods", static_cast<int64_t>(t.mods));
            if (t.doubleTap) {
                trig.insert("double_tap", true);
            }
            row.insert("trigger", std::move(trig));
            cfg.push_back(std::move(row));
        }
    }
}

}  // namespace NextKey
