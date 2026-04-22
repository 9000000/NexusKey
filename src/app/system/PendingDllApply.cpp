// NexusKey - Apply deferred TSF DLL swap at EXE startup
// SPDX-License-Identifier: GPL-3.0-only

#include "PendingDllApply.h"
#include "UpdateInstaller.h"  // for TSF_DLL_FILENAME

#include <filesystem>
#include <string>

namespace NextKey {

namespace {

std::wstring GetExeDirW() noexcept {
    wchar_t buf[MAX_PATH] = {};
    DWORD len = GetModuleFileNameW(nullptr, buf, MAX_PATH);
    if (len == 0) return L".";
    std::wstring full(buf, len);
    auto pos = full.find_last_of(L"\\/");
    return (pos != std::wstring::npos) ? full.substr(0, pos) : L".";
}

std::wstring TimestampSuffix() noexcept {
    SYSTEMTIME st{};
    GetLocalTime(&st);
    wchar_t ts[64];
    swprintf_s(ts, L"_%04u%02u%02u_%02u%02u%02u_pending",
               st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
    return ts;
}

}  // namespace

PendingDllState ApplyPendingDllUpdate() noexcept {
    namespace fs = std::filesystem;

    std::wstring exeDir = GetExeDirW();
    fs::path pending = fs::path(exeDir) / (std::wstring(TSF_DLL_FILENAME) + L".pending");
    fs::path live    = fs::path(exeDir) / TSF_DLL_FILENAME;
    fs::path marker  = fs::path(exeDir) / L"_pending_dll_update";
    fs::path oldDir  = fs::path(exeDir) / L"_old_version";

    std::error_code ec;

    if (!fs::exists(pending, ec)) {
        // Defensive: clean up orphan marker so UI does not show a phantom banner.
        fs::remove(marker, ec);
        return PendingDllState::None;
    }

    fs::create_directories(oldDir, ec);
    fs::path parked = oldDir / (std::wstring(TSF_DLL_FILENAME) + TimestampSuffix());

    // Step 1: move live → parked. If the live DLL is still mapped in some host
    // process without FILE_SHARE_DELETE, this fails — defer again.
    fs::rename(live, parked, ec);
    if (ec) {
        return PendingDllState::SwapFailed;
    }

    // Step 2: move pending → live.
    std::error_code ec2;
    fs::rename(pending, live, ec2);
    if (ec2) {
        // Extremely rare: something else grabbed the live name between the two
        // renames. Put the old copy back so the EXE doesn't launch with no DLL.
        fs::rename(parked, live, ec);
        return PendingDllState::SwapFailed;
    }

    // Swap succeeded. Clear the marker — banner still shows kSwapDoneNeedsReboot
    // because hosts may hold the parked copy mapped in RAM.
    fs::remove(marker, ec);
    return PendingDllState::SwapDoneNeedsReboot;
}

void RestartWindowsWithPrompt(HWND owner) noexcept {
    // Prompt is deliberately generic — callers may show localized banner copy
    // before calling this; this MessageBox is the final confirmation.
    const wchar_t* msg = L"Restart Windows now to finish the NexusKey update?";
    if (MessageBoxW(owner, msg, L"NexusKey",
                    MB_OKCANCEL | MB_ICONWARNING | MB_DEFBUTTON2) != IDOK) {
        return;
    }

    // Acquire SE_SHUTDOWN_NAME privilege (required for ExitWindowsEx).
    HANDLE hToken = nullptr;
    if (OpenProcessToken(GetCurrentProcess(),
                         TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &hToken)) {
        TOKEN_PRIVILEGES tp{};
        if (LookupPrivilegeValueW(nullptr, SE_SHUTDOWN_NAME, &tp.Privileges[0].Luid)) {
            tp.PrivilegeCount = 1;
            tp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
            AdjustTokenPrivileges(hToken, FALSE, &tp, 0, nullptr, nullptr);
        }
        CloseHandle(hToken);
    }

    ExitWindowsEx(EWX_REBOOT | EWX_RESTARTAPPS,
                  SHTDN_REASON_MAJOR_APPLICATION
                  | SHTDN_REASON_MINOR_UPGRADE
                  | SHTDN_REASON_FLAG_PLANNED);
}

}  // namespace NextKey
