// Encoding.h — small UTF helpers shared by main.cpp and tests.
//
// BMP-only — Vietnamese chars all fit (≤ U+1EF9), no surrogate-pair handling
// needed. If we ever need to emit non-BMP (emoji, etc.) this must be revisited.

#pragma once

#include <string>
#include <string_view>

namespace NextKey::TestRunner::Encoding {

[[nodiscard]] inline std::u16string Utf8ToUtf16(std::string_view input) {
    std::u16string out;
    out.reserve(input.size());
    std::size_t i = 0;
    while (i < input.size()) {
        const auto byte = static_cast<unsigned char>(input[i]);
        if (byte < 0x80) {
            out.push_back(static_cast<char16_t>(byte));
            i += 1;
        } else if ((byte & 0xE0) == 0xC0 && i + 1 < input.size()) {
            const auto b1 = static_cast<unsigned char>(input[i + 1]);
            out.push_back(static_cast<char16_t>(((byte & 0x1F) << 6) | (b1 & 0x3F)));
            i += 2;
        } else if ((byte & 0xF0) == 0xE0 && i + 2 < input.size()) {
            const auto b1 = static_cast<unsigned char>(input[i + 1]);
            const auto b2 = static_cast<unsigned char>(input[i + 2]);
            out.push_back(static_cast<char16_t>(
                ((byte & 0x0F) << 12) | ((b1 & 0x3F) << 6) | (b2 & 0x3F)));
            i += 3;
        } else {
            // 4-byte sequence (non-BMP) or malformed -- skip 1 byte. Vietnamese
            // chars are all BMP so this path is for emoji / corrupt input only.
            i += 1;
        }
    }
    return out;
}

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
