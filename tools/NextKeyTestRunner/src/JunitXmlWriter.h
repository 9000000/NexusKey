// JunitXmlWriter.h -- emit a JUnit-style XML report from CaseResult vector.
//
// JUnit XML is the de-facto exchange format for test reports (Jenkins,
// GitHub Actions, GitLab CI, Azure DevOps all consume it). One <testsuite>
// per corpus, one <testcase> per CaseResult, <failure> child for FAILs
// containing the verdict diff.

#pragma once

#include <ostream>
#include <string_view>
#include <vector>

#include "CaseResult.h"

namespace NextKey::TestRunner::JunitXmlWriter {

// Writes a JUnit XML document to `out`. Times are in seconds (per JUnit
// convention). XML special chars in `suiteName` and result fields are
// escaped.
void Write(std::ostream& out,
           std::string_view suiteName,
           const std::vector<CaseResult>& results,
           uint64_t totalWallClockMs);

}  // namespace NextKey::TestRunner::JunitXmlWriter
