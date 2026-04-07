// NexusKey Classic — Lite build entry point
// SPDX-License-Identifier: GPL-3.0-only

#include "core/Version.h"
#include "core/config/ConfigManager.h"
#include "core/ipc/SharedState.h"
#include "core/ipc/SharedStateManager.h"
#include "core/ipc/SecurityHelpers.h"
#include "core/config/ConfigEvent.h"
#include "core/Strings.h"
#include "core/Debug.h"

#include "system/HookEngine.h"
#include "system/QuickConvert.h"
#include "system/TrayIcon.h"
#include "system/TsfRegistration.h"
#include "system/StartupHelper.h"
#include "system/UpdateChecker.h"

#include <Windows.h>
#include <commctrl.h>
#include <ole2.h>
#include <memory>
#include <atomic>

#pragma comment(lib, "comctl32.lib")

using namespace NextKey;

static std::atomic<bool> g_running{true};
static TrayIcon g_trayIcon;
static HookEngine g_hookEngine;
static SharedStateManager g_sharedState;
static std::unique_ptr<QuickConvert> g_quickConvert;

// TODO: Task 5 will add ClassicSettingsDialog integration here

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, LPWSTR, int) {
    // Prevent multiple instances
    HANDLE hMutex = CreateMutexW(nullptr, TRUE, L"NexusKeyLiteMutex");
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        CloseHandle(hMutex);
        return 0;
    }

    OleInitialize(nullptr);
    INITCOMMONCONTROLSEX icc = { sizeof(icc), ICC_TAB_CLASSES | ICC_STANDARD_CLASSES };
    InitCommonControlsEx(&icc);

    // Placeholder: message loop
    MSG msg;
    while (GetMessage(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    OleUninitialize();
    CloseHandle(hMutex);
    return 0;
}
