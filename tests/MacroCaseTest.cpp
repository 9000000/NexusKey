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

}  // namespace
}  // namespace NextKey::Macro
