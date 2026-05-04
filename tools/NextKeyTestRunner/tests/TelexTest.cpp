// TelexTest.cpp — locks our C++ Telex port against vn-str golden output.
// Cross-platform (Linux + Windows). Source of truth: TelexGolden.h.

#include <gtest/gtest.h>

#include <iterator>
#include <string>

#include "Telex.h"
#include "TelexGolden.h"

namespace NextKey::TestRunner::Test {

namespace {

// UTF-16 (BMP only) → UTF-8 for ADD_FAILURE printing. Vietnamese chars all
// fit in BMP (≤ U+FFFF), so no surrogate-pair handling needed.
std::string Utf8(std::u16string_view input) {
    std::string out;
    out.reserve(input.size() * 3);
    for (char16_t ch : input) {
        if (ch < 0x80) {
            out.push_back(static_cast<char>(ch));
        } else if (ch < 0x800) {
            out.push_back(static_cast<char>(0xC0 | (ch >> 6)));
            out.push_back(static_cast<char>(0x80 | (ch & 0x3F)));
        } else {
            out.push_back(static_cast<char>(0xE0 | (ch >> 12)));
            out.push_back(static_cast<char>(0x80 | ((ch >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (ch & 0x3F)));
        }
    }
    return out;
}

}  // namespace

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
        << "Input:    " << Utf8(testCase.input) << "\n"
        << "Expected: " << Utf8(testCase.expected) << "\n"
        << "Actual:   " << Utf8(actual);
}

INSTANTIATE_TEST_SUITE_P(
    AllGoldenCases,
    TelexGoldenTest,
    ::testing::ValuesIn(std::begin(kTelexGolden), std::end(kTelexGolden)));

}  // namespace NextKey::TestRunner::Test
