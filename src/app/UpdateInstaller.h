// NexusKey - Self-Update Installer
// SPDX-License-Identifier: GPL-3.0-only
//
// Handles the --install-update CLI mode: waits for processes to exit,
// replaces files from ZIP, and relaunches.
// No Sciter dependency — runs before any Sciter initialization.

#pragma once

#include <Windows.h>
#include <string>

namespace NextKey {

/// Run the self-update installer mode.
/// Waits for other NexusKey processes to exit, extracts ZIP, replaces files, relaunches.
/// Called from --install-update CLI route. Never returns.
[[noreturn]] void RunUpdateInstaller(const std::wstring& zipPath);

/// Clean up leftover files from a previous update (*_old.*, _update_temp/).
/// Called on normal startup. Safe to call even if no update was performed.
void CleanupOldUpdateFiles() noexcept;

}  // namespace NextKey
