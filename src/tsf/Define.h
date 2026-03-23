// NexusKey - TSF Common Definitions
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

// Common macros and defines for TSF module
#define NEXUSKEY_TSF_VERSION_MAJOR 1
#define NEXUSKEY_TSF_VERSION_MINOR 0
#define NEXUSKEY_TSF_VERSION_PATCH 0

// Debug logging macro with format support
#ifdef _DEBUG
#include <cstdio>
inline void TsfLogImpl(const wchar_t* fmt, ...) {
    wchar_t prefix[64];
    _snwprintf_s(prefix, 64, _TRUNCATE, L"[NexusKey:%u] ", GetCurrentProcessId());
    wchar_t buf[512];
    va_list args;
    va_start(args, fmt);
    _vsnwprintf_s(buf, 512, _TRUNCATE, fmt, args);
    va_end(args);
    OutputDebugStringW(prefix);
    OutputDebugStringW(buf);
    OutputDebugStringW(L"\n");
}
#define TSF_LOG(...) TsfLogImpl(__VA_ARGS__)
#else
#define TSF_LOG(...) ((void)0)
#endif
