// NexusKey - Version Information (Single Source of Truth)
// SPDX-License-Identifier: GPL-3.0-only
//
// Uses #define (not constexpr) because RC files need preprocessor macros.

#pragma once

#define NEXUSKEY_VERSION_MAJOR 1
#define NEXUSKEY_VERSION_MINOR 0
#define NEXUSKEY_VERSION_PATCH 0
#define NEXUSKEY_VERSION_STR   "1.0.0"
#define NEXUSKEY_VERSION_WSTR  L"1.0.0"
#define NEXUSKEY_VERSION_PACKED ((NEXUSKEY_VERSION_MAJOR << 16) | (NEXUSKEY_VERSION_MINOR << 8) | NEXUSKEY_VERSION_PATCH)
#define NEXUSKEY_VERSION_RC    NEXUSKEY_VERSION_MAJOR,NEXUSKEY_VERSION_MINOR,NEXUSKEY_VERSION_PATCH,0
