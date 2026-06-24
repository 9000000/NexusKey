// VKey - TSF DLL Registration
// SPDX-License-Identifier: AGPL-3.0-only

#include "stdafx.h"
#include "Globals.h"
#include <strsafe.h>

namespace NextKey {
namespace TSF {

// Registry helper functions

[[nodiscard]] static bool HasHklmWriteAccess() noexcept {
    HKEY hKey = nullptr;
    LSTATUS ls = RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"Software\\Classes", 0, KEY_WRITE, &hKey);
    if (ls == ERROR_SUCCESS) {
        RegCloseKey(hKey);
        return true;
    }
    return false;
}

static HRESULT RegisterCLSID(bool useHklm) {
    wchar_t szModule[MAX_PATH];
    if (!GetModuleFileNameW(g_hInstance, szModule, MAX_PATH)) {
        return E_FAIL;
    }

    wchar_t szKey[256];
    StringCchPrintfW(szKey, 256, L"CLSID\\{%08X-%04X-%04X-%02X%02X-%02X%02X%02X%02X%02X%02X}",
        CLSID_TextService.Data1, CLSID_TextService.Data2, CLSID_TextService.Data3,
        CLSID_TextService.Data4[0], CLSID_TextService.Data4[1],
        CLSID_TextService.Data4[2], CLSID_TextService.Data4[3],
        CLSID_TextService.Data4[4], CLSID_TextService.Data4[5],
        CLSID_TextService.Data4[6], CLSID_TextService.Data4[7]);

    HKEY hRoot = useHklm ? HKEY_CLASSES_ROOT : HKEY_CURRENT_USER;
    
    wchar_t szFullKey[300];
    if (useHklm) {
        StringCchCopyW(szFullKey, 300, szKey);
    } else {
        StringCchPrintfW(szFullKey, 300, L"Software\\Classes\\%s", szKey);
    }

    HKEY hKey;
    DWORD dwDisp;
    LSTATUS ls = RegCreateKeyExW(hRoot, szFullKey, 0, nullptr, 
        REG_OPTION_NON_VOLATILE, KEY_WRITE, nullptr, &hKey, &dwDisp);
    if (ls != ERROR_SUCCESS) return HRESULT_FROM_WIN32(ls);

    ls = RegSetValueExW(hKey, nullptr, 0, REG_SZ,
        (const BYTE*)TEXT_SERVICE_DESCRIPTION, 
        (lstrlenW(TEXT_SERVICE_DESCRIPTION) + 1) * sizeof(wchar_t));
    if (ls != ERROR_SUCCESS) { RegCloseKey(hKey); return HRESULT_FROM_WIN32(ls); }
    RegCloseKey(hKey);

    // Register InprocServer32
    wchar_t szInproc[400];
    if (useHklm) {
        StringCchPrintfW(szInproc, 400, L"%s\\InprocServer32", szKey);
    } else {
        StringCchPrintfW(szInproc, 400, L"Software\\Classes\\%s\\InprocServer32", szKey);
    }

    ls = RegCreateKeyExW(hRoot, szInproc, 0, nullptr,
        REG_OPTION_NON_VOLATILE, KEY_WRITE, nullptr, &hKey, &dwDisp);
    if (ls != ERROR_SUCCESS) return HRESULT_FROM_WIN32(ls);

    ls = RegSetValueExW(hKey, nullptr, 0, REG_SZ,
        (const BYTE*)szModule, (lstrlenW(szModule) + 1) * sizeof(wchar_t));
    if (ls != ERROR_SUCCESS) { RegCloseKey(hKey); return HRESULT_FROM_WIN32(ls); }

    const wchar_t* szThreadingModel = L"Apartment";
    ls = RegSetValueExW(hKey, L"ThreadingModel", 0, REG_SZ,
        (const BYTE*)szThreadingModel, (lstrlenW(szThreadingModel) + 1) * sizeof(wchar_t));
    if (ls != ERROR_SUCCESS) { RegCloseKey(hKey); return HRESULT_FROM_WIN32(ls); }
    RegCloseKey(hKey);

    return S_OK;
}

static HRESULT UnregisterCLSID() {
    wchar_t szKey[256];
    StringCchPrintfW(szKey, 256, L"CLSID\\{%08X-%04X-%04X-%02X%02X-%02X%02X%02X%02X%02X%02X}",
        CLSID_TextService.Data1, CLSID_TextService.Data2, CLSID_TextService.Data3,
        CLSID_TextService.Data4[0], CLSID_TextService.Data4[1],
        CLSID_TextService.Data4[2], CLSID_TextService.Data4[3],
        CLSID_TextService.Data4[4], CLSID_TextService.Data4[5],
        CLSID_TextService.Data4[6], CLSID_TextService.Data4[7]);

    // Try deleting from HKLM (via HKEY_CLASSES_ROOT)
    wchar_t szInproc[300];
    StringCchPrintfW(szInproc, 300, L"%s\\InprocServer32", szKey);
    RegDeleteKeyW(HKEY_CLASSES_ROOT, szInproc);
    RegDeleteKeyW(HKEY_CLASSES_ROOT, szKey);

    // Try deleting from HKCU (via HKEY_CURRENT_USER\Software\Classes)
    wchar_t szHkcuInproc[400];
    StringCchPrintfW(szHkcuInproc, 400, L"Software\\Classes\\%s\\InprocServer32", szKey);
    RegDeleteKeyW(HKEY_CURRENT_USER, szHkcuInproc);

    wchar_t szHkcuClsid[400];
    StringCchPrintfW(szHkcuClsid, 400, L"Software\\Classes\\%s", szKey);
    RegDeleteKeyW(HKEY_CURRENT_USER, szHkcuClsid);

    return S_OK;
}

static HRESULT RegisterTIP() {
    ITfInputProcessorProfiles* pProfiles = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_TF_InputProcessorProfiles, nullptr,
        CLSCTX_INPROC_SERVER, IID_ITfInputProcessorProfiles, (void**)&pProfiles);
    if (FAILED(hr)) return hr;

