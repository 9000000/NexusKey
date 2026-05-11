// CaseResult.h -- aggregated outcome of a single test case run.
//
// Filled by RunSingleCase + post-mortem L1 analysis, then handed to the
// JUnit XML writer and the perf CSV writer.
//
// VerdictMode (mirroring TestCase) records how the case was scored:
//   Exact         -- pass = (actual == expected); errorChars/errorPct are 0.
//   EditDistance  -- pass = ((100 - errorPct) >= thresholdPct); errorChars
//                    is the Levenshtein count, errorPct = lev / max(1, |expected|) * 100.

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

#include "HookLogParser.h"
#include "TestCase.h"

namespace NextKey::TestRunner {

struct CaseResult {
    std::string name;
    bool passed = false;
    std::string failureMessage;     // empty when passed
    uint64_t wallClockMs = 0;       // total time for the case (clear+send+verify)
    uint32_t interKeyMicrosConfig = 0;  // mirrors TestCase.interKeyMicros
    HookLogParser::TimingStats l1Stats;  // populated post-mortem; zeroed if unavailable
    VerdictMode verdictMode = VerdictMode::Exact;
    std::size_t errorChars = 0;     // Levenshtein distance (only meaningful for EditDistance)
    double errorPct = 0.0;          // % error (only meaningful for EditDistance)
};

}  // namespace NextKey::TestRunner
