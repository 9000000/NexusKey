// NexusKey - Subprocess Helper
// SPDX-License-Identifier: GPL-3.0-only
//
// Shared initialization for Sciter subprocess dialogs + subprocess spawning.
// Tracks child process handles for cleanup on exit (TerminateProcess).

#pragma once

#include "sciter/SciterArchive.h"
#include "sciter/SciterHelper.h"
#include "core/config/ConfigManager.h"
#include "core/Strings.h"
#include "sciter-x.h"
#include "sciter-x-window.hpp"
#include <windows.h>
#include <ole2.h>
#include <vector>

namespace NextKey {

/// Per-process list of child process handles (for TerminateProcess on exit)
inline std::vector<HANDLE>& GetChildProcessHandles() {
    static std::vector<HANDLE> handles;
    return handles;
}

/// Terminate all tracked child processes and close handles.
/// Call from exit handler before PostQuitMessage / ExitProcess.
inline void TerminateAllSubprocesses() {
    for (HANDLE h : GetChildProcessHandles()) {
        if (h) {
            TerminateProcess(h, 0);
            CloseHandle(h);
        }
    }
    GetChildProcessHandles().clear();
}

/// Track a child process handle (caller retains ownership until TerminateAllSubprocesses)
inline void TrackChildProcess(HANDLE hProcess) {
    if (hProcess) {
        GetChildProcessHandles().push_back(hProcess);
    }
}

/// Initialize Sciter runtime for a subprocess dialog.
/// Call once at the start of RunXxxSubprocess() before creating any dialog.
inline void InitSciterSubprocess() {
    OleInitialize(nullptr);
    SciterHelper::ApplyDarkModeForApp();  // Enable dark mode for native controls

    // Load UI language from config (subprocess starts with default=Vietnamese)
    auto sysConfig = ConfigManager::LoadSystemConfigOrDefault();
    SetLanguage(static_cast<Language>(sysConfig.language));

    sciter::application::start();
    
    // FIX: ClearType corrupts alpha channel on Win10 DWM surfaces
    if (SciterHelper::IsWindows11OrGreater()) {
        SciterSetOption(nullptr, SCITER_SET_GFX_LAYER, GFX_LAYER_D2D);
        // CRITICAL: Disable DirectComposition on Win11 when using D2D to avoid black/blank window issues
        SciterSetOption(nullptr, SCITER_SET_UX_THEMING, TRUE);
    } else {
        // Win10 needs SKIA to fix alpha channel issues with DWM
        SciterSetOption(nullptr, SCITER_SET_GFX_LAYER, GFX_LAYER_SKIA);
    }

    SciterSetOption(nullptr, SCITER_SET_SCRIPT_RUNTIME_FEATURES,
        ALLOW_FILE_IO | ALLOW_SOCKET_IO | ALLOW_EVAL | ALLOW_SYSINFO);
    BindSciterResources();
}

/// Spawn a subprocess (single-instance by window title).
/// If a window with the given title already exists, brings it to front.
/// Tracks the process handle for cleanup via TerminateAllSubprocesses().
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
        TrackChildProcess(pi.hProcess);  // Track for cleanup, don't close
    }
}

}  // namespace NextKey
