// VKey — Hotkey label formatter. Linux-portable: VK codes are plain integers.
// Single source of truth for VK+mods → "Ctrl+Shift+F5" strings, shared between
// TrayIcon binding text, Sciter dialog C++ side, and the Classic Win32 UI.
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <cstdint>
#include <string>

namespace NextKey {

/// Render a hotkey combination as a human-readable wide string. Modifier
/// order is always Ctrl → Shift → Alt → Win, regardless of `mods` bit order.
///
/// Returns:
///   - "" when `vk == 0 && mods == 0` (nothing assigned).
///   - "Ctrl+Shift" etc. when `vk == 0` but modifiers are set
///     (modifier-only triggers — registry features, not convert-tool).
///   - "Ctrl+Shift+F5", "Alt+Z", "Space" etc. for the general case.
///
/// `vk` is interpreted with Win32 VK_* semantics:
///   - 0x30..0x39 → "0".."9"
///   - 0x41..0x5A → "A".."Z"
///   - 0x70..0x87 → "F1".."F24"
///   - 0x1B → "Esc"
///   - 0x20 → "Space"
///   - 0x08 → "Backspace"
///   - 0x09 → "Tab"
///   - 0x0D → "Enter"
///   - other → "VK 0x..." (hex fallback, so the user still sees something).
///
/// `mods` uses the `kMod*` bitmask from HotkeyRegistry.h
/// (`kModCtrl | kModShift | kModAlt | kModWin`).
[[nodiscard]] std::wstring FormatHotkeyLabel(uint32_t vk, uint32_t mods);

}  // namespace NextKey
