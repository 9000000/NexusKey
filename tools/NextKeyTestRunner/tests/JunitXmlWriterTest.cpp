#include <gtest/gtest.h>

#include <sstream>

#include "JunitXmlWriter.h"

namespace NextKey::TestRunner::Test {

namespace {

std::string Render(std::string_view suite,
                   const std::vector<CaseResult>& results,
                   uint64_t totalMs) {
    std::ostringstream os;
    JunitXmlWriter::Write(os, suite, results, totalMs);
    return os.str();
}

}  // namespace

TEST(JunitXmlWriterTest, EmptyResultsEmitsZeroTests) {
    const auto xml = Render("chaos", {}, 0);
    EXPECT_NE(xml.find("tests=\"0\""), std::string::npos);
    EXPECT_NE(xml.find("failures=\"0\""), std::string::npos);
    EXPECT_NE(xml.find("name=\"chaos\""), std::string::npos);
}

TEST(JunitXmlWriterTest, SinglePassEmitsSelfClosingTestcase) {
    CaseResult r;
    r.name = "1.1-ghost";
    r.passed = true;
    r.wallClockMs = 250;
    const auto xml = Render("chaos", {r}, 250);

    EXPECT_NE(xml.find("name=\"1.1-ghost\""), std::string::npos);
    EXPECT_NE(xml.find("classname=\"chaos\""), std::string::npos);
    EXPECT_NE(xml.find("time=\"0.250\""), std::string::npos);
    EXPECT_NE(xml.find("/>"), std::string::npos);    // self-closing
    EXPECT_EQ(xml.find("<failure"), std::string::npos);
}

TEST(JunitXmlWriterTest, SingleFailEmitsFailureChild) {
    CaseResult r;
    r.name = "2.1-x2-space";
    r.passed = false;
    r.failureMessage = "expected: việt nam\n        actual:   vệet nam";
    r.wallClockMs = 213;
    const auto xml = Render("chaos", {r}, 213);

    EXPECT_NE(xml.find("name=\"2.1-x2-space\""), std::string::npos);
    EXPECT_NE(xml.find("<failure message=\"Clipboard mismatch\""), std::string::npos);
    EXPECT_NE(xml.find("expected: việt nam"), std::string::npos);
    EXPECT_NE(xml.find("actual:   vệet nam"), std::string::npos);
    EXPECT_NE(xml.find("</failure>"), std::string::npos);
}

TEST(JunitXmlWriterTest, EscapesXmlSpecialChars) {
    CaseResult r;
    r.name = "case-with-<>&";
    r.passed = false;
    r.failureMessage = R"(expected: a<b
actual:   a&b "quoted")";
    r.wallClockMs = 100;
    const auto xml = Render("suite-name-with-<>", {r}, 100);

    EXPECT_NE(xml.find("case-with-&lt;&gt;&amp;"), std::string::npos);
    EXPECT_NE(xml.find("suite-name-with-&lt;&gt;"), std::string::npos);
    EXPECT_NE(xml.find("a&lt;b"), std::string::npos);
    EXPECT_NE(xml.find("a&amp;b"), std::string::npos);
    EXPECT_NE(xml.find("&quot;quoted&quot;"), std::string::npos);
}

TEST(JunitXmlWriterTest, MixedPassFailCounts) {
    std::vector<CaseResult> results;
    for (int i = 0; i < 5; ++i) {
        CaseResult r;
        r.name = "case-" + std::to_string(i);
        r.passed = (i % 2 == 0);  // 3 pass (0, 2, 4), 2 fail (1, 3)
        if (!r.passed) r.failureMessage = "diff";
        r.wallClockMs = 100;
        results.push_back(r);
    }
    const auto xml = Render("chaos", results, 500);
    EXPECT_NE(xml.find("tests=\"5\""), std::string::npos);
    EXPECT_NE(xml.find("failures=\"2\""), std::string::npos);
}

TEST(JunitXmlWriterTest, TimeFormatPadsMillisecondsBelow100) {
    CaseResult r;
    r.name = "t";
    r.passed = true;
    r.wallClockMs = 5;        // 0.005 seconds
    const auto xml = Render("s", {r}, 5);
    EXPECT_NE(xml.find("time=\"0.005\""), std::string::npos);
}

TEST(JunitXmlWriterTest, TimeFormatHandlesLargeValues) {
    CaseResult r;
    r.name = "t";
    r.passed = true;
    r.wallClockMs = 12'345;   // 12.345 seconds
    const auto xml = Render("s", {r}, 12'345);
    EXPECT_NE(xml.find("time=\"12.345\""), std::string::npos);
}

}  // namespace NextKey::TestRunner::Test
