// NexusKey - Startup Helper (Windows-only)
// SPDX-License-Identifier: GPL-3.0-only
//
// Manages run-on-startup registration via Registry (normal) or
// Task Scheduler (admin/elevated). Pattern from OpenKey.

#pragma once

#ifdef _WIN32
#include <Windows.h>
#include <ShlObj.h>
#include <shellapi.h>
#include <shobjidl.h>
#include <string>

namespace NextKey {

/// Registry key path for user-level startup programs
inline constexpr const wchar_t* STARTUP_REG_KEY = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
inline constexpr const wchar_t* STARTUP_REG_VALUE = L"NexusKey";
inline constexpr const wchar_t* STARTUP_TASK_NAME = L"NexusKey";

/// Check if the current process is running with admin privileges
[[nodiscard]] inline bool IsRunningAsAdmin() noexcept {
    return IsUserAnAdmin() != FALSE;
}

/// Get the full path to the current executable (quoted)
[[nodiscard]] inline std::wstring GetQuotedExePath() {
    wchar_t path[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, path, MAX_PATH);
    return L"\"" + std::wstring(path) + L"\"";
}

/// Remove the registry startup entry (HKCU\...\Run)
inline void RemoveRegistryStartup() noexcept {
    HKEY hKey = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, STARTUP_REG_KEY, 0, KEY_SET_VALUE, &hKey) == ERROR_SUCCESS) {
        RegDeleteValueW(hKey, STARTUP_REG_VALUE);
        RegCloseKey(hKey);
    }
}

/// Remove the scheduled task (requires elevation for /rl highest tasks)
inline void RemoveScheduledTask() noexcept {
    // Build command: schtasks /delete /tn NexusKey /f
    wchar_t args[256];
    swprintf_s(args, L"/delete /tn %s /f", STARTUP_TASK_NAME);

    SHELLEXECUTEINFOW sei = { sizeof(sei) };
    sei.lpVerb = L"runas";
    sei.lpFile = L"schtasks";
    sei.lpParameters = args;
    sei.nShow = SW_HIDE;
    sei.fMask = SEE_MASK_NOCLOSEPROCESS;

    if (ShellExecuteExW(&sei)) {
        if (sei.hProcess) {
            WaitForSingleObject(sei.hProcess, 5000);
            CloseHandle(sei.hProcess);
        }
    }
}

/// Set the registry startup entry (HKCU\...\Run)
inline bool SetRegistryStartup() noexcept {
    HKEY hKey = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, STARTUP_REG_KEY, 0, KEY_SET_VALUE, &hKey) != ERROR_SUCCESS) {
        return false;
    }

    std::wstring exePath = GetQuotedExePath();
    LSTATUS status = RegSetValueExW(
        hKey, STARTUP_REG_VALUE, 0, REG_SZ,
        reinterpret_cast<const BYTE*>(exePath.c_str()),
        static_cast<DWORD>((exePath.size() + 1) * sizeof(wchar_t))
    );
    RegCloseKey(hKey);
    return status == ERROR_SUCCESS;
}

/// Create a scheduled task to run at logon with highest privileges (UAC prompt)
inline bool CreateScheduledTaskElevated() noexcept {
    wchar_t path[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, path, MAX_PATH);

    // schtasks /tr needs escaped inner quotes for paths with spaces:
    //   /tr "\"D:\Phan Mem\NexusKey.exe\""
    std::wstring taskCmd = L"/create /sc onlogon /tn " + std::wstring(STARTUP_TASK_NAME) +
                           L" /rl highest /tr \"\\\"" + path + L"\\\"\" /f";
    wchar_t args[1024];
    swprintf_s(args, L"%s", taskCmd.c_str());

    SHELLEXECUTEINFOW sei = { sizeof(sei) };
    sei.lpVerb = L"runas";
    sei.lpFile = L"schtasks";
    sei.lpParameters = args;
    sei.nShow = SW_HIDE;
    sei.fMask = SEE_MASK_NOCLOSEPROCESS;

    if (!ShellExecuteExW(&sei)) {
        return false;
    }

    if (sei.hProcess) {
        WaitForSingleObject(sei.hProcess, 5000);
        CloseHandle(sei.hProcess);
    }
    return true;
}

