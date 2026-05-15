#include <gtest/gtest.h>

#include <sstream>

#include "PerfCsvWriter.h"

namespace NextKey::TestRunner::Test {

namespace {

std::string Render(const std::vector<CaseResult>& results) {
    std::ostringstream os;
    PerfCsvWriter::Write(os, results);
    return os.str();
}

}  // namespace

TEST(PerfCsvWriterTest, EmptyResultsEmitsHeaderOnly) {
    const auto csv = Render({});
    // 1 line for header, no data rows.
    EXPECT_EQ(std::count(csv.begin(), csv.end(), '\n'), 1);
    EXPECT_NE(csv.find("case_name,verdict,verdict_mode,error_chars,error_pct"), std::string::npos);
    EXPECT_NE(csv.find("l1_max_ms"), std::string::npos);
}

TEST(PerfCsvWriterTest, SinglePassRowHasCorrectFields) {
    CaseResult r;
    r.name = "1.1-ghost";
    r.passed = true;
    r.interKeyMicrosConfig = 10000;
    r.wallClockMs = 250;
    r.l1Stats.intervals = 5;
    r.l1Stats.meanMs = 13;
    r.l1Stats.p50Ms = 13;
    r.l1Stats.p95Ms = 17;
    r.l1Stats.p99Ms = 17;
    r.l1Stats.maxMs = 17;
    const auto csv = Render({r});

    // Expect exactly: header + 1 data row + (no trailing extra line).
    EXPECT_EQ(std::count(csv.begin(), csv.end(), '\n'), 2);
    EXPECT_NE(csv.find("1.1-ghost,PASS,exact,0,0.00,10000,250,5,13,13,17,17,17"),
              std::string::npos);
}

TEST(PerfCsvWriterTest, FailRowEmitsFailVerdict) {
    CaseResult r;
    r.name = "2.1-x2";
    r.passed = false;
    r.failureMessage = "diff";  // not emitted in CSV
    r.interKeyMicrosConfig = 1000;
    r.wallClockMs = 213;
    const auto csv = Render({r});
    EXPECT_NE(csv.find("2.1-x2,FAIL,exact,0,0.00,1000,213,0,0,0,0,0,0"),
              std::string::npos);
    EXPECT_EQ(csv.find("diff"), std::string::npos);  // failure msg not in CSV
}

TEST(PerfCsvWriterTest, EditDistanceRowEmitsModeAndErrorMetrics) {
    CaseResult r;
    r.name = "sustained-forward";
    r.passed = true;
    r.verdictMode = VerdictMode::EditDistance;
    r.errorChars = 7;
    r.errorPct = 4.66;          // 7 / 150 chars * 100
    r.interKeyMicrosConfig = 50000;
    r.wallClockMs = 12500;
    const auto csv = Render({r});
    EXPECT_NE(csv.find("sustained-forward,PASS,edit_distance,7,4.66,50000,12500"),
              std::string::npos);
}

TEST(PerfCsvWriterTest, MultipleRowsEmittedInOrder) {
    std::vector<CaseResult> results;
    for (int i = 0; i < 3; ++i) {
        CaseResult r;
        r.name = "case-" + std::to_string(i);
        r.passed = true;
        r.interKeyMicrosConfig = 5000;
        r.wallClockMs = 100 + static_cast<uint64_t>(i);
        results.push_back(r);
    }
    const auto csv = Render(results);
    EXPECT_EQ(std::count(csv.begin(), csv.end(), '\n'), 4);  // header + 3 rows
    EXPECT_LT(csv.find("case-0"), csv.find("case-1"));
    EXPECT_LT(csv.find("case-1"), csv.find("case-2"));
}

}  // namespace NextKey::TestRunner::Test
