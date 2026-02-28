// NexusKey - Shared Vietnamese Lookup Tables
// SPDX-License-Identifier: GPL-3.0-only
//
// Flat constexpr arrays for O(1) composition lookups.
// Shared between TelexEngine and VniEngine.

#pragma once

#include <cwchar>
#include <cwctype>

namespace NextKey {

//=============================================================================
// Modifier Lookup: base vowel + modifier → modified vowel
// Index: VowelBaseIndex(base) × 3 + ModifierIndex(mod)
// base: a=0, e=1, o=2, u=3
// mod:  Circumflex=0, Breve=1, Horn=2
//=============================================================================

constexpr wchar_t kModifiedVowel[4][3] = {
    /* a */ { L'\x00E2', L'\x0103', 0         },  // â, ă, (N/A)
    /* e */ { L'\x00EA', 0,         0         },  // ê
    /* o */ { L'\x00F4', 0,         L'\x01A1' },  // ô, (N/A), ơ
    /* u */ { 0,         0,         L'\x01B0' },  // (N/A), (N/A), ư
};

/// Map base vowel to index: a→0, e→1, o→2, u→3, else -1
constexpr int VowelBaseIndex(wchar_t base) {
    switch (base) {
        case L'a': return 0;
        case L'e': return 1;
        case L'o': return 2;
        case L'u': return 3;
        default:   return -1;
    }
}

//=============================================================================
// Tone Lookup: (possibly-modified) vowel + tone → toned vowel
// Index: ToneBaseIndex(ch) × 5 + ToneSlot(tone)
// tone: Acute=0, Grave=1, Hook=2, Tilde=3, Dot=4
//=============================================================================

// 12 base vowels × 5 tones
// Row order: a=0, â=1, ă=2, e=3, ê=4, i=5, o=6, ô=7, ơ=8, u=9, ư=10, y=11
constexpr wchar_t kTonedVowel[12][5] = {
    /* a  */ { L'\x00E1', L'\x00E0', L'\x1EA3', L'\x00E3', L'\x1EA1' },  // á à ả ã ạ
    /* â  */ { L'\x1EA5', L'\x1EA7', L'\x1EA9', L'\x1EAB', L'\x1EAD' },  // ấ ầ ẩ ẫ ậ
    /* ă  */ { L'\x1EAF', L'\x1EB1', L'\x1EB3', L'\x1EB5', L'\x1EB7' },  // ắ ằ ẳ ẵ ặ
    /* e  */ { L'\x00E9', L'\x00E8', L'\x1EBB', L'\x1EBD', L'\x1EB9' },  // é è ẻ ẽ ẹ
    /* ê  */ { L'\x1EBF', L'\x1EC1', L'\x1EC3', L'\x1EC5', L'\x1EC7' },  // ế ề ể ễ ệ
    /* i  */ { L'\x00ED', L'\x00EC', L'\x1EC9', L'\x0129', L'\x1ECB' },  // í ì ỉ ĩ ị
    /* o  */ { L'\x00F3', L'\x00F2', L'\x1ECF', L'\x00F5', L'\x1ECD' },  // ó ò ỏ õ ọ
    /* ô  */ { L'\x1ED1', L'\x1ED3', L'\x1ED5', L'\x1ED7', L'\x1ED9' },  // ố ồ ổ ỗ ộ
    /* ơ  */ { L'\x1EDB', L'\x1EDD', L'\x1EDF', L'\x1EE1', L'\x1EE3' },  // ớ ờ ở ỡ ợ
    /* u  */ { L'\x00FA', L'\x00F9', L'\x1EE7', L'\x0169', L'\x1EE5' },  // ú ù ủ ũ ụ
    /* ư  */ { L'\x1EE9', L'\x1EEB', L'\x1EED', L'\x1EEF', L'\x1EF1' },  // ứ ừ ử ữ ự
    /* y  */ { L'\x00FD', L'\x1EF3', L'\x1EF7', L'\x1EF9', L'\x1EF5' },  // ý ỳ ỷ ỹ ỵ
};

/// Map (possibly modified) vowel to tone table row index
constexpr int ToneBaseIndex(wchar_t ch) {
    switch (ch) {
        case L'a':         return 0;
        case L'\x00E2':    return 1;   // â
        case L'\x0103':    return 2;   // ă
        case L'e':         return 3;
        case L'\x00EA':    return 4;   // ê
        case L'i':         return 5;
        case L'o':         return 6;
        case L'\x00F4':    return 7;   // ô
        case L'\x01A1':    return 8;   // ơ
        case L'u':         return 9;
        case L'\x01B0':    return 10;  // ư
        case L'y':         return 11;
        default:           return -1;
    }
}

//=============================================================================
// Vietnamese-aware uppercase conversion (shared)
//=============================================================================

inline wchar_t ToUpperVietnamese(wchar_t ch) {
    // Latin-1 Supplement
    if (ch >= 0x00E0 && ch <= 0x00F6) return ch - 0x20;
    if (ch >= 0x00F8 && ch <= 0x00FE) return ch - 0x20;

    // Latin Extended-A
    if (ch == L'\x0103') return L'\x0102';  // ă → Ă
    if (ch == L'\x0111') return L'\x0110';  // đ → Đ

    // Latin Extended-B
    if (ch == L'\x01A1') return L'\x01A0';  // ơ → Ơ
    if (ch == L'\x01B0') return L'\x01AF';  // ư → Ư

    // Vietnamese toned vowels (Latin Extended Additional: 0x1EA0–0x1EF9)
    if (ch >= 0x1EA0 && ch <= 0x1EF9) {
        if (ch & 1) return ch - 1;  // Odd (lowercase) → Even (uppercase)
        return ch;
    }

    return towupper(ch);
}

}  // namespace NextKey
