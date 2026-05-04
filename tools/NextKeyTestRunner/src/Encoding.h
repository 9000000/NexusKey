// Encoding.h — small UTF helpers shared by main.cpp and tests.
//
// BMP-only — Vietnamese chars all fit (≤ U+1EF9), no surrogate-pair handling
// needed. If we ever need to emit non-BMP (emoji, etc.) this must be revisited.

#pragma once

#include <string>
#include <string_view>

namespace NextKey::TestRunner::Encoding {

[[nodiscard]] inline std::string Utf16ToUtf8(std::u16string_view input) {
    std::string out;
    out.reserve(input.size() * 3);
    for (char16_t ch : input) {
        if (ch < 0x80) {
            out.push_back(static_cast<char>(ch));
        } else if (ch < 0x800) {
            out.push_back(static_cast<char>(0xC0 | (ch >> 6)));
            out.push_back(static_cast<char>(0x80 | (ch & 0x3F)));
        } else {
            out.push_back(static_cast<char>(0xE0 | (ch >> 12)));
            out.push_back(static_cast<char>(0x80 | ((ch >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (ch & 0x3F)));
        }
    }
    return out;
}

}  // namespace NextKey::TestRunner::Encoding
