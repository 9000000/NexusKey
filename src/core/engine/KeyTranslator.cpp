// NexusKey - Key Translator Implementation
// SPDX-License-Identifier: GPL-3.0-only

#include "KeyTranslator.h"

#ifdef _WIN32
#include <Windows.h>
#endif

namespace NextKey {

KeyTranslator::KeyTranslator() = default;
KeyTranslator::~KeyTranslator() = default;

wchar_t KeyTranslator::VirtualKeyToChar(uint32_t vkCode, bool shiftPressed) const {
#ifdef _WIN32
    // Direct VK to ASCII mapping for A-Z (keyboard layout independent)
    // This ensures Telex input works regardless of active keyboard layout
    if (vkCode >= 0x41 && vkCode <= 0x5A) {
        // VK_A (0x41) = 65, 'a' = 97, 'A' = 65
        wchar_t base = static_cast<wchar_t>(vkCode);  // Already uppercase ASCII
        return shiftPressed ? base : (base + 32);     // +32 for lowercase
    }

    // For other keys (numbers, punctuation), use ToUnicode
    BYTE keyState[256] = {0};
    if (shiftPressed) {
        keyState[VK_SHIFT] = 0x80;
    }

    wchar_t buffer[4] = {0};
    int result = ToUnicode(vkCode, 0, keyState, buffer, 4, 0);

    if (result > 0) {
        return buffer[0];
    }
#else
    (void)vkCode;
    (void)shiftPressed;
#endif
    return 0;
}

bool KeyTranslator::IsCharacterKey(uint32_t vkCode) const {
#ifdef _WIN32
    // A-Z keys
    if (vkCode >= 0x41 && vkCode <= 0x5A) return true;
    // 0-9 keys
    if (vkCode >= 0x30 && vkCode <= 0x39) return true;
    // Numpad 0-9
    if (vkCode >= VK_NUMPAD0 && vkCode <= VK_NUMPAD9) return true;
    // OEM keys (punctuation, etc.)
    if (vkCode >= VK_OEM_1 && vkCode <= VK_OEM_8) return true;
#else
    (void)vkCode;
#endif
    return false;
}

bool KeyTranslator::IsModifierKey(uint32_t vkCode) const {
#ifdef _WIN32
    return vkCode == VK_SHIFT || vkCode == VK_LSHIFT || vkCode == VK_RSHIFT ||
           vkCode == VK_CONTROL || vkCode == VK_LCONTROL || vkCode == VK_RCONTROL ||
           vkCode == VK_MENU || vkCode == VK_LMENU || vkCode == VK_RMENU ||
           vkCode == VK_LWIN || vkCode == VK_RWIN;
#else
    (void)vkCode;
    return false;
#endif
}

}  // namespace NextKey
