// NexusKey - Shared Vietnamese Lookup Tables
// SPDX-License-Identifier: GPL-3.0-only
//
// Flat constexpr arrays for O(1) composition lookups.
// Shared between TelexEngine and VniEngine.

#pragma once

#include <cwchar>
#include <cwctype>
#include <iterator>

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
// Diphthong Tone Placement — shared between Telex and VNI engines
// Table[first_vowel][second_vowel] → 0=no rule, 1=FIRST, 2=SECOND
// Vowel index: a=0, e=1, i=2, o=3, u=4, y=5
//=============================================================================

/// Map base vowel to diphthong table index
constexpr int DiphthongVowelIndex(wchar_t base) {
    switch (base) {
        case L'a': return 0;
        case L'e': return 1;
        case L'i': return 2;
        case L'o': return 3;
        case L'u': return 4;
        case L'y': return 5;
        default:   return -1;
    }
}

/// Classic placement: oa, oe → coda-aware (hòa without coda, hoàn with coda)
/// Rule: 0=none, 1=FIRST, 2=SECOND, 3=FIRST-without-coda/SECOND-with-coda
constexpr uint8_t kDiphthongClassic[6][6] = {
    //          a  e  i  o  u  y
    /* a */ {   0, 0, 1, 1, 1, 1 },  // ai, ao, au, ay
    /* e */ {   0, 0, 1, 1, 1, 0 },  // ei, eo, eu
    /* i */ {   1, 0, 1, 0, 1, 0 },  // ia, ii, iu
    /* o */ {   3, 3, 1, 0, 1, 0 },  // oa=CODA_AWARE, oe=CODA_AWARE, oi, ou
    /* u */ {   1, 1, 1, 0, 1, 2 },  // ua, ue, ui, uu, uy=SECOND
    /* y */ {   0, 0, 0, 0, 3, 0 },  // yu=CODA_AWARE: yủn(coda→u), khuỷu(no coda→y)
};

/// Modern placement: oa, oe, ue → tone on SECOND (new-style: hóa, xoé, thuế)
constexpr uint8_t kDiphthongModern[6][6] = {
    //          a  e  i  o  u  y
    /* a */ {   0, 0, 1, 1, 1, 1 },  // (same as classic)
    /* e */ {   0, 0, 1, 1, 1, 0 },  // (same as classic)
    /* i */ {   1, 0, 1, 0, 1, 0 },  // (same as classic)
    /* o */ {   2, 2, 1, 0, 1, 0 },  // oa=SECOND, oe=SECOND (modern)
    /* u */ {   1, 2, 1, 2, 1, 2 },  // ue=SECOND, uo=SECOND, uy=SECOND (modern)
    /* y */ {   0, 0, 0, 0, 3, 0 },  // yu=CODA_AWARE: yủn(coda→u), khuỷu(no coda→y)
};

/// Triphthong patterns (Modern only): tone on MIDDLE vowel
struct TriphthongPattern { wchar_t v1, v2, v3; };
constexpr TriphthongPattern kTriphthongs[] = {
    {L'o', L'a', L'i'},   // oai
    {L'o', L'e', L'o'},   // oeo
    {L'u', L'y', L'a'},   // uya
    {L'u', L'y', L'u'},   // uyu
    {L'o', L'a', L'o'},   // oao
    {L'o', L'a', L'y'},   // oay
};
constexpr size_t kTriphthongCount = std::size(kTriphthongs);

/// Check if three vowel bases form a triphthong
[[nodiscard]] constexpr bool IsTriphthong(wchar_t v1, wchar_t v2, wchar_t v3) noexcept {
    // Flat comparison is faster than loop, no branch misprediction
    return (v1 == L'o' && v2 == L'a' && v3 == L'i') ||  // oai
           (v1 == L'o' && v2 == L'e' && v3 == L'o') ||  // oeo
           (v1 == L'u' && v2 == L'y' && v3 == L'a') ||  // uya
           (v1 == L'u' && v2 == L'y' && v3 == L'u') ||  // uyu
           (v1 == L'o' && v2 == L'a' && v3 == L'o') ||  // oao
           (v1 == L'o' && v2 == L'a' && v3 == L'y');    // oay
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

//=============================================================================
// Vietnamese-aware lowercase conversion (shared)
//=============================================================================

inline wchar_t ToLowerVietnamese(wchar_t ch) {
    // Latin-1 Supplement
    if (ch >= 0x00C0 && ch <= 0x00D6) return ch + 0x20;
    if (ch >= 0x00D8 && ch <= 0x00DE) return ch + 0x20;

    // Latin Extended-A
    if (ch == L'\x0102') return L'\x0103';  // Ă → ă
    if (ch == L'\x0110') return L'\x0111';  // Đ → đ

    // Latin Extended-B
    if (ch == L'\x01A0') return L'\x01A1';  // Ơ → ơ
    if (ch == L'\x01AF') return L'\x01B0';  // Ư → ư

    // Vietnamese toned vowels (Latin Extended Additional: 0x1EA0–0x1EF9)
    if (ch >= 0x1EA0 && ch <= 0x1EF9) {
        if (!(ch & 1)) return ch + 1;  // Even (uppercase) → Odd (lowercase)
    }

    return towlower(ch);
}

}  // namespace NextKey
