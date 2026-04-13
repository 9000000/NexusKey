// NexusKey - Debug Logging Infrastructure
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <cstdio>

#if defined(_DEBUG) || defined(NEXTKEY_DEBUG)
#ifdef _WIN32
#include <Windows.h>
#endif
#endif

namespace NextKey {

// Debug logging - compiles out in Release builds unless NEXTKEY_DEBUG is defined
#if defined(_DEBUG) || defined(NEXTKEY_DEBUG)

#ifdef _WIN32

// Note: Messages longer than 1024 chars are silently truncated
inline void DebugLog(const wchar_t* format, ...) {
    wchar_t buffer[1024];
    va_list args;
    va_start(args, format);
    _vsnwprintf_s(buffer, 1024, _TRUNCATE, format, args);
    va_end(args);
    OutputDebugStringW(buffer);
}
#else
inline void DebugLog(const wchar_t* /*format*/, ...) {
    // No-op on non-Windows platforms
}
#endif  // _WIN32

#define NEXTKEY_LOG(fmt, ...) ::NextKey::DebugLog(L"NexusKey: " fmt L"\n", ##__VA_ARGS__)

#else

#define NEXTKEY_LOG(...) ((void)0)

#endif

}  // namespace NextKey
