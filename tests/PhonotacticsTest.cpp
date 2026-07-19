// VKey - Tone-placement unit tests
// SPDX-License-Identifier: AGPL-3.0-only

#include <gtest/gtest.h>

#include "core/engine/Phonotactics.h"

namespace NextKey::Phonology {
namespace {

class PhonotacticsTonePosition : public ::testing::Test {
protected:
    static constexpr bool kClassic = false;
    static constexpr bool kModern = true;
};

TEST_F(PhonotacticsTonePosition, EmptyVowelReturnsSizeMax) {
    EXPECT_EQ(FindTonePosition(L"", L"", kClassic), SIZE_MAX);
}

TEST_F(PhonotacticsTonePosition, SingleVowelReturnsZero) {
    EXPECT_EQ(FindTonePosition(L"a", L"", kClassic), 0u);
    EXPECT_EQ(FindTonePosition(L"e", L"", kClassic), 0u);
    EXPECT_EQ(FindTonePosition(L"i", L"", kClassic), 0u);
    EXPECT_EQ(FindTonePosition(L"o", L"", kClassic), 0u);
    EXPECT_EQ(FindTonePosition(L"u", L"", kClassic), 0u);
    EXPECT_EQ(FindTonePosition(L"y", L"", kClassic), 0u);
}

TEST_F(PhonotacticsTonePosition, SingleHornVowelReturnsZero) {
    EXPECT_EQ(FindTonePosition(L"ư", L"", kClassic), 0u);
    EXPECT_EQ(FindTonePosition(L"ơ", L"", kClassic), 0u);
}

TEST_F(PhonotacticsTonePosition, DiphthongAiToneOnFirst) {
    EXPECT_EQ(FindTonePosition(L"ai", L"", kClassic), 0u);
    EXPECT_EQ(FindTonePosition(L"ai", L"", kModern), 0u);
}

TEST_F(PhonotacticsTonePosition, DiphthongAoToneOnFirst) {
    EXPECT_EQ(FindTonePosition(L"ao", L"", kClassic), 0u);
}

TEST_F(PhonotacticsTonePosition, DiphthongOaClassicNoCodaToneOnFirst) {
    EXPECT_EQ(FindTonePosition(L"oa", L"", kClassic), 0u);
}

TEST_F(PhonotacticsTonePosition, DiphthongOaModernNoCodaToneOnSecond) {
    EXPECT_EQ(FindTonePosition(L"oa", L"", kModern), 1u);
}

TEST_F(PhonotacticsTonePosition, DiphthongOaClassicWithCodaToneOnSecond) {
    EXPECT_EQ(FindTonePosition(L"oa", L"n", kClassic), 1u);
}

TEST_F(PhonotacticsTonePosition, HornDiphthongUOToneOnHorn) {
    EXPECT_EQ(FindTonePosition(L"ươ", L"", kClassic), 1u);
    EXPECT_EQ(FindTonePosition(L"ươ", L"", kModern), 1u);
}

TEST_F(PhonotacticsTonePosition, ModifiedVowelGetsTonePriority) {
    EXPECT_EQ(FindTonePosition(L"iê", L"", kClassic), 1u);
    EXPECT_EQ(FindTonePosition(L"âu", L"", kClassic), 0u);
}

TEST_F(PhonotacticsTonePosition, TriphthongOaiToneOnMiddle) {
    EXPECT_EQ(FindTonePosition(L"oai", L"", kModern), 1u);
}

TEST_F(PhonotacticsTonePosition, TriphthongUyuToneOnMiddle) {
    EXPECT_EQ(FindTonePosition(L"uyu", L"", kModern), 1u);
}

TEST_F(PhonotacticsTonePosition, ThreeVowelUyeToneOnModifiedThird) {
    EXPECT_EQ(FindTonePosition(L"uyê", L"", kModern), 2u);
}

TEST_F(PhonotacticsTonePosition, ShiftedThreeVowelTypoAoi) {
    EXPECT_EQ(FindTonePosition(L"aoi", L"", kClassic), 0u);
}

TEST_F(PhonotacticsTonePosition, ShiftedThreeVowelRepeatHoaa) {
    EXPECT_EQ(FindTonePosition(L"oaa", L"", kClassic), 0u);
}

TEST_F(PhonotacticsTonePosition, ClassicOaiHasRemainderRule) {
    EXPECT_EQ(FindTonePosition(L"oai", L"", kClassic), 1u);
}

}  // namespace
}  // namespace NextKey::Phonology
