// NexusKey - TSF Common Definitions
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

// Common macros and defines for TSF module
#define NEXUSKEY_TSF_VERSION_MAJOR 1
#define NEXUSKEY_TSF_VERSION_MINOR 0
#define NEXUSKEY_TSF_VERSION_PATCH 0

// Debug logging macro with format support.
//
// Single OutputDebugStringW call per log line — concatenating prefix + body +
// newline into one local buffer first. Three separate OutputDebugStringW calls
// could interleave under concurrent TSF DLL writers in different processes.
#ifdef _DEBUG
#include <cstdio>
inline void TsfLogImpl(const wchar_t* fmt, ...) {
    wchar_t buf[640];
    int prefixLen = _snwprintf_s(buf, 640, _TRUNCATE, L"[NexusKey:%u] ",
                                 GetCurrentProcessId());
    if (prefixLen < 0) prefixLen = 0;  // _TRUNCATE return: -1 on truncation
    va_list args;
    va_start(args, fmt);
    int bodyLen = _vsnwprintf_s(buf + prefixLen, 640 - prefixLen, _TRUNCATE,
                                 fmt, args);
    va_end(args);
    if (bodyLen < 0) bodyLen = 640 - prefixLen - 2;  // truncated — fill to room for \n
    int totalLen = prefixLen + bodyLen;
    if (totalLen < 639) {
        buf[totalLen]     = L'\n';
        buf[totalLen + 1] = L'\0';
    }
    OutputDebugStringW(buf);
}
#define TSF_LOG(...) TsfLogImpl(__VA_ARGS__)
#else
#define TSF_LOG(...) ((void)0)
#endif
