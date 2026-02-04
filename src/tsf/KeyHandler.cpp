// NexusKey - Key Handler Implementation
// SPDX-License-Identifier: GPL-3.0-only

#include "stdafx.h"
#include "KeyHandler.h"

namespace NextKey {
namespace TSF {

bool KeyHandler::IsPrintableKey(UINT vkCode) {
    // A-Z
    if (vkCode >= 0x41 && vkCode <= 0x5A) return true;
    // 0-9
    if (vkCode >= 0x30 && vkCode <= 0x39) return true;
    // Numpad 0-9
    if (vkCode >= VK_NUMPAD0 && vkCode <= VK_NUMPAD9) return true;
    // Common OEM keys
    if (vkCode >= VK_OEM_1 && vkCode <= VK_OEM_8) return true;

    return false;
}

bool KeyHandler::IsCommitKey(UINT vkCode) {
    return vkCode == VK_SPACE || 
           vkCode == VK_RETURN ||
           vkCode == VK_TAB;
}

bool KeyHandler::IsCancelKey(UINT vkCode) {
    return vkCode == VK_ESCAPE;
}

bool KeyHandler::IsNavigationKey(UINT vkCode) {
    return vkCode == VK_LEFT ||
           vkCode == VK_RIGHT ||
           vkCode == VK_UP ||
           vkCode == VK_DOWN ||
           vkCode == VK_HOME ||
           vkCode == VK_END ||
           vkCode == VK_PRIOR ||  // Page Up
           vkCode == VK_NEXT;     // Page Down
}

}  // namespace TSF
}  // namespace NextKey
