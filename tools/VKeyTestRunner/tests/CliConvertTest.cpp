// CliConvertTest.cpp -- exercises the pure helper that backs the
// `VKeyTestRunner --convert TEXT` flag. Verifying the helper here lets us
// keep main.cpp's argv handling thin and avoids spawning a process from tests.

#include <gtest/gtest.h>

#include <sstream>
#include <string>
#include <string_view>

#include "CliConvert.h"

namespace NextKey::TestRunner::Test {

using NextKey::TestRunner::CliConvert::Convert;

// Helper: run Convert and return stdout as UTF-8.
[[nodiscard]] std::string CaptureConvert(std::u16string_view input) {
    std::ostringstream os;
    Convert(input, os);
    return os.str();
}

TEST(CliConvert, PlainAsciiPassesThrough) {
    EXPECT_EQ(CaptureConvert(u"abc"), "abc\n");
}

TEST(CliConvert, SingleVietnameseWord) {
    // "việt" -> "vieejt" (per Telex.h kTable: ệ -> eej).
    EXPECT_EQ(CaptureConvert(u"việt"), "vieejt\n");
}

TEST(CliConvert, SingleVietnameseWordWithSac) {
    // "viết" -> "vieest" (per vn-str algorithm: ế -> "ees" applied as a single
    // glyph substitution, so the trailing 't' goes AFTER the tone block).
    // This differs from what a human types (v-i-e-e-t-s) but is what the
    // golden table emits; engine produces the same final glyph for either
    // key sequence. See tests/TelexGolden.h:83 for the canonical mapping.
    EXPECT_EQ(CaptureConvert(u"viết"), "vieest\n");
}

TEST(CliConvert, MultipleWordsWithSpaces) {
    // "có dấu" -> "cos daasu" (ó -> os, ấ -> aas).
    EXPECT_EQ(CaptureConvert(u"có dấu"), "cos daasu\n");
}

TEST(CliConvert, SentenceFromUserExample) {
    // The example from pre-mortem session 2026-05-04.
    EXPECT_EQ(CaptureConvert(u"việt có dấu"), "vieejt cos daasu\n");
}

TEST(CliConvert, EmptyInputProducesNewlineOnly) {
    // Even empty input emits the trailing newline so shell pipes get a clean
    // record terminator.
    EXPECT_EQ(CaptureConvert(u""), "\n");
}

}  // namespace NextKey::TestRunner::Test
