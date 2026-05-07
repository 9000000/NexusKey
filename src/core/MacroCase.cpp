// NexusKey - Macro expansion decision logic implementation
// Copyright (c) 2024-2026 PhatMT. All rights reserved.
// SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-NexusKey-Commercial

#include "core/MacroCase.h"

#include "core/engine/CodeTableConverter.h"

#include <cwctype>

namespace NextKey::Macro {

namespace {
[[nodiscard]] std::wstring LowerCopy(std::wstring s) {
    for (auto& c : s) c = static_cast<wchar_t>(std::towlower(c));
    return s;
}
}  // namespace

MacroPlan Plan(const PlanInputs& in, const CaseMapper& mapper) {
    (void)in; (void)mapper; (void)LowerCopy;
    return {};   // stub — implemented in Task 4
}

std::wstring ExpandEscapesForClipboard(std::wstring_view expansion) {
    std::wstring out;
    out.reserve(expansion.size());
    for (std::size_t i = 0; i < expansion.size(); ++i) {
        if (expansion[i] == L'\\' && i + 1 < expansion.size() && expansion[i + 1] == L'n') {
            out += L"\r\n";
            ++i;
        } else {
            out += expansion[i];
        }
    }
    return out;
}

std::vector<Segment> BuildSegments(std::wstring_view expansion, CodeTable codeTable) {
    (void)expansion; (void)codeTable;
    return {};   // stub — implemented in Task 3
}

}  // namespace NextKey::Macro