    hr = pProfiles->Register(CLSID_TextService);
    if (FAILED(hr)) {
        pProfiles->Release();
        return hr;
    }

    // Declare a US substitute keyboard layout for VKey's profile. This was attempted as a
    // way to MERGE the duplicate "VIE" switcher entry (a TIP that owns a substitute should
    // replace the language's base layout, like MS's JP/KO IMEs). VERDICT 2026-06-24: it does
    // NOT merge on tested Win11 — the substitute is written correctly (SubstituteLayout=
    // 0x04090409, Enable=1) but Windows still shows a 2nd base entry under the VIE group.
    // See Globals.h::TEXTSERVICE_LANGID — the 2nd entry is an accepted limitation. Keep this
    // call anyway: it gives VKey's own entry a US physical base (per-TIP, scoped to VKey,
    // complementing the machine-wide vie-layout-hack). Do NOT re-attempt the merge here.
    //
    // The substitute HKL MUST be a *loaded* layout — passing a raw/unloaded HKL is why an
    // earlier RegisterProfile attempt E_FAILed (0x80004005) inside regsvr32. Load US first.
    // KLF_NOTELLSHELL keeps it off the user's active-layout indicator.
    HKL usHkl = LoadKeyboardLayoutW(L"00000409", KLF_NOTELLSHELL | KLF_SUBSTITUTE_OK);

    // Prefer the modern ProfileMgr::RegisterProfile so the substitute is declared ATOMICALLY
    // at registration (the documented IME pattern), not bolted on afterwards.
    bool profileRegistered = false;
    if (usHkl) {
        ITfInputProcessorProfileMgr* pProfileMgr = nullptr;
        if (SUCCEEDED(CoCreateInstance(CLSID_TF_InputProcessorProfiles, nullptr,
                CLSCTX_INPROC_SERVER, IID_ITfInputProcessorProfileMgr,
                reinterpret_cast<void**>(&pProfileMgr))) && pProfileMgr) {
            HRESULT hrReg = pProfileMgr->RegisterProfile(
                CLSID_TextService,
                TEXTSERVICE_LANGID,
                GUID_Profile,
                TEXT_SERVICE_DESCRIPTION,
                static_cast<ULONG>(wcslen(TEXT_SERVICE_DESCRIPTION)),
                nullptr, 0,
                TEXTSERVICE_ICON_INDEX,
                usHkl,   // hklsubstitute — US physical base; collapses the switcher entry
                0,       // dwPreferredLayout
                TRUE,    // bEnabledByDefault
                0);      // dwFlags
            pProfileMgr->Release();
            profileRegistered = SUCCEEDED(hrReg);
            OutputDebugStringW(profileRegistered
                ? L"RegisterTIP: RegisterProfile(+US substitute) OK\n"
                : L"RegisterTIP: RegisterProfile failed, falling back to AddLanguageProfile\n");
        }
    }

