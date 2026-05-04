// TelexTest.cpp — locks our C++ Telex port against vn-str golden output.
// Cross-platform (Linux + Windows). Source of truth: TelexGolden.h.

#include <gtest/gtest.h>

#include <iterator>
#include <string>

#include "Encoding.h"
#include "Telex.h"
#include "TelexGolden.h"

namespace NextKey::TestRunner::Test {

// Hardcoded sanity tests — independent of generator.
TEST(TelexTest, EmptyString) {
    EXPECT_EQ(::NextKey::TestRunner::Telex::StrToTelex(u""), u"");
}

TEST(TelexTest, AsciiPassthrough) {
    EXPECT_EQ(::NextKey::TestRunner::Telex::StrToTelex(u"hello"), u"hello");
}

TEST(TelexTest, BasicVietnameseLowercase) {
    EXPECT_EQ(::NextKey::TestRunner::Telex::StrToTelex(u"việt"), u"vieejt");
}

TEST(TelexTest, UppercaseVietnamesePassesThrough) {
    // vn-str only converts lowercase — uppercase Vietnamese passes through unchanged.
    EXPECT_EQ(::NextKey::TestRunner::Telex::StrToTelex(u"VIỆT"), u"VIỆT");
}

TEST(TelexTest, MixedSentence) {
    EXPECT_EQ(::NextKey::TestRunner::Telex::StrToTelex(u"Trường Sa"), u"Truwowfng Sa");
}

TEST(TelexTest, NumbersAndPunctuationPassThrough) {
    EXPECT_EQ(::NextKey::TestRunner::Telex::StrToTelex(u"123, abc!"), u"123, abc!");
}

// Parameterized test over generated golden corpus (201 cases).
class TelexGoldenTest : public ::testing::TestWithParam<TelexGoldenCase> {};

TEST_P(TelexGoldenTest, MatchesVnStr) {
    const auto& testCase = GetParam();
    const std::u16string actual = ::NextKey::TestRunner::Telex::StrToTelex(testCase.input);
    EXPECT_EQ(actual, testCase.expected)
        << "Group:    " << testCase.group << "\n"
        << "Input:    " << Encoding::Utf16ToUtf8(testCase.input) << "\n"
        << "Expected: " << Encoding::Utf16ToUtf8(testCase.expected) << "\n"
        << "Actual:   " << Encoding::Utf16ToUtf8(actual);
}

INSTANTIATE_TEST_SUITE_P(
    AllGoldenCases,
    TelexGoldenTest,
    ::testing::ValuesIn(std::begin(kTelexGolden), std::end(kTelexGolden)));

}  // namespace NextKey::TestRunner::Test
