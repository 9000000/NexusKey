// NexusKey - Key Handler Header
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "stdafx.h"

namespace NextKey {
namespace TSF {

/// Static key handling utilities
class KeyHandler {
public:
    /// Check if a virtual key is a printable character
    static bool IsPrintableKey(UINT vkCode);

    /// Check if a virtual key commits composition (space, enter, etc.)
    static bool IsCommitKey(UINT vkCode);

    /// Check if a virtual key cancels composition (escape, etc.)
    static bool IsCancelKey(UINT vkCode);

    /// Check if a virtual key is a navigation key (arrows, home, end, etc.)
    static bool IsNavigationKey(UINT vkCode);
};

}  // namespace TSF
}  // namespace NextKey
