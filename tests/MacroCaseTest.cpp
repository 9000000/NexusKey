// NexusKey - Macro expansion decision unit tests
// SPDX-License-Identifier: GPL-3.0-only

#include <gtest/gtest.h>
#include "core/MacroCase.h"

namespace NextKey::Macro {
namespace {

struct AsciiCaseMapper final : CaseMapper {
    void Upper(wchar_t* buf, std::size_t n) const override {
        for (std::size_t i = 0; i < n; ++i)
            if (buf[i] >= L'a' && buf[i] <= L'z') buf[i] = buf[i] - L'a' + L'A';
    }
    void Lower(wchar_t* buf, std::size_t n) const override {
        for (std::size_t i = 0; i < n; ++i)
            if (buf[i] >= L'A' && buf[i] <= L'Z') buf[i] = buf[i] - L'A' + L'a';
    }
};

TEST(MacroCaseSmokeTest, StubLinks) {
    AsciiCaseMapper mapper;
    std::wstring raw, prev;
    std::vector<uint8_t> widths;
    std::unordered_map<std::wstring, std::wstring> table;
    PlanInputs in{raw, prev, widths, table, false, CodeTable::Unicode, false, L' ', 200};
    auto plan = Plan(in, mapper);
    EXPECT_FALSE(plan.matched);   // stub returns default-constructed MacroPlan
}

TEST(ClipboardEscapesTest, EmptyInput) {
    EXPECT_EQ(ExpandEscapesForClipboard(L""), L"");
}

TEST(ClipboardEscapesTest, NoEscapes) {
    EXPECT_EQ(ExpandEscapesForClipboard(L"hello world"), L"hello world");
}

TEST(ClipboardEscapesTest, SingleNewlineEscape) {
    EXPECT_EQ(ExpandEscapesForClipboard(L"line1\\nline2"), L"line1\r\nline2");
}

TEST(ClipboardEscapesTest, MultipleNewlinesEscape) {
    EXPECT_EQ(ExpandEscapesForClipboard(L"a\\nb\\nc"), L"a\r\nb\r\nc");
}

TEST(ClipboardEscapesTest, LiteralBackslashFollowedByNonN) {
    // \\t is NOT an escape — only \n is recognized. \t passes through verbatim.
    EXPECT_EQ(ExpandEscapesForClipboard(L"a\\tb"), L"a\\tb");
}

TEST(BuildSegmentsTest, EmptyExpansion) {
    auto segs = BuildSegments(L"", CodeTable::Unicode);
    EXPECT_TRUE(segs.empty());
}

TEST(BuildSegmentsTest, PureTextNoNewline) {
    auto segs = BuildSegments(L"hello", CodeTable::Unicode);
    ASSERT_EQ(segs.size(), 1u);
    EXPECT_FALSE(segs[0].isReturn);
    EXPECT_EQ(segs[0].text, L"hello");
}

TEST(BuildSegmentsTest, TextNewlineText) {
    auto segs = BuildSegments(L"line1\\nline2", CodeTable::Unicode);
    ASSERT_EQ(segs.size(), 3u);
    EXPECT_FALSE(segs[0].isReturn); EXPECT_EQ(segs[0].text, L"line1");
    EXPECT_TRUE(segs[1].isReturn);  EXPECT_EQ(segs[1].text, L"");
    EXPECT_FALSE(segs[2].isReturn); EXPECT_EQ(segs[2].text, L"line2");
}

TEST(BuildSegmentsTest, LeadingNewline) {
    auto segs = BuildSegments(L"\\nhello", CodeTable::Unicode);
    ASSERT_EQ(segs.size(), 2u);
    EXPECT_TRUE(segs[0].isReturn);
    EXPECT_FALSE(segs[1].isReturn); EXPECT_EQ(segs[1].text, L"hello");
}

TEST(BuildSegmentsTest, NonUnicodeSingleByteEncoding) {
    // 'à' (U+00E0) under TCVN3 maps to 0xB5 (single byte).
    // Verify ConvertChar is invoked and result has the encoded byte, not 'à'.
    auto segs = BuildSegments(L"à", CodeTable::TCVN3);
    ASSERT_EQ(segs.size(), 1u);
    EXPECT_FALSE(segs[0].isReturn);
    ASSERT_EQ(segs[0].text.size(), 1u);
    EXPECT_NE(segs[0].text[0], L'à');               // must have been converted
    EXPECT_EQ(static_cast<uint16_t>(segs[0].text[0]), 0x00B5);
}

TEST(BuildSegmentsTest, NonUnicodeMultiUnitEncoding) {
    // 'á' (U+00E1) under VNIWindows maps to encoded value 0xF961 → 2 units:
    // units[0] = LOBYTE = 0x61 ('a'), units[1] = HIBYTE = 0xF9 (tone marker).
    // Both units must be appended to the segment text in order.
    auto segs = BuildSegments(L"á", CodeTable::VNIWindows);
    ASSERT_EQ(segs.size(), 1u);
    EXPECT_FALSE(segs[0].isReturn);
    ASSERT_EQ(segs[0].text.size(), 2u);
    EXPECT_EQ(static_cast<uint16_t>(segs[0].text[0]), 0x0061);
    EXPECT_EQ(static_cast<uint16_t>(segs[0].text[1]), 0x00F9);
}

}  // namespace
}  // namespace NextKey::Macro
