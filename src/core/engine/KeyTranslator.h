// NexusKey - Key Translator Header
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <cstdint>

namespace NextKey {

/// Translates virtual key codes to characters
class KeyTranslator {
public:
    KeyTranslator();
    ~KeyTranslator();

    /// Translate a virtual key code to a wide character
    /// Returns 0 if no translation available
    wchar_t VirtualKeyToChar(uint32_t vkCode, bool shiftPressed) const;

    /// Check if a virtual key is a character key (A-Z, 0-9, etc.)
    bool IsCharacterKey(uint32_t vkCode) const;

    /// Check if a virtual key is a modifier (Shift, Ctrl, Alt)
    bool IsModifierKey(uint32_t vkCode) const;
};

}  // namespace NextKey
