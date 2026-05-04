// TestCase.h -- single test case loaded from a TOML corpus file.
//
// `keys` is post-escape-resolution: TOML escapes like \b \n have already been
// turned into U+0008 / U+000A. The driver consumes `keys` codepoint-by-
// codepoint and maps control codes (BS / TAB / Enter) to their VK equivalents.

#pragma once

#include <cstdint>
#include <string>

namespace NextKey::TestRunner {

struct TestCase {
    std::string name;
    std::string targetApp;          // "notepad", "chrome", ... informational at D6
    std::u16string keys;            // raw Telex keystrokes (escapes resolved)
    std::u16string expected;        // expected clipboard contents
    uint32_t interKeyMicros = 10'000;
    uint32_t budgetP99Micros = 0;   // 0 = no budget enforcement
};

}  // namespace NextKey::TestRunner
