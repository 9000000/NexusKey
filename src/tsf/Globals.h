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
extern const GUID GUID_LangBarItem_Toggle;

// Module instance handle
extern HINSTANCE g_hInstance;

// DLL reference count
extern LONG g_dllRefCount;

// String constants
constexpr const wchar_t* TEXT_SERVICE_DESCRIPTION = L"NexusKey Vietnamese IME";

// CLSID as string for registry checks (matches CLSID_TextService in Globals.cpp)
// {D84D1E5B-8F2C-4B1A-9D3E-6F7A8B9C0D1E}
constexpr const wchar_t* CLSID_TEXTSERVICE_STRING = L"{D84D1E5B-8F2C-4B1A-9D3E-6F7A8B9C0D1E}";
// Use English keyboard as base - NexusKey handles Vietnamese conversion via Telex
constexpr LANGID TEXTSERVICE_LANGID = MAKELANGID(LANG_ENGLISH, SUBLANG_ENGLISH_US);
constexpr ULONG TEXTSERVICE_ICON_INDEX = 0;

}  // namespace TSF
}  // namespace NextKey
