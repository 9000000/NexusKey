// NexusKey - TSF DLL Entry Point
// SPDX-License-Identifier: GPL-3.0-only

#include "stdafx.h"
#include "Globals.h"
#include "TextService.h"
#include "ComUtils.h"

namespace NextKey {
namespace TSF {

// Global class factory
static TextServiceFactory g_classFactory;

}  // namespace TSF
}  // namespace NextKey

BOOL WINAPI DllMain(HINSTANCE hInstance, DWORD dwReason, LPVOID /*lpReserved*/) {
    switch (dwReason) {
        case DLL_PROCESS_ATTACH:
            NextKey::TSF::g_hInstance = hInstance;
            DisableThreadLibraryCalls(hInstance);
            break;

        case DLL_PROCESS_DETACH:
            break;
    }
    return TRUE;
}

extern "C" {

STDAPI DllGetClassObject(REFCLSID rclsid, REFIID riid, void** ppv) {
    if (ppv == nullptr) return E_POINTER;
    *ppv = nullptr;

    if (!IsEqualCLSID(rclsid, NextKey::TSF::CLSID_TextService)) {
        return CLASS_E_CLASSNOTAVAILABLE;
    }

    return NextKey::TSF::g_classFactory.QueryInterface(riid, ppv);
}

STDAPI DllCanUnloadNow() {
    return (NextKey::TSF::g_dllRefCount == 0) ? S_OK : S_FALSE;
}

}  // extern "C"
