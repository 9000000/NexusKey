// NexusKey - Core Application Entry Point
// SPDX-License-Identifier: GPL-3.0-only

#include "TrayIcon.h"
#include "SettingsDialog.h"
#include "core/SharedStateManager.h"
#include "core/TypingConfig.h"
#include <Windows.h>
#include <string>

// Sciter initialization
#include "sciter-x.h"

using namespace NextKey;

// Forward declarations
void OnMenuCommand(TrayMenuId id);
void SpawnSettingsSubprocess();
int RunSettingsSubprocess();

// Global state
static bool g_running = true;
static TrayIcon g_trayIcon;
static SharedStateManager g_sharedState;
static HINSTANCE g_hInstance = nullptr;

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, LPWSTR lpCmdLine, int) {
    g_hInstance = hInstance;

    // ═══════════════════════════════════════════════════════════
    // Subprocess Router - Sciter dialogs run as separate processes
    // to avoid memory/assertion issues on close
    // ═══════════════════════════════════════════════════════════
    if (lpCmdLine && wcsstr(lpCmdLine, L"--settings") != nullptr) {
        return RunSettingsSubprocess();
    }

    // ═══════════════════════════════════════════════════════════
    // Main Process - System tray and IPC
    // ═══════════════════════════════════════════════════════════
    
    // Initialize shared state for Engine IPC
    if (!g_sharedState.Create()) {
        OutputDebugStringW(L"NexusKey: Failed to create shared memory\n");
    } else {
        OutputDebugStringW(L"NexusKey: SharedState created\n");
    }

    // Create system tray icon
    if (!g_trayIcon.Create(hInstance)) {
        MessageBoxW(nullptr, L"Failed to create tray icon", L"NexusKey", MB_ICONERROR);
        return 1;
    }

    // Set menu callback
    g_trayIcon.SetMenuCallback(OnMenuCommand);
    g_trayIcon.SetVietnameseMode(true);

    OutputDebugStringW(L"NexusKey: Tray icon created, entering message loop\n");

    // Win32 message loop
    MSG msg;
    while (g_running && GetMessageW(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    OutputDebugStringW(L"NexusKey: Exiting\n");
    return 0;
}

void OnMenuCommand(TrayMenuId id) {
    switch (id) {
        case TrayMenuId::Settings:
            SpawnSettingsSubprocess();
            break;

        case TrayMenuId::About:
            MessageBoxW(
                nullptr,
                L"NexusKey Vietnamese Input\n"
                L"Version 1.0.0\n\n"
                L"A modern Vietnamese typing solution for Windows.\n\n"
                L"SPDX-License-Identifier: GPL-3.0-only",
                L"About NexusKey",
                MB_ICONINFORMATION
            );
            break;

        case TrayMenuId::Exit:
            g_running = false;
            PostQuitMessage(0);
            break;
    }
}

void SpawnSettingsSubprocess() {
    // Check if already open (single-instance)
    HWND existing = FindWindowW(nullptr, L"NexusKey Settings");
    if (existing) {
        SetForegroundWindow(existing);
        return;
    }

    // Get exe path
    wchar_t exePath[MAX_PATH];
    GetModuleFileNameW(nullptr, exePath, MAX_PATH);

    // Build command line
    wchar_t cmdLine[MAX_PATH + 32];
    swprintf_s(cmdLine, L"\"%s\" --settings", exePath);

    // Spawn subprocess
    STARTUPINFOW si = { sizeof(si) };
    PROCESS_INFORMATION pi;

    if (CreateProcessW(nullptr, cmdLine, nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi)) {
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
        OutputDebugStringW(L"NexusKey: Settings subprocess spawned\n");
    } else {
        MessageBoxW(nullptr, L"Failed to open settings", L"NexusKey", MB_ICONERROR);
    }
}

int RunSettingsSubprocess() {
    OutputDebugStringW(L"NexusKey: Running settings subprocess\n");

    // Initialize Sciter
    SciterSetOption(nullptr, SCITER_SET_DEBUG_MODE, TRUE);
    SciterSetOption(nullptr, SCITER_SET_SCRIPT_RUNTIME_FEATURES, 
        ALLOW_FILE_IO | ALLOW_SOCKET_IO | ALLOW_EVAL | ALLOW_SYSINFO);

    // Create and show dialog
    SettingsDialog dialog;
    dialog.SetOnSettingsChanged([]() {
        OutputDebugStringW(L"NexusKey: Settings changed\n");
    });

    // Message loop
    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
        
        if (!IsWindow(dialog.get_hwnd())) break;
    }

    // Clean exit - ExitProcess avoids Sciter assertion failures
    OutputDebugStringW(L"NexusKey: Settings subprocess exiting\n");
    ExitProcess(0);
    return 0;
}
