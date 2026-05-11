// CliConvert.h -- pure helper backing the `--convert TEXT` CLI flag.
//
// Reads UTF-16 Vietnamese (or mixed) text and writes the raw Telex keystroke
// sequence to the given output stream, terminated with '\n'. Used by:
//   - main.cpp wmain dispatch when --convert is supplied
//   - tests/CliConvertTest.cpp (with a stringstream sink) for verification
//
// Header-only so the cross-platform GTest target can exercise it on Linux
// without pulling in any Win32 plumbing.

#pragma once

#include <ostream>
#include <string>
#include <string_view>

#include "Encoding.h"
#include "Telex.h"

namespace NextKey::TestRunner::CliConvert {

// Writes Telex::StrToTelex(input) to `out`, UTF-8 encoded, with a trailing
// newline. Empty input emits just the newline (clean record terminator for
// shell pipelines).
inline void Convert(std::u16string_view input, std::ostream& out) {
    const std::u16string raw = Telex::StrToTelex(input);
    out << Encoding::Utf16ToUtf8(raw) << '\n';
}

}  // namespace NextKey::TestRunner::CliConvert
