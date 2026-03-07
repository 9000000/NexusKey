// NexusKey - TSF Registration & Diagnostics
// SPDX-License-Identifier: GPL-3.0-only

// Windows/COM headers MUST come first — <msctf.h> includes <comcat.h>
// which requires COM base types from <ole2.h>/<objbase.h>
#include <Windows.h>
#include <ole2.h>
#include <msctf.h>

#include "TsfRegistration.h"
#include "core/ipc/SharedState.h"
#include "core/ipc/SharedStateManager.h"
#include "core/Debug.h"
#include "tsf/Globals.h"
#include <vector>

namespace NextKey {

using DllRegisterServerFn = HRESULT(STDAPICALLTYPE*)();

std::wstring GetTsfDllPath() {
    wchar_t exePath[MAX_PATH];
    GetModuleFileNameW(nullptr, exePath, MAX_PATH);
    std::wstring path(exePath);
    size_t pos = path.find_last_of(L"\\/");
    if (pos != std::wstring::npos) {
        path = path.substr(0, pos + 1);
    }
    return path + L"NextKeyTSF.dll";
}

bool IsTsfRegistered() {
    std::wstring keyPath = L"CLSID\\";
    keyPath += TSF::CLSID_TEXTSERVICE_STRING;

    HKEY hKey;
    LSTATUS ls = RegOpenKeyExW(HKEY_CLASSES_ROOT, keyPath.c_str(), 0, KEY_READ, &hKey);
    if (ls == ERROR_SUCCESS) {
        RegCloseKey(hKey);
        return true;
    }
    return false;
}

bool RegisterTsf() {
    std::wstring dllPath = GetTsfDllPath();
    NEXTKEY_LOG(L"RegisterTsf: loading DLL from '%s'", dllPath.c_str());

    HMODULE hDll = LoadLibraryW(dllPath.c_str());
    if (!hDll) {
        NEXTKEY_LOG(L"RegisterTsf: failed to load NextKeyTSF.dll (error: %lu)", GetLastError());
        return false;
    }

    auto pRegister = reinterpret_cast<DllRegisterServerFn>(
        GetProcAddress(hDll, "DllRegisterServer")
    );

    bool success = false;
    if (pRegister) {
        HRESULT hr = pRegister();
        success = SUCCEEDED(hr);
        if (success) {
            NEXTKEY_LOG(L"RegisterTsf: DllRegisterServer succeeded");
        } else {
            NEXTKEY_LOG(L"RegisterTsf: DllRegisterServer failed (hr: 0x%08X)", hr);
        }
    } else {
        NEXTKEY_LOG(L"RegisterTsf: DllRegisterServer export not found");
    }

    FreeLibrary(hDll);
    return success;
}

bool UnregisterTsf() {
    std::wstring dllPath = GetTsfDllPath();

    HMODULE hDll = LoadLibraryW(dllPath.c_str());
    if (!hDll) {
        NEXTKEY_LOG(L"Failed to load NextKeyTSF.dll for unregister");
        return false;
    }

    auto pUnregister = reinterpret_cast<DllRegisterServerFn>(
        GetProcAddress(hDll, "DllUnregisterServer")
    );

    bool success = false;
    if (pUnregister) {
        HRESULT hr = pUnregister();
        success = SUCCEEDED(hr);
        if (success) {
            NEXTKEY_LOG(L"TSF unregistered successfully");
        } else {
            NEXTKEY_LOG(L"TSF unregister failed (hr: 0x%08X)", hr);
        }
    }

    FreeLibrary(hDll);
    return success;
}

bool RegisterTsfElevated() {
    wchar_t exePath[MAX_PATH];
    GetModuleFileNameW(nullptr, exePath, MAX_PATH);
    NEXTKEY_LOG(L"RegisterTsfElevated: launching '%s --register-tsf' with elevation", exePath);

    SHELLEXECUTEINFOW sei = { sizeof(sei) };
    sei.lpVerb = L"runas";
    sei.lpFile = exePath;
    sei.lpParameters = L"--register-tsf";
    sei.nShow = SW_HIDE;
    sei.fMask = SEE_MASK_NOCLOSEPROCESS;

    if (ShellExecuteExW(&sei)) {
        if (sei.hProcess) {
            WaitForSingleObject(sei.hProcess, 10000);
            CloseHandle(sei.hProcess);
        }
        bool registered = IsTsfRegistered();
        NEXTKEY_LOG(L"RegisterTsfElevated: %s", registered ? L"succeeded" : L"failed");
        return registered;
    }
    NEXTKEY_LOG(L"RegisterTsfElevated: ShellExecuteExW failed (error: %lu)", GetLastError());
    return false;
}

bool UnregisterTsfElevated() {
    wchar_t exePath[MAX_PATH];
    GetModuleFileNameW(nullptr, exePath, MAX_PATH);

    SHELLEXECUTEINFOW sei = { sizeof(sei) };
    sei.lpVerb = L"runas";
    sei.lpFile = exePath;
    sei.lpParameters = L"--unregister-tsf";
    sei.nShow = SW_HIDE;
    sei.fMask = SEE_MASK_NOCLOSEPROCESS;

    if (ShellExecuteExW(&sei)) {
        if (sei.hProcess) {
            WaitForSingleObject(sei.hProcess, 10000);
            CloseHandle(sei.hProcess);
        }
        return !IsTsfRegistered();
    }
    return false;
}

void RunDiagnostics() {
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

    std::wstring out;
    out += L"=== NexusKey Diagnostics ===\n\n";

    // 1. TSF Registration check
    out += IsTsfRegistered() ? L"[OK] TSF registered\n" : L"[FAIL] TSF NOT registered\n";

    // 2. Enumerate all keyboard layouts (HKLs)
    out += L"\n--- Keyboard Layouts (HKLs) ---\n";
    int count = GetKeyboardLayoutList(0, nullptr);
    if (count > 0) {
        std::vector<HKL> hklList(count);
        GetKeyboardLayoutList(count, hklList.data());
        for (int i = 0; i < count; i++) {
            auto hklVal = reinterpret_cast<DWORD_PTR>(hklList[i]);
            bool isTip = (HIWORD(hklVal) >= 0xF000);
            wchar_t buf[128];
            swprintf_s(buf, L"  HKL[%d] = 0x%08IX  LOWORD=0x%04X  HIWORD=0x%04X  %s\n",
                        i, hklVal, LOWORD(hklVal), HIWORD(hklVal),
                        isTip ? L"(TIP substitute)" : L"(keyboard layout)");
            out += buf;
        }
    } else {
        out += L"  (none found)\n";
    }

    // 3. Current thread HKL
    {
        HKL cur = GetKeyboardLayout(0);
        wchar_t buf[128];
        swprintf_s(buf, L"\nCurrent thread HKL: 0x%08IX\n",
                    reinterpret_cast<DWORD_PTR>(cur));
        out += buf;
    }

    // 4. Foreground thread HKL
    {
        HWND fg = GetForegroundWindow();
        if (fg) {
            DWORD tid = GetWindowThreadProcessId(fg, nullptr);
            HKL fgHkl = GetKeyboardLayout(tid);
            wchar_t buf[128];
            swprintf_s(buf, L"Foreground thread HKL: 0x%08IX (tid=%lu)\n",
                        reinterpret_cast<DWORD_PTR>(fgHkl), tid);
            out += buf;
        }
    }

    // 5. TSF Active Profile
    out += L"\n--- TSF Active Profile ---\n";
    ITfInputProcessorProfileMgr* pProfileMgr = nullptr;
    HRESULT hr = CoCreateInstance(
        CLSID_TF_InputProcessorProfiles, nullptr, CLSCTX_INPROC_SERVER,
        IID_ITfInputProcessorProfileMgr,
        reinterpret_cast<void**>(&pProfileMgr));
    if (SUCCEEDED(hr) && pProfileMgr) {
        TF_INPUTPROCESSORPROFILE activeProfile = {};
        hr = pProfileMgr->GetActiveProfile(GUID_TFCAT_TIP_KEYBOARD, &activeProfile);
        if (SUCCEEDED(hr)) {
            wchar_t buf[256];
            swprintf_s(buf, L"  Type=%lu  LangID=0x%04X  HKL=0x%08IX\n",
                        activeProfile.dwProfileType, activeProfile.langid,
                        reinterpret_cast<DWORD_PTR>(activeProfile.hkl));
            out += buf;

            // Check CLSID
            wchar_t clsidStr[64];
            StringFromGUID2(activeProfile.clsid, clsidStr, 64);
            out += L"  CLSID=";
            out += clsidStr;
            out += L"\n";

            // NexusKey CLSID for comparison
            static const GUID CLSID_NK = {
                0xD84D1E5B, 0x8F2C, 0x4B1A,
                {0x9D, 0x3E, 0x6F, 0x7A, 0x8B, 0x9C, 0x0D, 0x1E}
            };
            out += IsEqualCLSID(activeProfile.clsid, CLSID_NK)
                ? L"  → This IS NexusKey\n"
                : L"  → This is NOT NexusKey\n";
        } else {
            out += L"  GetActiveProfile failed\n";
        }

        // 6. Enumerate all profiles for 0x0409
        out += L"\n--- All 0x0409 Profiles ---\n";
        IEnumTfInputProcessorProfiles* pEnum = nullptr;
        hr = pProfileMgr->EnumProfiles(0x0409, &pEnum);
        if (SUCCEEDED(hr) && pEnum) {
            TF_INPUTPROCESSORPROFILE profile;
            ULONG fetched = 0;
            int idx = 0;
            while (pEnum->Next(1, &profile, &fetched) == S_OK && fetched == 1) {
                wchar_t clsidStr2[64];
                StringFromGUID2(profile.clsid, clsidStr2, 64);
                wchar_t buf2[256];
                swprintf_s(buf2, L"  [%d] type=%lu  hkl=0x%08IX  clsid=%s\n",
                            idx++, profile.dwProfileType,
                            reinterpret_cast<DWORD_PTR>(profile.hkl), clsidStr2);
                out += buf2;
            }
            pEnum->Release();
        }

        pProfileMgr->Release();
    } else {
        out += L"  Failed to create ITfInputProcessorProfileMgr\n";
    }

    // 7. SharedState check
    out += L"\n--- SharedState ---\n";
    {
        SharedStateManager sm;
        if (sm.Open()) {
            SharedState state = sm.Read();
            if (state.IsValid()) {
                wchar_t buf[256];
                swprintf_s(buf, L"  magic=0x%08X  epoch=%u  flags=0x%08X\n"
                                L"  VIETNAMESE_MODE=%d  ENGINE_ENABLED=%d\n"
                                L"  inputMethod=%d  spellCheck=%d\n",
                            state.magic, state.epoch, state.flags,
                            (state.flags & SharedFlags::VIETNAMESE_MODE) ? 1 : 0,
                            (state.flags & SharedFlags::ENGINE_ENABLED) ? 1 : 0,
                            state.inputMethod, state.spellCheck);
                out += buf;
            } else {
                out += L"  SharedState invalid (magic mismatch)\n";
            }
        } else {
            out += L"  SharedState not available (EXE not running?)\n";
        }
    }

    CoUninitialize();
    MessageBoxW(nullptr, out.c_str(), L"NexusKey Diagnostics", MB_OK | MB_ICONINFORMATION);
}

}  // namespace NextKey
