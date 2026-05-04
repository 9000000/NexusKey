// CaseResult.h -- aggregated outcome of a single test case run.
//
// Filled by RunSingleCase + post-mortem L1 analysis, then handed to the
// JUnit XML writer and the perf CSV writer.

#pragma once

#include <cstdint>
#include <string>

#include "HookLogParser.h"

namespace NextKey::TestRunner {

struct CaseResult {
    std::string name;
    bool passed = false;
    std::string failureMessage;     // empty when passed
    uint64_t wallClockMs = 0;       // total time for the case (clear+send+verify)
    uint32_t interKeyMicrosConfig = 0;  // mirrors TestCase.interKeyMicros
    HookLogParser::TimingStats l1Stats;  // populated post-mortem; zeroed if unavailable
};

}  // namespace NextKey::TestRunner
