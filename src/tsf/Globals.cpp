// NexusKey - TSF Globals Implementation
// SPDX-License-Identifier: GPL-3.0-only

#include "stdafx.h"
#include "Globals.h"
#include <initguid.h>

namespace NextKey {
namespace TSF {

// Module instance
HINSTANCE g_hInstance = nullptr;

// DLL reference count
LONG g_dllRefCount = 0;

// {D84D1E5B-8F2C-4B1A-9D3E-6F7A8B9C0D1E}
// NexusKey Text Service CLSID
DEFINE_GUID(CLSID_TextService,
    0xD84D1E5B, 0x8F2C, 0x4B1A, 0x9D, 0x3E, 0x6F, 0x7A, 0x8B, 0x9C, 0x0D, 0x1E);

// {E95E2F6C-9A3D-5C2B-AE4F-7A8B9CAD1E2F}
// NexusKey Profile GUID (Vietnamese)
DEFINE_GUID(GUID_Profile,
    0xE95E2F6C, 0x9A3D, 0x5C2B, 0xAE, 0x4F, 0x7A, 0x8B, 0x9C, 0xAD, 0x1E, 0x2F);

// {E5B5E9F1-7A3B-4C2D-9E8F-1A2B3C4D5E6F}
// Display Attribute GUID (invisible - no underline/highlight)
const GUID GUID_DisplayAttribute_Input =
    { 0xe5b5e9f1, 0x7a3b, 0x4c2d, { 0x9e, 0x8f, 0x1a, 0x2b, 0x3c, 0x4d, 0x5e, 0x6f } };

// {2C77A81E-41CC-4178-A3A7-5F8A987568E1}
// Standard TSF input mode indicator — Windows shows this in the modern input indicator tray
const GUID GUID_LangBarItem_Toggle =
    { 0x2c77a81e, 0x41cc, 0x4178, { 0xa3, 0xa7, 0x5f, 0x8a, 0x98, 0x75, 0x68, 0xe1 } };

}  // namespace TSF
}  // namespace NextKey
