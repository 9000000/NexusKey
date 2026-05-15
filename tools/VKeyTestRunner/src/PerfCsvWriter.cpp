#include "PerfCsvWriter.h"

#include <cstdio>

namespace NextKey::TestRunner::PerfCsvWriter {

namespace {

constexpr const char* VerdictModeStr(VerdictMode m) noexcept {
    switch (m) {
        case VerdictMode::EditDistance: return "edit_distance";
        case VerdictMode::Exact:        // fallthrough
        default:                        return "exact";
    }
}

// Format error_pct with two decimals and no scientific notation.
void WriteErrorPct(std::ostream& out, double pct) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.2f", pct);
    out << buf;
}

}  // namespace

void Write(std::ostream& out, const std::vector<CaseResult>& results) {
    out << "case_name,verdict,verdict_mode,error_chars,error_pct,"
        << "inter_key_us_config,wall_clock_ms,"
        << "l1_count,l1_mean_ms,l1_p50_ms,l1_p95_ms,l1_p99_ms,l1_max_ms\n";
    for (const auto& r : results) {
        out << r.name << ','
            << (r.passed ? "PASS" : "FAIL") << ','
            << VerdictModeStr(r.verdictMode) << ','
            << r.errorChars << ',';
        WriteErrorPct(out, r.errorPct);
        out << ','
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
