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

// Profile GUID as string for registry checks (matches GUID_Profile in Globals.cpp)
// {2FE17DA4-D8E2-4B28-8566-C30E8F04BFD4}
constexpr const wchar_t* GUID_PROFILE_STRING = L"{2FE17DA4-D8E2-4B28-8566-C30E8F04BFD4}";


// Register the profile under English (United States), 0x0409. VKey's engine assumes a US
// QWERTY base and produces Vietnamese itself via Telex/VNI — it does not need the stock
// Vietnamese layout (KBDVNTC, which remaps the digit row to ăâêô). 0x0409 is in EVERY
// user's input list by default, so ITfInputProcessorProfileMgr::ActivateProfile succeeds
// for every user — including a NON-ADMIN user whose session never added Vietnamese to its
// Preferred Languages.
//
// History (do NOT re-weld this to 0x042A): we briefly registered under Vietnamese (0x042A,
// 2026-06-24) only so the input indicator pill reads "VIE" instead of "ENG". That cosmetic
// win cost real function — issue #209:
//   * Non-admin: ActivateProfile fails E_FAIL (0x80004005) because 0x042A is not in the
//     user's input list, so the VKey TIP never activates → no Vietnamese in TSF apps.
//   * Win+Space shows TWO "VIE" entries (Windows synthesises a base entry for a TIP under a
//     non-preferred language; the US SubstituteLayout does not collapse it).
//   * Digit row / = / [ ] type ăâêô… unless the machine-wide 0000042a→KBDUS.DLL reg hack
//     (tools/vie-layout-hack/, admin-only) is applied.
// Reverting to 0x0409 fixes all three at once. The tray V/E icon is the real V/E indicator,
// so the "ENG" pill is acceptable; tools/vie-layout-hack/ is now OBSOLETE (no 0x042A layout
// to override). The US SubstituteLayout in RegisterTIP() is harmless-but-redundant under
// 0x0409 (US is already the base) and is kept only to avoid an untested registration change.
constexpr LANGID TEXTSERVICE_LANGID = MAKELANGID(LANG_ENGLISH, SUBLANG_ENGLISH_US);  // 0x0409
constexpr ULONG TEXTSERVICE_ICON_INDEX = 0;

}  // namespace TSF
}  // namespace NextKey
