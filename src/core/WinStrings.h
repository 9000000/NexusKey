// VKey - Windows UTF-8/Wide String Conversion Utilities
// SPDX-License-Identifier: AGPL-3.0-only

#pragma once

#ifdef _WIN32

#include <Windows.h>
#include <string>
#include <cstring>
#include <cwchar>

namespace NextKey {

/// Get formatted build version string (e.g. YYYYMMDD.HHMM) from compile time macros
[[nodiscard]] inline std::wstring GetBuildVersion(const char* dateStr, const char* timeStr) {
    if (!dateStr || strlen(dateStr) < 11 || !timeStr || strlen(timeStr) < 8) {
        return L"Unknown";
    }

    char month[4] = { dateStr[0], dateStr[1], dateStr[2], '\0' };
    int monthNum = 1;
    if (strcmp(month, "Jan") == 0) monthNum = 1;
    else if (strcmp(month, "Feb") == 0) monthNum = 2;
    else if (strcmp(month, "Mar") == 0) monthNum = 3;
    else if (strcmp(month, "Apr") == 0) monthNum = 4;
    else if (strcmp(month, "May") == 0) monthNum = 5;
    else if (strcmp(month, "Jun") == 0) monthNum = 6;
    else if (strcmp(month, "Jul") == 0) monthNum = 7;
    else if (strcmp(month, "Aug") == 0) monthNum = 8;
    else if (strcmp(month, "Sep") == 0) monthNum = 9;
    else if (strcmp(month, "Oct") == 0) monthNum = 10;
    else if (strcmp(month, "Nov") == 0) monthNum = 11;
    else if (strcmp(month, "Dec") == 0) monthNum = 12;

    int day = 0;
    if (dateStr[4] == ' ') {
        day = dateStr[5] - '0';
    } else {
        day = (dateStr[4] - '0') * 10 + (dateStr[5] - '0');
    }

    int year = (dateStr[7] - '0') * 1000 + (dateStr[8] - '0') * 100 + (dateStr[9] - '0') * 10 + (dateStr[10] - '0');

    int hour = (timeStr[0] - '0') * 10 + (timeStr[1] - '0');
    int minute = (timeStr[3] - '0') * 10 + (timeStr[4] - '0');

    wchar_t buf[64];
    swprintf_s(buf, L"%04d%02d%02d.%02d%02d", year, monthNum, day, hour, minute);
    return buf;
}

/// Convert a wide string (UTF-16) to a UTF-8 std::string.
[[nodiscard]] inline std::string WideToUtf8(const std::wstring& wstr) {
    if (wstr.empty()) return {};
    int len = WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (len <= 0) return {};
    std::string result(len - 1, 0);
    WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), -1, result.data(), len, nullptr, nullptr);
    return result;
}

/// Convert a UTF-8 std::string to a wide string (UTF-16).
[[nodiscard]] inline std::wstring Utf8ToWide(const std::string& str) {
    if (str.empty()) return {};
    int len = MultiByteToWideChar(CP_UTF8, 0, str.c_str(), -1, nullptr, 0);
    if (len <= 0) return {};
    std::wstring result(len - 1, 0);
    MultiByteToWideChar(CP_UTF8, 0, str.c_str(), -1, result.data(), len);
    return result;
}

}  // namespace NextKey

#endif  // _WIN32
