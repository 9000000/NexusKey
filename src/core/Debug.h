// NexusKey - Debug Logging Infrastructure
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <Windows.h>
#include <cstdio>

namespace NextKey {

// Debug logging - compiles out in Release builds unless NEXTKEY_DEBUG is defined
#if defined(_DEBUG) || defined(NEXTKEY_DEBUG)

inline void DebugLog(const wchar_t* format, ...) {
    wchar_t buffer[1024];
    va_list args;
    va_start(args, format);
    vswprintf_s(buffer, format, args);
    va_end(args);
    OutputDebugStringW(buffer);
}

#define NEXTKEY_LOG(fmt, ...) ::NextKey::DebugLog(L"NexusKey: " fmt L"\n", ##__VA_ARGS__)

#else

#define NEXTKEY_LOG(...) ((void)0)

#endif

}  // namespace NextKey
