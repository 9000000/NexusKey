// VKey - Path string helpers
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-VKey-Commercial
//
// Tiny cross-platform path-splitting helper. Lives in src/core so it is covered
// by the Linux VKeyTests target — the src/app inline copies (e.g.
// ExcludedAppsDialog::addApp's classifier-key normalization, #209) are Win32-only
// and were untestable; route the security-relevant ones through here instead.
//
// Handles BOTH separators ('\\' and '/') so a forward-slash path (browsed UNC,
// typed unix-style path) normalizes the same as a backslash one. No <filesystem>
// dependency — a plain find_last_of is faster and avoids std::filesystem's
// locale/edge-case surface for this trivial split.

#pragma once

#include <string>

namespace NextKey {

/// Last path component (file name) of `path`. Returns the whole string when
/// there is no separator (it already IS the basename). A trailing separator
/// yields an empty basename ("C:\\dir\\" -> "").
[[nodiscard]] inline std::wstring PathBasename(const std::wstring& path) noexcept {
    const auto slash = path.find_last_of(L"\\/");
    return (slash == std::wstring::npos) ? path : path.substr(slash + 1);
}

}  // namespace NextKey
