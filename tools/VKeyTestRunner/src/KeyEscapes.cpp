#include "KeyEscapes.h"

namespace NextKey::TestRunner::KeyEscapes {

std::optional<std::u16string> Resolve(std::u16string_view input) {
    std::u16string out;
    out.reserve(input.size());
    for (std::size_t i = 0; i < input.size(); ++i) {
        const char16_t ch = input[i];
        if (ch != u'\\') {
            out.push_back(ch);
            continue;
        }
        if (i + 1 >= input.size()) {
            return std::nullopt;  // trailing backslash
        }
        const char16_t esc = input[++i];
        switch (esc) {
            case u'b':  out.push_back(u'\b'); break;
            case u't':  out.push_back(u'\t'); break;
            case u'n':  out.push_back(u'\n'); break;
            case u'r':  out.push_back(u'\r'); break;
            case u'\\': out.push_back(u'\\'); break;
            case u'"':  out.push_back(u'"');  break;
            default:
                return std::nullopt;
        }
    }
    return out;
}

}  // namespace NextKey::TestRunner::KeyEscapes
