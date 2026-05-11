// Telex.h — C++ port of vn-str strToTelex (https://github.com/tronghieu60s/vn-str)
// Header-only, pure C++, cross-platform (no Win32 deps). Used by test runner
// to convert Vietnamese strings → raw Telex keystrokes for SendInput driving.
//
// Algorithm matches vn-str exactly: per-codepoint lookup in a 67-entry table,
// fallback to original char. Lowercase Vietnamese only — uppercase passes through
// unchanged (this matches vn-str's behavior, which we lock as our golden).

#pragma once

#include <cstddef>
#include <string>
#include <string_view>

namespace NextKey::TestRunner::Telex {

namespace detail {

struct Entry {
    char16_t input;
    std::u16string_view output;
};

// Table mirrors vn-str/src/convertString.ts VN_TELEX (commit 0.4.0).
inline constexpr Entry kTable[] = {
    {u'á', u"as"}, {u'à', u"af"}, {u'ả', u"ar"}, {u'ã', u"ax"}, {u'ạ', u"aj"},
    {u'ắ', u"aws"}, {u'ằ', u"awf"}, {u'ẳ', u"awr"}, {u'ẵ', u"awx"}, {u'ặ', u"awj"},
    {u'ă', u"aw"},
    {u'ấ', u"aas"}, {u'ầ', u"aaf"}, {u'ẩ', u"aar"}, {u'ẫ', u"aax"}, {u'ậ', u"aaj"},
    {u'â', u"aa"},
    {u'đ', u"dd"},
    {u'é', u"es"}, {u'è', u"ef"}, {u'ẻ', u"er"}, {u'ẽ', u"ex"}, {u'ẹ', u"ej"},
    {u'ế', u"ees"}, {u'ề', u"eef"}, {u'ể', u"eer"}, {u'ễ', u"eex"}, {u'ệ', u"eej"},
    {u'ê', u"ee"},
    {u'í', u"is"}, {u'ì', u"if"}, {u'ỉ', u"ir"}, {u'ĩ', u"ix"}, {u'ị', u"ij"},
    {u'ó', u"os"}, {u'ò', u"of"}, {u'ỏ', u"or"}, {u'õ', u"ox"}, {u'ọ', u"oj"},
    {u'ố', u"oos"}, {u'ồ', u"oof"}, {u'ổ', u"oor"}, {u'ỗ', u"oox"}, {u'ộ', u"ooj"},
    {u'ô', u"oo"},
    {u'ớ', u"ows"}, {u'ờ', u"owf"}, {u'ở', u"owr"}, {u'ỡ', u"owx"}, {u'ợ', u"owj"},
    {u'ơ', u"ow"},
    {u'ú', u"us"}, {u'ù', u"uf"}, {u'ủ', u"ur"}, {u'ũ', u"ux"}, {u'ụ', u"uj"},
    {u'ứ', u"uws"}, {u'ừ', u"uwf"}, {u'ử', u"uwr"}, {u'ữ', u"uwx"}, {u'ự', u"uwj"},
    {u'ư', u"uw"},
    {u'ý', u"ys"}, {u'ỳ', u"yf"}, {u'ỷ', u"yr"}, {u'ỹ', u"yx"}, {u'ỵ', u"yj"},
};

inline constexpr std::size_t kTableSize = sizeof(kTable) / sizeof(kTable[0]);

// Returns empty view if `ch` has no replacement (caller passes `ch` through).
[[nodiscard]] constexpr std::u16string_view Lookup(char16_t ch) noexcept {
    for (std::size_t i = 0; i < kTableSize; ++i) {
        if (kTable[i].input == ch) return kTable[i].output;
    }
    return {};
}

}  // namespace detail

// Vietnamese chars are BMP (≤ U+1EF9), so per-char16_t iteration is correct.
// Surrogate pairs would split incorrectly, but vn-str has the same limitation.
[[nodiscard]] inline std::u16string StrToTelex(std::u16string_view input) {
    std::u16string out;
    out.reserve(input.size() * 3);  // worst case 3 wchars per input ("uws", "ows" ...)
    for (char16_t ch : input) {
        const std::u16string_view repl = detail::Lookup(ch);
        if (!repl.empty()) {
            out.append(repl);
        } else {
            out.push_back(ch);
        }
    }
    return out;
}

}  // namespace NextKey::TestRunner::Telex
