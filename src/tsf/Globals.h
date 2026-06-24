// VKey - TSF Global Definitions
// SPDX-License-Identifier: AGPL-3.0-only

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
constexpr const wchar_t* TEXT_SERVICE_DESCRIPTION = L"VKey Vietnamese IME";

// CLSID as string for registry checks (matches CLSID_TextService in Globals.cpp)
// {DEB18BD1-2331-4F2A-B030-DA9EB0093683}
constexpr const wchar_t* CLSID_TEXTSERVICE_STRING = L"{DEB18BD1-2331-4F2A-B030-DA9EB0093683}";

// Register the profile under Vietnamese so the language indicator shows "VIE - VKey"
// (not "ENG"). VKey's engine assumes a US QWERTY base and produces Vietnamese itself via
// Telex/VNI, so the stock Vietnamese keyboard layout (KBDVNTC — digit row remapped to
// ăâêô) must NOT be used as the physical key map.
//
// ACCEPTED LIMITATION — Win+Space shows TWO "VIE" entries (decided 2026-06-24, do NOT
// re-investigate). The VIE pill is welded to langid 0x042A: any TIP enabled under a
// language that is NOT in the user's Preferred Languages gets Windows to synthesise a
// second "base" entry next to it. RegisterTIP() declares a US SubstituteLayout (verified
// written: SubstituteLayout=0x04090409) — Windows still does NOT collapse the two entries,
// so the substitute is kept only to give VKey's own entry a US physical base. There is no
// way from VKey to get "single entry + VIE pill" on this Windows; the only single-entry
// option is reverting to 0x0409 (ENG pill), rejected because the VKey tray V/E icon is the
// real indicator and the owner wants the VIE label. The 2nd entry is harmless (selecting it
// = plain US, no Telex). The "0000042a → KBDUS.DLL" hack in tools/vie-layout-hack/ remains
// the belt-and-braces for the physical US map.
constexpr LANGID TEXTSERVICE_LANGID = MAKELANGID(LANG_VIETNAMESE, SUBLANG_DEFAULT);  // 0x042A
constexpr ULONG TEXTSERVICE_ICON_INDEX = 0;

}  // namespace TSF
}  // namespace NextKey
