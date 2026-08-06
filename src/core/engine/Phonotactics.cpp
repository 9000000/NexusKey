// VKey - Vietnamese tone-placement rules
// SPDX-License-Identifier: GPL-3.0-only

#include "Phonotactics.h"

#include <array>
#include <cstdint>

#include "VietnameseTables.h"

namespace NextKey::Phonology {
namespace {

enum class VowelMod : uint8_t {
    None,
    Circumflex,
    Breve,
    Horn,
};

struct VowelInfo {
    wchar_t base;
    VowelMod mod;
};

[[nodiscard]] VowelInfo Decompose(wchar_t c) noexcept {
    switch (c) {
        case L'a':      return {L'a', VowelMod::None};
        case L'\x0103': return {L'a', VowelMod::Breve};
        case L'\x00E2': return {L'a', VowelMod::Circumflex};
        case L'e':      return {L'e', VowelMod::None};
        case L'\x00EA': return {L'e', VowelMod::Circumflex};
        case L'i':      return {L'i', VowelMod::None};
        case L'o':      return {L'o', VowelMod::None};
        case L'\x00F4': return {L'o', VowelMod::Circumflex};
        case L'\x01A1': return {L'o', VowelMod::Horn};
        case L'u':      return {L'u', VowelMod::None};
        case L'\x01B0': return {L'u', VowelMod::Horn};
        case L'y':      return {L'y', VowelMod::None};
        default:        return {0, VowelMod::None};
    }
}

}  // namespace

std::size_t FindTonePosition(std::wstring_view vowelSeq,
                             std::wstring_view coda,
                             bool modernOrtho) noexcept {
    // Vietnamese nuclei contain at most three vowels. The larger fixed buffer
    // keeps typo handling deterministic without allocating on the hook path.
    std::array<VowelInfo, 16> vowels{};
    std::size_t count = 0;
    for (wchar_t ch : vowelSeq) {
        if (count >= vowels.size()) break;
        const VowelInfo info = Decompose(ch);
        if (info.base != 0) vowels[count++] = info;
    }
    if (count == 0) return SIZE_MAX;

    // P1: the last horn vowel wins (ươ -> ơ).
    for (std::size_t i = count; i-- > 0;) {
        if (vowels[i].mod == VowelMod::Horn) return i;
    }

    // P2: the first non-horn modified vowel wins.
    for (std::size_t i = 0; i < count; ++i) {
        if (vowels[i].mod == VowelMod::Circumflex
            || vowels[i].mod == VowelMod::Breve) {
            return i;
        }
    }

    if (count >= 2) {
        if (modernOrtho && count >= 3
            && NextKey::IsTriphthong(vowels[count - 3].base,
                                     vowels[count - 2].base,
                                     vowels[count - 1].base)) {
            return count - 2;
        }

        if (count == 3
            && vowels[0].mod == VowelMod::None
            && vowels[1].mod == VowelMod::None
            && vowels[2].mod == VowelMod::None
            && NextKey::IsBareSmartTriphthongTail(
                vowels[0].base, vowels[1].base, vowels[2].base)) {
            return count - 1;
        }

        std::size_t firstPos = count - 2;
        std::size_t lastPos = count - 1;
        int firstDiphIndex = NextKey::DiphthongVowelIndex(vowels[firstPos].base);
        int lastDiphIndex = NextKey::DiphthongVowelIndex(vowels[lastPos].base);
        bool shifted = false;

        // Prefer the first two vowels of a three-vowel cluster when they form a
        // known diphthong. This also preserves repeated-vowel typo behavior.
        if (count >= 3) {
            const int shiftFirstIndex =
                NextKey::DiphthongVowelIndex(vowels[count - 3].base);
            if (shiftFirstIndex >= 0 && firstDiphIndex >= 0) {
                const uint8_t shiftRule = modernOrtho
                    ? NextKey::kDiphthongModern[shiftFirstIndex][firstDiphIndex]
                    : NextKey::kDiphthongClassic[shiftFirstIndex][firstDiphIndex];
                if (shiftRule != 0) {
                    lastDiphIndex = firstDiphIndex;
                    firstDiphIndex = shiftFirstIndex;
                    firstPos = count - 3;
                    lastPos = count - 2;
                    shifted = true;
                }
            }
        }

        if (firstDiphIndex >= 0 && lastDiphIndex >= 0) {
            uint8_t rule = modernOrtho
                ? NextKey::kDiphthongModern[firstDiphIndex][lastDiphIndex]
                : NextKey::kDiphthongClassic[firstDiphIndex][lastDiphIndex];
            if (rule == 3) {
                if (shifted && vowels[count - 1].base == vowels[count - 2].base) {
                    rule = 1;
                } else {
                    rule = (lastPos + 1 < count) || !coda.empty() ? 2 : 1;
                }
            }
            if (rule == 1) return firstPos;
            if (rule == 2) return lastPos;
        }
    }

    return count - 1;
}

}  // namespace NextKey::Phonology
