// VKey — Hotkey label formatter implementation.
// SPDX-License-Identifier: GPL-3.0-only

#include "core/hotkey/HotkeyLabel.h"

#include <cwctype>

#include "core/config/TypingConfig.h"  // HotkeyConfig::ToMods bit values
#include "core/hotkey/HotkeyRegistry.h"

namespace NextKey {

// Drift guard — TypingConfig.h hardcodes 0x01/0x02/0x04/0x08 for the
// modifier bitmask to avoid pulling HotkeyRegistry.h into every config
// consumer. This assert wires the two headers together so any divergence
// breaks the build instead of silently mismapping flags at runtime.
static_assert(kModCtrl  == 0x01u, "HotkeyConfig::ToMods Ctrl bit drift");
static_assert(kModShift == 0x02u, "HotkeyConfig::ToMods Shift bit drift");
static_assert(kModAlt   == 0x04u, "HotkeyConfig::ToMods Alt bit drift");
static_assert(kModWin   == 0x08u, "HotkeyConfig::ToMods Win bit drift");

namespace {

[[nodiscard]] std::wstring VkToKeyName(uint32_t vk) {
    if (vk == 0) return L"";

    if (vk >= 0x30 && vk <= 0x39) {                  // '0'..'9'
        return std::wstring(1, static_cast<wchar_t>(vk));
    }
    if (vk >= 0x41 && vk <= 0x5A) {                  // 'A'..'Z'
        return std::wstring(1, static_cast<wchar_t>(vk));
    }
    if (vk >= 0x70 && vk <= 0x87) {                  // F1..F24
        std::wstring s = L"F";
        s += std::to_wstring(vk - 0x6F);
        return s;
    }
    switch (vk) {
    // Modifier keys — named so chord labels render as "Ctrl+Shift"
    // (modifier-combo capture) instead of the hex fallback "VK 0x0010".
    case 0x10: return L"Shift";
    case 0x11: return L"Ctrl";
    case 0x12: return L"Alt";
    case 0x5B: case 0x5C: return L"Win";
    // Common navigation / control keys
    case 0x08: return L"Backspace";
    case 0x09: return L"Tab";
    case 0x0D: return L"Enter";
    case 0x1B: return L"Esc";
    case 0x20: return L"Space";
    case 0x25: return L"Left";
    case 0x26: return L"Up";
    case 0x27: return L"Right";
    case 0x28: return L"Down";
    case 0x2D: return L"Insert";
    case 0x2E: return L"Delete";
    case 0x24: return L"Home";
    case 0x23: return L"End";
    case 0x21: return L"PgUp";
    case 0x22: return L"PgDn";
    default: break;
    }

    // Fallback so users see *something* instead of a blank label.
    static constexpr wchar_t kHex[] = L"0123456789ABCDEF";
    std::wstring s = L"VK 0x";
    s += kHex[(vk >> 12) & 0xF];
    s += kHex[(vk >> 8)  & 0xF];
    s += kHex[(vk >> 4)  & 0xF];
    s += kHex[(vk >> 0)  & 0xF];
    return s;
}

void AppendWithPlus(std::wstring& out, std::wstring_view token) {
    if (!out.empty()) out.push_back(L'+');
    out.append(token);
}

}  // namespace

std::wstring FormatHotkeyLabel(uint32_t vk, uint32_t mods) {
    std::wstring out;
    out.reserve(24);

    if (mods & kModCtrl)  AppendWithPlus(out, L"Ctrl");
    if (mods & kModShift) AppendWithPlus(out, L"Shift");
    if (mods & kModAlt)   AppendWithPlus(out, L"Alt");
    if (mods & kModWin)   AppendWithPlus(out, L"Win");

    if (vk != 0) {
        AppendWithPlus(out, VkToKeyName(vk));
    }
    return out;
}

uint32_t LegacyKeyCharToVk(const std::wstring& s) noexcept {
    if (s.size() != 1) return 0;
    wchar_t c = static_cast<wchar_t>(std::towupper(s[0]));
    // A-Z and 0-9 share code points with VK_A..VK_Z (0x41..0x5A)
    // and VK_0..VK_9 (0x30..0x39). Everything else → 0 (user reassigns).
    if ((c >= L'A' && c <= L'Z') || (c >= L'0' && c <= L'9')) {
        return static_cast<uint32_t>(c);
    }
    return 0;
}

}  // namespace NextKey
