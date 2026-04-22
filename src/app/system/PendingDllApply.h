// NexusKey - Apply deferred TSF DLL swap at EXE startup
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <Windows.h>

namespace NextKey {

/// Result of ApplyPendingDllUpdate() — drives the restart banner.
enum class PendingDllState {
    kNone = 0,               // Nothing pending; normal startup.
    kSwapFailed = 1,         // Pending file exists but live DLL could not be released.
    kSwapDoneNeedsReboot = 2 // New DLL on disk; other processes may still hold the old one.
};

/// Called early in the main-process startup path (before CleanupOldUpdateFiles)
/// in src/app/main.cpp. Inspects `exeDir\\NextKeyTSF.dll.pending`; if present,
/// tries to swap it in for the live copy. Never throws. Safe to call when no
/// pending file exists. Caller publishes the result into SharedState flags
/// (TSF_PENDING_DLL_SWAP / TSF_POST_UPDATE_REBOOT) so subprocess dialogs can
/// observe the state.
PendingDllState ApplyPendingDllUpdate() noexcept;

/// Prompt for reboot and call ExitWindowsEx(EWX_REBOOT|EWX_RESTARTAPPS).
/// Acquires SE_SHUTDOWN_NAME privilege automatically. Shared entry point used
/// by both SettingsDialog (Sciter) and ClassicSettingsDialog + TrayIcon.
/// `owner` is used as the MessageBox parent; may be nullptr.
void RestartWindowsWithPrompt(HWND owner) noexcept;

}  // namespace NextKey
