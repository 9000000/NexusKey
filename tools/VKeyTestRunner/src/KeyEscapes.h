// KeyEscapes.h -- resolves backslash escapes in a TOML `keys` field.
//
// Recognized escapes:
//   \b -> U+0008 (backspace)
//   \t -> U+0009 (tab)
//   \n -> U+000A (newline / Enter)
//   \r -> U+000D (carriage return)
//   \\ -> U+005C (backslash)
//   \" -> U+0022 (double quote)
//
// Returns nullopt on a trailing or unknown escape sequence. Caller maps to
// a parse error including the test case name.
//
// Note: TOML's own basic-string parser already resolves these escapes when
// reading a `"..."` value, so by the time this function sees the string,
// the Telex backslashes have already been turned into real U+0008 etc.
// We keep this module for the literal-string `'...'` case (no escape
// processing in TOML literal strings) and for hand-crafted unit tests.

#pragma once

#include <optional>
#include <string>
#include <string_view>

namespace NextKey::TestRunner::KeyEscapes {

[[nodiscard]] std::optional<std::u16string> Resolve(std::u16string_view input);

}  // namespace NextKey::TestRunner::KeyEscapes
