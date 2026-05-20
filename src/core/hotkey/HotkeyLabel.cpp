// VKey — Hotkey label formatter implementation.
// SPDX-License-Identifier: GPL-3.0-only

#include "core/hotkey/HotkeyLabel.h"

#include "core/hotkey/HotkeyRegistry.h"

namespace NextKey {

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

}  // namespace NextKey
