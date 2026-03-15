// NexusKey - Self-Update Installer Implementation
// SPDX-License-Identifier: GPL-3.0-only

#include "UpdateInstaller.h"

#include <TlHelp32.h>
#include <filesystem>
#include <vector>

namespace NextKey {

namespace {

/// Get exe directory
std::wstring GetExeDirectory() {
    wchar_t path[MAX_PATH] = {};
    DWORD len = GetModuleFileNameW(nullptr, path, MAX_PATH);
    if (len == 0) return L".";
    std::wstring fullPath(path, len);
    auto pos = fullPath.find_last_of(L"\\/");
    return (pos != std::wstring::npos) ? fullPath.substr(0, pos) : L".";
}

/// Get full exe path
std::wstring GetExePath() {
    wchar_t path[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, path, MAX_PATH);
    return path;
}

/// Get current process ID
DWORD GetCurrentPID() {
    return GetCurrentProcessId();
}

/// Wait for all other instances of this app to exit (up to timeoutMs)
bool WaitForOtherProcesses(DWORD timeoutMs) {
    DWORD myPid = GetCurrentPID();
    
    wchar_t myPath[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, myPath, MAX_PATH);
    std::wstring myName = myPath;
    auto pos = myName.find_last_of(L"\\/");
    if (pos != std::wstring::npos) myName = myName.substr(pos + 1);

    DWORD startTick = GetTickCount();
    while (GetTickCount() - startTick < timeoutMs) {
        bool othersRunning = false;

        HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        if (snap == INVALID_HANDLE_VALUE) break;

        PROCESSENTRY32W pe = {};
        pe.dwSize = sizeof(pe);

        if (Process32FirstW(snap, &pe)) {
            do {
                if (pe.th32ProcessID != myPid &&
                    (_wcsicmp(pe.szExeFile, myName.c_str()) == 0)) {
                    othersRunning = true;
                    break;
                }
            } while (Process32NextW(snap, &pe));
        }

        CloseHandle(snap);

        if (!othersRunning) return true;
        Sleep(500);
    }

    return false;  // Timeout
}

/// Extract ZIP using PowerShell Expand-Archive (hidden window)
bool ExtractZip(const std::wstring& zipPath, const std::wstring& destDir) {
    // Build PowerShell command
    std::wstring cmd = L"powershell.exe -NoProfile -ExecutionPolicy Bypass -Command \"";
    cmd += L"Expand-Archive -Path '";
    cmd += zipPath;
    cmd += L"' -DestinationPath '";
    cmd += destDir;
    cmd += L"' -Force\"";

    STARTUPINFOW si = { sizeof(si) };
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;

    PROCESS_INFORMATION pi = {};
    if (!CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi)) {
        return false;
    }

    WaitForSingleObject(pi.hProcess, 60000);  // 60s timeout

    DWORD exitCode = 1;
    GetExitCodeProcess(pi.hProcess, &exitCode);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);

    return exitCode == 0;
}

/// Copy all files from srcDir to destDir (overwriting)
bool CopyDirectoryContents(const std::wstring& srcDir, const std::wstring& destDir) {
    try {
        namespace fs = std::filesystem;
        for (const auto& entry : fs::recursive_directory_iterator(srcDir)) {
            auto relativePath = fs::relative(entry.path(), srcDir);
            auto destPath = fs::path(destDir) / relativePath;

            if (entry.is_directory()) {
                fs::create_directories(destPath);
            } else {
                fs::create_directories(destPath.parent_path());
                fs::copy_file(entry.path(), destPath, fs::copy_options::overwrite_existing);
            }
        }
        return true;
    } catch (...) {
        return false;
    }
}

}  // namespace

