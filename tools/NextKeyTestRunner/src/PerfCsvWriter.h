// PerfCsvWriter.h -- emit per-case perf metrics as CSV (RFC 4180).
//
// One header row + one row per CaseResult. Suitable for spreadsheet import,
// pandas, or simple grep/awk. Test case names in our corpus are ASCII-only
// (e.g. "1.1-ghost-hoaf-bs-t") so we don't bother with comma/quote escaping
// today; if Vietnamese or commas appear in names later, this writer will
// emit broken CSV and should be revisited.

#pragma once

#include <ostream>
#include <vector>

#include "CaseResult.h"

namespace NextKey::TestRunner::PerfCsvWriter {

void Write(std::ostream& out, const std::vector<CaseResult>& results);

}  // namespace NextKey::TestRunner::PerfCsvWriter