    // Fallback: legacy AddLanguageProfile if the ProfileMgr path is unavailable or failed,
    // so registration still succeeds even when the merge attempt can't run. Best-effort
    // SubstituteKeyboardLayout afterwards (historically unreliable, but harmless to try).
    if (!profileRegistered) {
        hr = pProfiles->AddLanguageProfile(
            CLSID_TextService,
            TEXTSERVICE_LANGID,
            GUID_Profile,
            TEXT_SERVICE_DESCRIPTION,
            static_cast<ULONG>(wcslen(TEXT_SERVICE_DESCRIPTION)),
            nullptr, 0,
            TEXTSERVICE_ICON_INDEX
        );
        if (FAILED(hr)) {
            pProfiles->Release();
            return hr;
        }
        if (usHkl) {
            (void)pProfiles->SubstituteKeyboardLayout(  // best-effort; merge known not honored
                CLSID_TextService, TEXTSERVICE_LANGID, GUID_Profile, usHkl);
        }
    }

    // The system-wide tools/vie-layout-hack/ (0000042a -> KBDUS.DLL) remains as the
    // belt-and-braces fallback for the *physical* US map if Windows ignores the substitute.

    // Enable the profile so it appears in the language bar / input indicator
    hr = pProfiles->EnableLanguageProfile(
        CLSID_TextService,
        TEXTSERVICE_LANGID,
        GUID_Profile,
        TRUE
    );

    pProfiles->Release();
    return hr;
}

static HRESULT UnregisterTIP() {
    ITfInputProcessorProfiles* pProfiles = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_TF_InputProcessorProfiles, nullptr,
        CLSCTX_INPROC_SERVER, IID_ITfInputProcessorProfiles, (void**)&pProfiles);
    if (FAILED(hr)) return hr;

    hr = pProfiles->Unregister(CLSID_TextService);
    pProfiles->Release();
    return hr;
}

static HRESULT RegisterCategory() {
    ITfCategoryMgr* pCategoryMgr = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_TF_CategoryMgr, nullptr,
        CLSCTX_INPROC_SERVER, IID_ITfCategoryMgr, (void**)&pCategoryMgr);
    if (FAILED(hr)) return hr;

    // Register as keyboard TIP
    hr = pCategoryMgr->RegisterCategory(
        CLSID_TextService,
        GUID_TFCAT_TIP_KEYBOARD,
        CLSID_TextService
    );

    pCategoryMgr->Release();
    return hr;
}

static HRESULT UnregisterCategory() {
    ITfCategoryMgr* pCategoryMgr = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_TF_CategoryMgr, nullptr,
        CLSCTX_INPROC_SERVER, IID_ITfCategoryMgr, (void**)&pCategoryMgr);
    if (FAILED(hr)) return hr;

    hr = pCategoryMgr->UnregisterCategory(
        CLSID_TextService,
        GUID_TFCAT_TIP_KEYBOARD,
        CLSID_TextService
    );

    pCategoryMgr->Release();
    return hr;
}

static void CleanupHkcuClsidOverride() noexcept {
    wchar_t keyPath[256];
    swprintf_s(keyPath, L"Software\\Classes\\CLSID\\%s", CLSID_TEXTSERVICE_STRING);

    // Only clean up HKCU override if the TSF DLL is registered in HKLM
    HKEY hKeyHklm = nullptr;
    LSTATUS lsHklm = RegOpenKeyExW(HKEY_LOCAL_MACHINE, keyPath, 0, KEY_READ, &hKeyHklm);
    if (lsHklm == ERROR_SUCCESS) {
        RegCloseKey(hKeyHklm);
        RegDeleteTreeW(HKEY_CURRENT_USER, keyPath);  // No-op if key absent (ERROR_FILE_NOT_FOUND)
    }
}

}  // namespace TSF
}  // namespace NextKey

// DLL exports for registration
extern "C" {

STDAPI DllRegisterServer() {
    using namespace NextKey::TSF;

    bool useHklm = HasHklmWriteAccess();

    if (useHklm) {
        // Clean up any HKCU override first to ensure HKLM registration takes effect
        CleanupHkcuClsidOverride();
    }

    HRESULT hr = RegisterCLSID(useHklm);
    if (FAILED(hr)) return hr;

    hr = RegisterTIP();
    if (FAILED(hr)) {
        UnregisterCLSID();  // Clean up partial state
        return hr;
    }

    hr = RegisterCategory();
    if (FAILED(hr)) {
        UnregisterTIP();
        UnregisterCLSID();
        return hr;
    }

    return S_OK;
}

STDAPI DllUnregisterServer() {
    using namespace NextKey::TSF;

    UnregisterCategory();
    UnregisterTIP();
    UnregisterCLSID();

    return S_OK;
}

}  // extern "C"
