// VKey - tray status tooltip model and formatter
// SPDX-License-Identifier: AGPL-3.0-only

#pragma once

#include <string>
#include <string_view>

namespace NextKey {

struct TrayStatusContext {
    std::wstring exeName;
    std::wstring ruleText;
    bool isTsf{false};
    bool isRustEngine{false};

    bool operator==(const TrayStatusContext&) const = default;
};

[[nodiscard]] inline std::wstring
FormatTrayStatusText(const TrayStatusContext& context) {
    constexpr std::wstring_view kAppSeparator = L" \x2014 ";
    constexpr std::wstring_view kPartSeparator = L" \x2022 ";
    const std::wstring_view engine =
        context.isRustEngine ? L"Rust" : L"C++";
    const std::wstring_view method = context.isTsf ? L"TSF" : L"Hook";

    std::wstring text;
    text.reserve(context.exeName.size() + context.ruleText.size() +
                 engine.size() + method.size() + 12);
    if (!context.exeName.empty()) {
        text.append(context.exeName);
        text.append(kAppSeparator);
    }
    text.append(engine);
    text.append(kPartSeparator);
    text.append(method);
    if (!context.ruleText.empty()) {
        text.append(kPartSeparator);
        text.append(context.ruleText);
    }
    return text;
}

}  // namespace NextKey
