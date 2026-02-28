// NexusKey - Subprocess Helper
// SPDX-License-Identifier: GPL-3.0-only
//
// Shared initialization for Sciter subprocess dialogs + subprocess spawning.

#pragma once

#include "SciterArchive.h"
#include "sciter-x.h"
#include "sciter-x-window.hpp"
#include <windows.h>
#include <ole2.h>

namespace NextKey {

/// Initialize Sciter runtime for a subprocess dialog.
/// Call once at the start of RunXxxSubprocess() before creating any dialog.
inline void InitSciterSubprocess() {
    OleInitialize(nullptr);
    sciter::application::start();
    SciterSetOption(nullptr, SCITER_SET_SCRIPT_RUNTIME_FEATURES,
        ALLOW_FILE_IO | ALLOW_SOCKET_IO | ALLOW_EVAL | ALLOW_SYSINFO);
    BindSciterResources();
}

/// Spawn a subprocess (single-instance by window title).
/// If a window with the given title already exists, brings it to front.
inline void SpawnSubprocess(const wchar_t* windowTitle, const wchar_t* cliArgs) {
    HWND existing = FindWindowW(nullptr, windowTitle);
    if (existing) {
        SetForegroundWindow(existing);
        return;
    }

    wchar_t exePath[MAX_PATH];
    GetModuleFileNameW(nullptr, exePath, MAX_PATH);

    wchar_t cmdLine[MAX_PATH + 128];
    swprintf_s(cmdLine, L"\"%s\" %s", exePath, cliArgs);

    STARTUPINFOW si = { sizeof(si) };
    PROCESS_INFORMATION pi;
    if (CreateProcessW(nullptr, cmdLine, nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi)) {
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
    }
}

}  // namespace NextKey
