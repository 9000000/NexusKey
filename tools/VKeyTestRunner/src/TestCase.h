// TestCase.h -- single test case loaded from a TOML corpus file.
//
// `keys` is post-escape-resolution: TOML escapes like \b \n have already been
// turned into U+0008 / U+000A. The driver consumes `keys` codepoint-by-
// codepoint and maps control codes (BS / TAB / Enter) to their VK equivalents.
//
// VerdictMode controls how the final clipboard contents are compared to
// `expected`:
//   Exact         -- actual == expected (binary PASS/FAIL). Default.
//   EditDistance  -- Levenshtein-derived error% must be <= (100 - thresholdPct)
//                    for PASS. Used for sustained-typing corpora where
//                    realistic engine output may have a few stray chars but
//                    still pass a corruption-rate gate.

#pragma once

#include <cstdint>
#include <string>

namespace NextKey::TestRunner {

enum class VerdictMode {
    Exact,
    EditDistance,
};

struct TestCase {
    std::string name;
    std::string targetApp;          // "notepad", "chrome", ... informational at D6
    std::u16string keys;            // raw Telex keystrokes (escapes resolved)
    std::u16string expected;        // expected clipboard contents
    uint32_t interKeyMicros = 10'000;
    uint32_t budgetP99Micros = 0;   // 0 = no budget enforcement
    VerdictMode verdictMode = VerdictMode::Exact;
    double thresholdPct = 100.0;    // only used when verdictMode == EditDistance
};

}  // namespace NextKey::TestRunner
