// VKey - portable legacy RegisterHotKey registration planning
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "core/config/TypingConfig.h"

#include <vector>

namespace NextKey {

/// Expand one configured hotkey into deterministic chords accepted by the
/// legacy Win32 RegisterHotKey fallback. The returned VK codes are plain
/// integers so this planning seam stays portable and free of Windows headers.
/// A non-zero configured VK produces one unchanged chord. For a modifier-only
/// config, each required modifier becomes the main VK once while its flag is
/// cleared from that alternative, in Ctrl/Shift/Alt/Win order.
[[nodiscard]] std::vector<HotkeyConfig> BuildRegisterHotKeyPlan(
    const HotkeyConfig& config);

}  // namespace NextKey
