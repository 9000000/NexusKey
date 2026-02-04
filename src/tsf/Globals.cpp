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

// {E95E2F6C-9G3D-5C2B-AE4F-7G8B9CAD1E2F}
// NexusKey Profile GUID (Vietnamese)
DEFINE_GUID(GUID_Profile,
    0xE95E2F6C, 0x9A3D, 0x5C2B, 0xAE, 0x4F, 0x7A, 0x8B, 0x9C, 0xAD, 0x1E, 0x2F);

// {E5B5E9F1-7A3B-4C2D-9E8F-1A2B3C4D5E6F}
// Display Attribute GUID (invisible - no underline/highlight)
const GUID GUID_DisplayAttribute_Input =
    { 0xe5b5e9f1, 0x7a3b, 0x4c2d, { 0x9e, 0x8f, 0x1a, 0x2b, 0x3c, 0x4d, 0x5e, 0x6f } };

}  // namespace TSF
}  // namespace NextKey
