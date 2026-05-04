// TelexTest.cpp — locks our C++ Telex port against vn-str golden output.
// Cross-platform (Linux + Windows). Source of truth: TelexGolden.h.

#include <gtest/gtest.h>

#include <string>

#include "Telex.h"
#include "TelexGolden.h"

namespace NextKey::TestRunner::Test {

namespace {

// UTF-16 (BMP only) → UTF-8 for ADD_FAILURE printing. Vietnamese chars all
// fit in BMP (≤ U+FFFF), so no surrogate-pair handling needed.
std::string Utf8(std::u16string_view s) {
    std::string r;
    r.reserve(s.size() * 3);
    for (char16_t c : s) {
        if (c < 0x80) {
            r.push_back(static_cast<char>(c));
        } else if (c < 0x800) {
            r.push_back(static_cast<char>(0xC0 | (c >> 6)));
            r.push_back(static_cast<char>(0x80 | (c & 0x3F)));
        } else {
            r.push_back(static_cast<char>(0xE0 | (c >> 12)));
            r.push_back(static_cast<char>(0x80 | ((c >> 6) & 0x3F)));
            r.push_back(static_cast<char>(0x80 | (c & 0x3F)));
        }
    }
    return r;
}

}  // namespace

// Hardcoded sanity tests — independent of generator.
TEST(TelexTest, EmptyString) {
    EXPECT_EQ(::NextKey::TestRunner::Telex::StrToTelex(u""), std::u16string(u""));
}

TEST(TelexTest, AsciiPassthrough) {
    EXPECT_EQ(::NextKey::TestRunner::Telex::StrToTelex(u"hello"), std::u16string(u"hello"));
}

TEST(TelexTest, BasicVietnameseLowercase) {
    EXPECT_EQ(::NextKey::TestRunner::Telex::StrToTelex(u"việt"), std::u16string(u"vieejt"));
}

TEST(TelexTest, UppercaseVietnamesePassesThrough) {
    // vn-str only converts lowercase — uppercase Vietnamese passes through unchanged.
    EXPECT_EQ(::NextKey::TestRunner::Telex::StrToTelex(u"VIỆT"), std::u16string(u"VIỆT"));
}

TEST(TelexTest, MixedSentence) {
    EXPECT_EQ(
        ::NextKey::TestRunner::Telex::StrToTelex(u"Trường Sa"),
        std::u16string(u"Truwowfng Sa"));
}

TEST(TelexTest, NumbersAndPunctuationPassThrough) {
    EXPECT_EQ(
        ::NextKey::TestRunner::Telex::StrToTelex(u"123, abc!"),
        std::u16string(u"123, abc!"));
}

// Parameterized test over generated golden corpus (201 cases).
class TelexGoldenTest : public ::testing::TestWithParam<TelexGoldenCase> {};

TEST_P(TelexGoldenTest, MatchesVnStr) {
    const auto& c = GetParam();
    const std::u16string actual = ::NextKey::TestRunner::Telex::StrToTelex(c.input);
    const std::u16string expected = c.expected;
    EXPECT_EQ(actual, expected)
        << "Group:    " << c.group << "\n"
        << "Input:    " << Utf8(c.input) << "\n"
        << "Expected: " << Utf8(expected) << "\n"
        << "Actual:   " << Utf8(actual);
}

INSTANTIATE_TEST_SUITE_P(
    AllGoldenCases,
    TelexGoldenTest,
    ::testing::ValuesIn(kTelexGolden, kTelexGolden + kTelexGoldenCount));

}  // namespace NextKey::TestRunner::Test
