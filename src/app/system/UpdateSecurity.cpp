// NexusKey - Update Security Helpers Implementation
// SPDX-License-Identifier: GPL-3.0-only

#include "UpdateSecurity.h"

#include <algorithm>
#include <cctype>
#include <cwctype>

#ifdef _WIN32
#include <Windows.h>
#include <bcrypt.h>
#include <urlmon.h>
#include <fstream>
#include <sstream>
#pragma comment(lib, "bcrypt.lib")
#pragma comment(lib, "urlmon.lib")
#endif

namespace NextKey {

// ── SEC-002: PowerShell single-quote escaping ──────────────────────────────

std::wstring EscapePowerShellSingleQuote(const std::wstring& input) noexcept {
    try {
        std::wstring result;
        result.reserve(input.size() + 8);
        for (wchar_t ch : input) {
            result += ch;
            if (ch == L'\'') {
                result += L'\'';  // Double the single quote: ' -> ''
            }
        }
        return result;
    } catch (...) {
        return input;  // Allocation failure — return unescaped (best effort)
    }
}

// ── SEC-003: URL domain validation ─────────────────────────────────────────

namespace {

bool StartsWithIgnoreCase(const std::wstring& str, const wchar_t* prefix) noexcept {
    for (size_t i = 0; prefix[i] != L'\0'; ++i) {
        if (i >= str.size()) return false;
        if (towlower(str[i]) != towlower(prefix[i])) return false;
    }
    return true;
}

bool StartsWithIgnoreCase(const std::string& str, const char* prefix) noexcept {
    for (size_t i = 0; prefix[i] != '\0'; ++i) {
        if (i >= str.size()) return false;
        if (tolower(static_cast<unsigned char>(str[i])) !=
            tolower(static_cast<unsigned char>(prefix[i]))) return false;
    }
    return true;
}

}  // namespace

bool IsAllowedDownloadUrl(const std::wstring& url) noexcept {
    static constexpr const wchar_t* allowedPrefixes[] = {
        L"https://github.com/",
        L"https://objects.githubusercontent.com/",
        L"https://codeload.github.com/",
    };
    for (const auto* prefix : allowedPrefixes) {
        if (StartsWithIgnoreCase(url, prefix)) return true;
    }
    return false;
}

bool IsAllowedDownloadUrl(const std::string& url) noexcept {
    static constexpr const char* allowedPrefixes[] = {
        "https://github.com/",
        "https://objects.githubusercontent.com/",
        "https://codeload.github.com/",
    };
    for (const auto* prefix : allowedPrefixes) {
        if (StartsWithIgnoreCase(url, prefix)) return true;
    }
    return false;
}

// ── SEC-001: SHA-256 (Windows-only, implementations added in Task 3) ───────

#ifdef _WIN32

std::string ParseSha256File(const std::string& content) noexcept {
    // Implementation added in Task 3
    (void)content;
    return {};
}

std::string ComputeFileSha256(const std::wstring& filePath) noexcept {
    // Implementation added in Task 3
    (void)filePath;
    return {};
}

bool VerifyDownloadedZip(
    const std::wstring& zipUrl,
    const std::wstring& localZipPath) noexcept
{
    // Implementation added in Task 4
    (void)zipUrl;
    (void)localZipPath;
    return false;
}

#endif  // _WIN32

}  // namespace NextKey
