// VKey - Shared quick-convert decisions and transformations
// SPDX-License-Identifier: GPL-3.0-only

#include "QuickConvertLogic.h"

#include "core/engine/CodeTableConverter.h"

namespace NextKey {

std::wstring ApplyQuickConvertOption(const std::wstring& input,
                                     QuickConvertOption option,
                                     const ConvertConfig& config) {
    switch (option) {
        case QuickConvertOption::Encoding: {
            const auto source = static_cast<CodeTable>(config.sourceEncoding);
            const auto destination = static_cast<CodeTable>(config.destEncoding);
            return CodeTableConverter::EncodeString(
                CodeTableConverter::DecodeString(input, source), destination);
        }
        case QuickConvertOption::Upper:
            return CodeTableConverter::ToUpper(input);
        case QuickConvertOption::Lower:
            return CodeTableConverter::ToLower(input);
        case QuickConvertOption::SentenceCase:
            return CodeTableConverter::ToSentenceCase(input);
        case QuickConvertOption::TitleCase:
            return CodeTableConverter::ToTitleCase(input);
        case QuickConvertOption::RemoveDiacritics:
            return CodeTableConverter::RemoveDiacritics(input);
    }
    return input;
}

std::wstring ApplyQuickConvertConfig(const std::wstring& input,
                                     const ConvertConfig& config) {
    std::wstring result = input;
    if (config.sourceEncoding != config.destEncoding) {
        result = ApplyQuickConvertOption(result, QuickConvertOption::Encoding, config);
    }
    if (config.removeMark) {
        result = ApplyQuickConvertOption(result, QuickConvertOption::RemoveDiacritics, config);
    }
    if (config.allCaps) {
        result = ApplyQuickConvertOption(result, QuickConvertOption::Upper, config);
    } else if (config.allLower) {
        result = ApplyQuickConvertOption(result, QuickConvertOption::Lower, config);
    } else if (config.capsFirst) {
        result = ApplyQuickConvertOption(result, QuickConvertOption::SentenceCase, config);
    } else if (config.capsEach) {
        result = ApplyQuickConvertOption(result, QuickConvertOption::TitleCase, config);
    }
    return result;
}

std::vector<QuickConvertOption> GetEnabledQuickConvertOptions(
    const ConvertConfig& config) {
    std::vector<QuickConvertOption> options;
    if (config.sourceEncoding != config.destEncoding) {
        options.push_back(QuickConvertOption::Encoding);
    }
    if (config.allCaps) options.push_back(QuickConvertOption::Upper);
    if (config.allLower) options.push_back(QuickConvertOption::Lower);
    if (config.capsFirst) options.push_back(QuickConvertOption::SentenceCase);
    if (config.capsEach) options.push_back(QuickConvertOption::TitleCase);
    if (config.removeMark) options.push_back(QuickConvertOption::RemoveDiacritics);
    return options;
}

const wchar_t* GetQuickConvertOptionName(QuickConvertOption option) noexcept {
    switch (option) {
        case QuickConvertOption::Encoding:
            return L"\x2192 Chuy\x1EC3n m\x00E3";
        case QuickConvertOption::Upper:
            return L"\x2192 ch\x1EEF HOA";
        case QuickConvertOption::Lower:
            return L"\x2192 ch\x1EEF th\x01B0\x1EDDng";
        case QuickConvertOption::SentenceCase:
            return L"\x2192 Hoa \x0111\x1EA7u c\x00E2u";
        case QuickConvertOption::TitleCase:
            return L"\x2192 Hoa T\x1EEBng Ch\x1EEF";
        case QuickConvertOption::RemoveDiacritics:
            return L"\x2192 B\x1ECF d\x1EA5u";
    }
    return L"\x2192 Chuy\x1EC3n m\x00E3 xong";
}

}  // namespace NextKey