/// Register or unregister run-on-startup.
///
/// - enable + !asAdmin → Registry entry (normal startup)
/// - enable + asAdmin  → Task Scheduler with highest privileges (UAC prompt)
/// - !enable           → Remove both registry entry and scheduled task
inline void RegisterRunOnStartup(bool enable, bool asAdmin) {
    if (!enable) {
        // Remove both to be safe
        RemoveRegistryStartup();
        RemoveScheduledTask();
        return;
    }

    if (asAdmin) {
        // Create elevated scheduled task, remove registry to avoid duplicate
        if (CreateScheduledTaskElevated()) {
            RemoveRegistryStartup();
        }
    } else {
        // Normal registry startup, remove any elevated task
        RemoveScheduledTask();
        SetRegistryStartup();
    }
}

/// Get the Desktop folder path for the current user
[[nodiscard]] inline std::wstring GetDesktopPath() {
    wchar_t path[MAX_PATH] = {};
    if (SUCCEEDED(SHGetFolderPathW(nullptr, CSIDL_DESKTOPDIRECTORY, nullptr, 0, path))) {
        return std::wstring(path);
    }
    return {};
}

/// Get the shortcut (.lnk) path on Desktop
[[nodiscard]] inline std::wstring GetDesktopShortcutPath() {
    std::wstring desktop = GetDesktopPath();
    if (desktop.empty()) return {};
    return desktop + L"\\NexusKey.lnk";
}

/// Create a desktop shortcut (.lnk) pointing to the current executable.
/// Uses COM IShellLink + IPersistFile.
inline bool CreateDesktopShortcut() {
    std::wstring lnkPath = GetDesktopShortcutPath();
    if (lnkPath.empty()) return false;

    // Get unquoted exe path
    wchar_t exePath[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, exePath, MAX_PATH);

    // Working directory = exe directory
    std::wstring workDir(exePath);
    size_t lastSlash = workDir.find_last_of(L"\\/");
    if (lastSlash != std::wstring::npos) {
        workDir = workDir.substr(0, lastSlash);
    }

    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

    IShellLinkW* pShellLink = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER,
                                   IID_IShellLinkW, reinterpret_cast<void**>(&pShellLink));
    if (FAILED(hr) || !pShellLink) {
        CoUninitialize();
        return false;
    }

    pShellLink->SetPath(exePath);
    pShellLink->SetWorkingDirectory(workDir.c_str());
    pShellLink->SetDescription(L"NexusKey Vietnamese Input");
    pShellLink->SetIconLocation(exePath, 0);

    IPersistFile* pPersistFile = nullptr;
    hr = pShellLink->QueryInterface(IID_IPersistFile, reinterpret_cast<void**>(&pPersistFile));
    bool ok = false;
    if (SUCCEEDED(hr) && pPersistFile) {
        hr = pPersistFile->Save(lnkPath.c_str(), TRUE);
        ok = SUCCEEDED(hr);
        pPersistFile->Release();
    }

    pShellLink->Release();
    CoUninitialize();
    return ok;
}

/// Remove the desktop shortcut if it exists
inline bool RemoveDesktopShortcut() {
    std::wstring lnkPath = GetDesktopShortcutPath();
    if (lnkPath.empty()) return false;
    return DeleteFileW(lnkPath.c_str()) != FALSE;
}

/// Create or remove the desktop shortcut
inline void SetDesktopShortcut(bool enable) {
    if (enable) {
        CreateDesktopShortcut();
    } else {
        RemoveDesktopShortcut();
    }
}

}  // namespace NextKey

#endif  // _WIN32
