// EditDistanceTest.cpp -- Levenshtein over u16string_view + ErrorPct helper.
//
// Used for sustained-typing corpus verdict (verdict_mode = "edit_distance").
// Pure CPU, no Win32 deps -- runs on Linux test target.

#include <gtest/gtest.h>

#include "EditDistance.h"

namespace NextKey::TestRunner::Test {

using NextKey::TestRunner::EditDistance::Levenshtein;
using NextKey::TestRunner::EditDistance::ErrorPct;

// --- Levenshtein basic cases ----------------------------------------------

TEST(EditDistance, BothEmptyIsZero) {
    EXPECT_EQ(Levenshtein(u"", u""), 0u);
}

TEST(EditDistance, OneEmptyIsLengthOfOther) {
    EXPECT_EQ(Levenshtein(u"", u"abc"), 3u);
    EXPECT_EQ(Levenshtein(u"abc", u""), 3u);
}

TEST(EditDistance, IdenticalIsZero) {
    EXPECT_EQ(Levenshtein(u"abc", u"abc"), 0u);
    EXPECT_EQ(Levenshtein(u"việt nam", u"việt nam"), 0u);
}

TEST(EditDistance, SingleSubstitutionIsOne) {
    EXPECT_EQ(Levenshtein(u"abc", u"abd"), 1u);
}

TEST(EditDistance, SingleInsertionIsOne) {
    EXPECT_EQ(Levenshtein(u"ab", u"abc"), 1u);
}

TEST(EditDistance, SingleDeletionIsOne) {
    EXPECT_EQ(Levenshtein(u"abc", u"ab"), 1u);
}

TEST(EditDistance, ClassicKittenSitting) {
    // Standard textbook example: kitten -> sitting requires 3 edits
    // (substitute k->s, substitute e->i, insert g).
    EXPECT_EQ(Levenshtein(u"kitten", u"sitting"), 3u);
}

TEST(EditDistance, VietnameseSingleToneSwap) {
    // "việt" vs "viết" differ by ệ vs ế -- one substitution.
    EXPECT_EQ(Levenshtein(u"việt", u"viết"), 1u);
}

TEST(EditDistance, VietnameseLengthMismatch) {
    // "có dấu" (6 chars including space) vs "có" (2 chars) -- 4 deletions.
    EXPECT_EQ(Levenshtein(u"có dấu", u"có"), 4u);
}

TEST(EditDistance, OperandOrderIndependent) {
    // Levenshtein is symmetric: lev(a,b) == lev(b,a).
    EXPECT_EQ(Levenshtein(u"abcdef", u"abc"),
              Levenshtein(u"abc", u"abcdef"));
}

// --- ErrorPct cases --------------------------------------------------------

TEST(ErrorPct, BothEmptyIsZeroPct) {
    EXPECT_DOUBLE_EQ(ErrorPct(u"", u""), 0.0);
}

TEST(ErrorPct, ExactMatchIsZeroPct) {
    EXPECT_DOUBLE_EQ(ErrorPct(u"việt nam", u"việt nam"), 0.0);
}

TEST(ErrorPct, OneCharWrongOnThreeCharExpectedIsAboutThirtyThreePct) {
    // lev=1, expected.size()=3 -> 33.333...%
    const double pct = ErrorPct(u"abd", u"abc");
    EXPECT_NEAR(pct, 33.333, 0.01);
}

TEST(ErrorPct, ExpectedEmptyButActualNotIsHundredPct) {
    EXPECT_DOUBLE_EQ(ErrorPct(u"abc", u""), 100.0);
}

TEST(ErrorPct, VietnameseToneSwapOnElevenCharsIsAboutNinePct) {
    // "việt có dấu" (11 chars) vs "viết có dấu" -- one tone swap on ê.
    // lev=1, expected.size()=11 -> 9.09%
    const double pct = ErrorPct(u"việt có dấu", u"viết có dấu");
    EXPECT_NEAR(pct, 9.09, 0.01);
}

}  // namespace NextKey::TestRunner::Test
