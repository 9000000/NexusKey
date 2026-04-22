// NexusKey - Apply deferred TSF DLL swap at EXE startup
// SPDX-License-Identifier: GPL-3.0-only

#include "PendingDllApply.h"
#include "UpdateInstaller.h"              // TSF_DLL_FILENAME, constants, MakeParkedDllTimestamp
#include "core/Strings.h"
#include "core/ipc/SharedState.h"          // SharedFlags
#include "core/ipc/SharedStateManager.h"

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

}  // namespace

PendingDllState ApplyPendingDllUpdate() noexcept {
    namespace fs = std::filesystem;

    std::wstring exeDir = GetExeDirW();
    fs::path pending = fs::path(exeDir) / (std::wstring(TSF_DLL_FILENAME) + TSF_DLL_PENDING_SUFFIX);
    fs::path live    = fs::path(exeDir) / TSF_DLL_FILENAME;
    fs::path marker  = fs::path(exeDir) / TSF_DLL_PENDING_MARKER;
    fs::path oldDir  = fs::path(exeDir) / OLD_VERSION_DIRNAME;

    std::error_code ec;

    if (!fs::exists(pending, ec)) {
        fs::remove(marker, ec);   // defensive: clean orphan markers
        return PendingDllState::None;
    }

    fs::create_directories(oldDir, ec);
    fs::path parked = oldDir / (std::wstring(TSF_DLL_FILENAME)
                              + MakeParkedDllTimestamp(L"_pending"));

    fs::rename(live, parked, ec);
    if (ec) {
        return PendingDllState::SwapFailed;
    }

    std::error_code ec2;
    fs::rename(pending, live, ec2);
    if (ec2) {
        fs::rename(parked, live, ec);   // rollback
        return PendingDllState::SwapFailed;
    }

    fs::remove(marker, ec);
    return PendingDllState::SwapDoneNeedsReboot;
}

void RestartWindowsNow() noexcept {
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

void RestartWindowsWithPrompt(HWND owner) noexcept {
    if (MessageBoxW(owner, S(StringId::UPDATE_BANNER_CONFIRM), L"NexusKey",
                    MB_OKCANCEL | MB_ICONWARNING | MB_DEFBUTTON2) == IDOK) {
        RestartWindowsNow();
    }
}

int GetUpdateBannerState(const SharedStateManager& shared) noexcept {
    if (!shared.IsConnected()) return 0;
    const uint32_t flags = shared.ReadFlags();
    if (flags & SharedFlags::TSF_PENDING_DLL_SWAP) return 1;
    if (flags & (SharedFlags::TSF_POST_UPDATE_REBOOT | SharedFlags::TSF_ABI_MISMATCH)) return 2;
    return 0;
}

}  // namespace NextKey