[[noreturn]] void RunUpdateInstaller(const std::wstring& zipPath) {
    std::wstring exeDir = GetExeDirectory();
    std::wstring currentExePath = GetExePath();
    std::wstring tempDir = exeDir + L"\\_update_temp";

    // 1. Wait for all other NexusKey.exe processes to exit (30s timeout)
    WaitForOtherProcesses(30000);

    // 2. Move ALL .exe and .dll files to _old_version/ folder
    // This handles sciter.dll, TSF DLLs, and the main EXE regardless of name.
    std::wstring oldVersionDir = exeDir + L"\\_old_version";
    {
        namespace fs = std::filesystem;
        std::error_code ec;
        fs::create_directories(oldVersionDir, ec);

        for (const auto& entry : fs::directory_iterator(exeDir)) {
            if (!entry.is_regular_file()) continue;
            auto ext = entry.path().extension().wstring();
            if (_wcsicmp(ext.c_str(), L".exe") == 0 || _wcsicmp(ext.c_str(), L".dll") == 0) {
                // Don't move files that are already inside a special folder (though iterator is non-recursive)
                std::wstring name = entry.path().filename().wstring();
                std::wstring destPath = oldVersionDir + L"\\" + name;
                
                DeleteFileW(destPath.c_str());
                MoveFileW(entry.path().c_str(), destPath.c_str());
            }
        }
    }

    // 3. Extract ZIP to _update_temp/
    {
        // Clean up any previous temp dir
        namespace fs = std::filesystem;
        std::error_code ec;
        fs::remove_all(tempDir, ec);
    }

    bool extracted = ExtractZip(zipPath, tempDir);
    if (!extracted) {
        // Rollback attempt: this is hard because we renamed everything.
        // But usually extract fails due to disk space or corrupted zip.
        ExitProcess(1);
    }

    // 4. Detect ZIP structure: root files or single subdirectory
    std::wstring finalExePath;
    {
        namespace fs = std::filesystem;
        std::wstring sourceDir = tempDir;

        // Check if there's a single subdirectory (common ZIP structure)
        std::vector<fs::directory_entry> entries;
        for (const auto& e : fs::directory_iterator(tempDir)) {
            entries.push_back(e);
        }

        if (entries.size() == 1 && entries[0].is_directory()) {
            sourceDir = entries[0].path().wstring();
        }

        // 5. Copy new files to exe directory
        CopyDirectoryContents(sourceDir, exeDir);

        // 6. Find the main executable to launch
        // Prefer "NexusKey.exe", then "NextKey.exe", then "NextKey32.exe", then any "Nexus/Next*.exe"
        const std::vector<std::wstring> preferredNames = { L"NexusKey.exe", L"NextKey.exe", L"NextKey32.exe", L"NexusKey64.exe" };
        for (const auto& name : preferredNames) {
            std::wstring testPath = exeDir + L"\\" + name;
            if (fs::exists(testPath)) {
                finalExePath = testPath;
                break;
            }
        }

        // Fallback: find any EXE that looks like the main app
        if (finalExePath.empty()) {
            for (const auto& entry : fs::directory_iterator(exeDir)) {
                if (!entry.is_regular_file()) continue;
                if (_wcsicmp(entry.path().extension().c_str(), L".exe") == 0) {
                    std::wstring name = entry.path().filename().wstring();
                    if (name.find(L"Nexus") != std::wstring::npos || name.find(L"Next") != std::wstring::npos) {
                        // Skip updater if it's named NextKeyUpdate.exe
                        if (name.find(L"Update") == std::wstring::npos) {
                            finalExePath = entry.path().wstring();
                            break;
                        }
                    }
                }
            }
        }
    }

    // 7. Clean up temp files
    DeleteFileW(zipPath.c_str());
    {
        namespace fs = std::filesystem;
        std::error_code ec;
        fs::remove_all(tempDir, ec);
    }

    // 8. Launch new executable
    if (!finalExePath.empty()) {
        STARTUPINFOW si = { sizeof(si) };
        PROCESS_INFORMATION pi = {};
        if (CreateProcessW(finalExePath.c_str(), nullptr, nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi)) {
            CloseHandle(pi.hThread);
            CloseHandle(pi.hProcess);
        }
    }

    // 9. Exit updater
    ExitProcess(0);
}

bool CleanupOldUpdateFiles() noexcept {
    bool cleaned = false;
    try {
        std::wstring exeDir = GetExeDirectory();
        namespace fs = std::filesystem;

        // 1. Delete *_old.* files (legacy cleanup)
        for (const auto& entry : fs::directory_iterator(exeDir)) {
            if (!entry.is_regular_file()) continue;
            std::wstring name = entry.path().filename().wstring();

            // Check for _old before extension
            auto stem = entry.path().stem().wstring();
            if (stem.size() >= 4 && stem.substr(stem.size() - 4) == L"_old") {
                std::error_code ec;
                if (fs::remove(entry.path(), ec)) {
                    cleaned = true;
                }
            }
        }

        // 2. Delete _old_version/ directory
        std::wstring oldVersionDir = exeDir + L"\\_old_version";
        if (fs::exists(oldVersionDir)) {
            std::error_code ec;
            if (fs::remove_all(oldVersionDir, ec) > 0) {
                cleaned = true;
            }
        }

        // 3. Delete _update_temp/ directory if it exists
        std::wstring tempDir = exeDir + L"\\_update_temp";
        if (fs::exists(tempDir)) {
            std::error_code ec;
            fs::remove_all(tempDir, ec);
        }
    } catch (...) {
        // Cleanup is best-effort
    }
    return cleaned;
}

}  // namespace NextKey
