// VKey - tray status tooltip model and formatter
// SPDX-License-Identifier: AGPL-3.0-only

#pragma once

#include <string>
#include <string_view>

namespace NextKey {

/// Worker-supplied half of the tooltip. Deliberately excludes V/E mode and the
/// TSF-indicator setting: those are owned by TrayIcon locally, and folding them
/// in here would make the snapshot equality check fire on local-only changes.
struct TrayStatusContext {
    std::wstring exeName;
    std::wstring ruleText;
    bool isTsf{false};
    bool isRustEngine{false};

    bool operator==(const TrayStatusContext&) const = default;
};

/// `modeLabel` is the localized V/E line (StringId::TIP_VIETNAMESE / _ENGLISH),
/// passed in rather than looked up so this stays pure and language-independent.
///
/// `modeLabel` is the localized V/E line (StringId::TIP_VIETNAMESE / _ENGLISH),
/// passed in rather than looked up so this stays pure and language-independent.
///
/// An empty exeName means no focus classification has landed yet (cold start).
/// Report the mode alone instead of guessing engine/method — the first
/// classification arrives within one worker tick and fills the rest in.
[[nodiscard]] inline std::wstring
FormatTrayStatusText(std::wstring_view modeLabel,
                     const TrayStatusContext& context) {
    if (context.exeName.empty()) return std::wstring{modeLabel};

    constexpr std::wstring_view kAppSeparator = L" \x2014 ";
    constexpr std::wstring_view kPartSeparator = L" \x2022 ";
    const std::wstring_view engine =
        context.isRustEngine ? L"Rust" : L"C++";
    const std::wstring_view method = context.isTsf ? L"TSF" : L"Hook";

    std::wstring text;
    text.reserve(modeLabel.size() + context.exeName.size() +
                 context.ruleText.size() + engine.size() + method.size() + 16);
    text.append(modeLabel);
    text.append(kAppSeparator);
    text.append(context.exeName);
    text.append(kPartSeparator);
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
