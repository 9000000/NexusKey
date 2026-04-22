// NexusKey - Apply deferred TSF DLL swap at EXE startup
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <Windows.h>

namespace NextKey {

class SharedStateManager;

/// Result of ApplyPendingDllUpdate() — drives the restart banner.
enum class PendingDllState {
    None = 0,                 // Nothing pending; normal startup.
    SwapFailed = 1,           // Pending file exists but live DLL could not be released.
    SwapDoneNeedsReboot = 2   // New DLL on disk; other processes may still hold the old one.
};

/// Called early in the main-process startup path (before CleanupOldUpdateFiles).
/// Inspects `exeDir\\NextKeyTSF.dll.pending`; if present, tries to swap it in
/// for the live copy. Never throws. Caller MUST publish the result into
/// SharedState flags (TSF_PENDING_DLL_SWAP / TSF_POST_UPDATE_REBOOT) so
/// subprocess dialogs can observe it — hence [[nodiscard]].
[[nodiscard]] PendingDllState ApplyPendingDllUpdate() noexcept;

/// Prompt for reboot and call ExitWindowsEx(EWX_REBOOT|EWX_RESTARTAPPS).
/// Acquires SE_SHUTDOWN_NAME privilege automatically. Shared entry point used
/// by SettingsDialog, ClassicSettingsDialog and TrayIcon.
void RestartWindowsWithPrompt(HWND owner) noexcept;

/// Read TSF-update flags from SharedState and decide banner visibility.
/// Returns 0 = hide, 1 = "update not finished" (pending-swap failed),
/// 2 = "some apps still run the old version" (swap done + mismatch cases).
/// Single source of truth for the banner-state decision used by Sciter
/// settings, Classic settings and the tray menu.
[[nodiscard]] int GetUpdateBannerState(const SharedStateManager& shared) noexcept;

}  // namespace NextKey
