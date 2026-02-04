// NexusKey - TSF Global Definitions
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <Windows.h>
#include <msctf.h>
#include <string>

namespace NextKey {
namespace TSF {

// GUIDs - will be defined in Globals.cpp
extern const GUID CLSID_TextService;
extern const GUID GUID_Profile;
extern const GUID GUID_DisplayAttribute_Input;

// Module instance handle
extern HINSTANCE g_hInstance;

// DLL reference count
extern LONG g_dllRefCount;

// String constants
constexpr const wchar_t* TEXT_SERVICE_DESCRIPTION = L"NexusKey Vietnamese IME";
// Use English keyboard as base - NexusKey handles Vietnamese conversion via Telex
constexpr LANGID TEXTSERVICE_LANGID = MAKELANGID(LANG_ENGLISH, SUBLANG_ENGLISH_US);
constexpr ULONG TEXTSERVICE_ICON_INDEX = 0;

}  // namespace TSF
}  // namespace NextKey
