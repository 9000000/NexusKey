// NexusKey - Auto-Capitalization Decision
// Copyright (c) 2024-2026 PhatMT. All rights reserved.
// SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-NexusKey-Commercial
//
// Pure decision: given a snapshot of text up to (but not including) the
// caret, should the next typed letter be auto-capitalized?
//
// Linux-portable so the rule has Linux GTest coverage even though the
// only consumer (TSF `InspectPrecedingTextEditSession`) needs Win32
// edit sessions to obtain the buffer.

#pragma once

#include <cstddef>

namespace NextKey {

/// Returns true when the next typed character should be capitalized.
///
/// Rule:
///   - Empty buffer → true (start of document).
///   - Only whitespace before caret → true (start of document, modulo
///     leading spaces).
///   - Last non-whitespace char is `\n` or `\r` → true (start of line).
///   - Last non-whitespace char is `.`, `?`, or `!` AND at least one
///     whitespace separated it from the caret → true (start of new
///     sentence). The whitespace gate is required so domains glued to
///     the period (".com", ".vn") are NOT auto-capped.
///   - Otherwise → false.
[[nodiscard]] inline bool ComputeShouldAutoCap(const wchar_t* buf, std::size_t len) noexcept {
    if (len == 0) return true;

    std::size_t i = len;
    bool skippedWhitespace = false;
    while (i > 0 && (buf[i - 1] == L' ' || buf[i - 1] == L'\t')) {
        --i;
        skippedWhitespace = true;
    }

    if (i == 0) return true;

    const wchar_t c = buf[i - 1];
    return (c == L'\n' || c == L'\r') ||
           ((c == L'.' || c == L'?' || c == L'!') && skippedWhitespace);
}

}  // namespace NextKey
