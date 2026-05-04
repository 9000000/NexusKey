#include "PerfCsvWriter.h"

namespace NextKey::TestRunner::PerfCsvWriter {

void Write(std::ostream& out, const std::vector<CaseResult>& results) {
    out << "case_name,verdict,inter_key_us_config,wall_clock_ms,"
        << "l1_count,l1_mean_ms,l1_p50_ms,l1_p95_ms,l1_p99_ms,l1_max_ms\n";
    for (const auto& r : results) {
        out << r.name << ','
            << (r.passed ? "PASS" : "FAIL") << ','
            << r.interKeyMicrosConfig << ','
            << r.wallClockMs << ','
            << r.l1Stats.intervals << ','
            << r.l1Stats.meanMs << ','
            << r.l1Stats.p50Ms << ','
            << r.l1Stats.p95Ms << ','
            << r.l1Stats.p99Ms << ','
            << r.l1Stats.maxMs << '\n';
    }
}

}  // namespace NextKey::TestRunner::PerfCsvWriter
