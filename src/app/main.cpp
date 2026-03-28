// NexusKey - Core Application Entry Point
// SPDX-License-Identifier: GPL-3.0-only

#include "system/TrayIcon.h"
#include "system/SubprocessHelper.h"
#include "system/SubprocessRunners.h"
#include "system/UpdateChecker.h"
#include "system/UpdateInstaller.h"
#include "system/ToastPopup.h"
#include "core/Version.h"
#include "core/config/TypingConfig.h"
#include "core/config/ConfigManager.h"
#include "core/config/ConfigEvent.h"
#include "core/ipc/SharedState.h"
#include "core/Strings.h"
#include "core/Debug.h"

#include "system/TsfRegistration.h"

#ifdef NEXUSKEY_HOOK_ENGINE
#include "system/HookEngine.h"
#include "system/QuickConvert.h"
#include "core/ipc/SharedStateManager.h"
#else
#include "system/HotkeyManager.h"
#include "core/ipc/SharedStateManager.h"
#endif

#include <Windows.h>
#include <ole2.h>
#include <timeapi.h>
#include <memory>
#include <string>
#include <atomic>
#include <thread>

#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "winmm.lib")

// Require Common Controls v6 for TaskDialog / TaskDialogIndirect
#pragma comment(linker, "/manifestdependency:\"type='win32' name='Microsoft.Windows.Common-Controls' " \
    "version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

using namespace NextKey;

// Forward declarations
void SpawnSettingsSubprocess();

// Global state
static std::atomic<bool> g_running{true};
static TrayIcon g_trayIcon;
static HINSTANCE g_hInstance = nullptr;

#ifdef NEXUSKEY_HOOK_ENGINE
static HookEngine g_hookEngine;
static std::unique_ptr<QuickConvert> g_quickConvert;
static SharedStateManager g_sharedState;  // Shared memory for Settings subprocess IPC
#else
static SharedStateManager g_sharedState;
static HotkeyManager g_hotkeyManager;
#endif

// Forward declaration
void OnMenuCommand(TrayMenuId id);

// Settings window helpers — search by title (subprocess may or may not be open)
static HWND GetSettingsHwnd() noexcept {
    return FindWindowW(nullptr, L"NexusKey Settings");
}
static void NotifySettingsMode(bool vietnamese) noexcept {
    if (HWND h = GetSettingsHwnd()) {
        PostMessageW(h, WM_NEXUSKEY_MODE_CHANGED, vietnamese ? 1 : 0, 0);
    }
}

#ifndef NEXUSKEY_HOOK_ENGINE
// V/E icon sync: 250ms poll of SharedState flags (atomic read, no IPC)
static constexpr UINT_PTR TIMER_ID_ICON_POLL = 100;

static void CALLBACK IconPollTimerProc(HWND, UINT, UINT_PTR, DWORD) {
    uint32_t flags = g_sharedState.ReadFlags();
    bool vietnamese = (flags & SharedFlags::VIETNAMESE_MODE) != 0;
    g_trayIcon.SetVietnameseMode(vietnamese);  // no-op if unchanged
}
#endif

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, LPWSTR lpCmdLine, int) {
    g_hInstance = hInstance;

    // ═══════════════════════════════════════════════════════════
    // Command-line Router
    // ═══════════════════════════════════════════════════════════

    // TSF Unregistration (runs elevated, then exits)
    // NOTE: Must check --unregister-tsf BEFORE --register-tsf
    // because "--register-tsf" is a substring of "--unregister-tsf"
    if (lpCmdLine && wcsstr(lpCmdLine, L"--unregister-tsf") != nullptr) {
        CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        bool ok = UnregisterTsf();
        CoUninitialize();
        return ok ? 0 : 1;
    }

    // TSF Registration (runs elevated, then exits)
    if (lpCmdLine && wcsstr(lpCmdLine, L"--register-tsf") != nullptr) {
        CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        bool ok = RegisterTsf();
        CoUninitialize();
        return ok ? 0 : 1;
    }

    // Diagnostics mode (shows HKL/TSF/SharedState info in MessageBox)
    if (lpCmdLine && wcsstr(lpCmdLine, L"--diag") != nullptr) {
        RunDiagnostics();
        return 0;
    }

    // Self-update installer mode (MUST be before Sciter dialog routes — sciter.dll not loaded yet)
    if (lpCmdLine && wcsstr(lpCmdLine, L"--install-update") != nullptr) {
        const wchar_t* arg = wcsstr(lpCmdLine, L"--install-update");
        arg += wcslen(L"--install-update");  // Skip "--install-update"
        // Skip whitespace
        while (*arg == L' ' || *arg == L'\t') arg++;
        // Strip surrounding quotes if present
        std::wstring zipPath(arg);
        if (zipPath.size() >= 2 && zipPath.front() == L'"' && zipPath.back() == L'"') {
            zipPath = zipPath.substr(1, zipPath.size() - 2);
        }
        if (!zipPath.empty()) {
            RunUpdateInstaller(zipPath);  // [[noreturn]]
        }
        return 1;  // Missing zip path
    }

    // Subprocess dialog routes
    if (lpCmdLine && wcsstr(lpCmdLine, L"--settings") != nullptr) {
        RunSettingsSubprocess();  // [[noreturn]]
    }
    if (lpCmdLine && wcsstr(lpCmdLine, L"--excludedapps") != nullptr) {
        RunExcludedAppsSubprocess();  // [[noreturn]]
    }
    if (lpCmdLine && wcsstr(lpCmdLine, L"--tsfapps") != nullptr) {
        RunTsfAppsSubprocess();  // [[noreturn]]
    }
    if (lpCmdLine && wcsstr(lpCmdLine, L"--macro") != nullptr) {
        RunMacroSubprocess();  // [[noreturn]]
    }
    if (lpCmdLine && wcsstr(lpCmdLine, L"--convert") != nullptr) {
        RunConvertToolSubprocess();  // [[noreturn]]
    }
    if (lpCmdLine && wcsstr(lpCmdLine, L"--about") != nullptr) {
        RunAboutSubprocess();  // [[noreturn]]
    }

    // ═══════════════════════════════════════════════════════════
    // Main Process
    // ═══════════════════════════════════════════════════════════

    // Ensure only one background instance of NexusKey runs at a time.
    // We check this AFTER subprocess routing so settings/macro dialogs 
    // can spawn freely, but a second background process cannot.
    HANDLE hMutex = CreateMutexW(nullptr, TRUE, L"Local\\NexusKey_Main_Mutex");
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        // Another background instance is already running.
        // If the user has "show on startup" configured, popup the settings dialog of the 
        // existing instance to indicate the app is active. Otherwise, strictly silent.
        auto sysConfig = ConfigManager::LoadSystemConfigOrDefault();
        if (sysConfig.showOnStartup) {
            HWND existingTrayWnd = FindWindowW(L"NexusKeyTrayClass", nullptr);
            if (existingTrayWnd) {
                PostMessageW(existingTrayWnd, WM_NEXUSKEY_SHOW_SETTINGS, 0, 0);
                
                HWND existingSettings = GetSettingsHwnd();
                if (existingSettings) {
                    SetForegroundWindow(existingSettings);
                }
            }
        }
        
        NEXTKEY_LOG(L"Another instance is already running. Exiting.");
        CloseHandle(hMutex);
        return 0;
    }

    // Load config
    auto config = ConfigManager::LoadOrDefault();
    auto hotkeyConfig = ConfigManager::LoadHotkeyConfigOrDefault();

    // Initialize UI language from system config
    auto systemConfig = ConfigManager::LoadSystemConfigOrDefault();
    SetLanguage(static_cast<Language>(systemConfig.language));

    // Clean up leftover files from a previous update.
    // If files were cleaned up, it means we just finished an update.
    if (CleanupOldUpdateFiles()) {
        NEXTKEY_LOG(L"Update completed successfully, old files cleaned up");
    }

#ifdef NEXUSKEY_HOOK_ENGINE
    // ═══════════════════════════════════════════════════════════
    // Hook Engine Mode — single process, no DLL/COM needed
    // ═══════════════════════════════════════════════════════════

    // Create SharedState for Settings subprocess IPC
    if (g_sharedState.Create()) {
        SharedState state;
        state.InitDefaults();
        state.inputMethod = static_cast<uint8_t>(config.inputMethod);
        state.spellCheck = config.spellCheckEnabled ? 1 : 0;
        state.optimizeLevel = config.optimizeLevel;
        state.codeTable = static_cast<uint8_t>(config.codeTable);
        state.SetFeatureFlags(EncodeFeatureFlags(config));
        g_sharedState.Write(state);
        NEXTKEY_LOG(L"SharedState created for HookEngine mode");
    }

    // Tray Icon
    if (!g_trayIcon.Create(hInstance)) {
        // MessageBox acceptable: fatal startup error, app cannot function without tray icon.
        // No matching StringId — using English string (language config not yet applied to UI).
        MessageBoxW(nullptr, L"Failed to create tray icon", L"NexusKey", MB_ICONERROR);
        return 1;
    }
    g_trayIcon.SetMenuCallback(OnMenuCommand);

    // Wire mode change callback: HookEngine → defer icon update via PostMessage
    g_hookEngine.SetModeChangeCallback([](bool vietnamese) {
        g_sharedState.SetOrClearFlag(SharedFlags::VIETNAMESE_MODE, vietnamese);
        WPARAM wp = vietnamese ? 1 : 0;
        HWND trayWnd = g_trayIcon.GetMessageWindow();
        if (trayWnd) {
            PostMessageW(trayWnd, WM_NEXUSKEY_TRAY_MODE_SYNC, wp, 0);
        }
        // Notify settings dialog directly (1 hop instead of 2 via TRAY_MODE_SYNC).
        // This keeps the toggle in sync with the tray icon even on rapid clicks.
        NotifySettingsMode(vietnamese);
    });

    // Wire TSF active callback: HookEngine → SharedState flag for DLL
    g_hookEngine.SetTsfActiveCallback([](bool active) {
        g_sharedState.SetOrClearFlag(SharedFlags::TSF_ACTIVE, active);
    });

    // Wire settings dialog → HookEngine mode set (cross-process)
    g_trayIcon.SetModeRequestCallback([](bool vietnamese) {
        if (g_hookEngine.IsVietnameseMode() != vietnamese) {
            g_hookEngine.ToggleVietnameseMode();
        }
    });

    // Wire menu state getter — reads from SharedState only (no TOML)
    g_trayIcon.SetMenuStateGetter([]() -> TrayMenuState {
        SharedState state = g_sharedState.Read();
        uint32_t ff = state.GetFeatureFlags();
        return {
            g_hookEngine.IsVietnameseMode(),
            state.spellCheck != 0,
            (ff & FeatureFlags::SMART_SWITCH) != 0,
            (ff & FeatureFlags::MACRO_ENABLED) != 0,
            state.inputMethod,
            static_cast<CodeTable>(state.codeTable)
        };
    });

    // Load convert config and create QuickConvert
    {
        auto convertConfig = ConfigManager::LoadConvertConfigOrDefault();
        g_quickConvert = std::make_unique<QuickConvert>(convertConfig);

        g_hookEngine.SetConvertHotkey(convertConfig.hotkey);
        g_hookEngine.SetConvertCallback([]() {
            if (g_quickConvert) {
                g_quickConvert->Execute();
            }
        });

        g_hookEngine.SetConfigReloadCallback([]() {
            if (g_quickConvert) {
                auto cc = ConfigManager::LoadConvertConfigOrDefault();
                g_quickConvert->UpdateConfig(cc);
            }
        });
    }

    // Set 1ms timer resolution so Sleep(1) actually sleeps ~1ms instead of ~15ms.
    // Required for smooth Vietnamese input — backspace-then-retype needs a short gap
    // between SendInput calls for Electron/Console apps, but 15ms (default) is noticeable.
    timeBeginPeriod(1);

    // Start keyboard hook engine
    if (!g_hookEngine.Start(hInstance, config, hotkeyConfig)) {
        // MessageBox acceptable: fatal startup error, app cannot function without keyboard hook.
        // No matching StringId — using English string (language config not yet applied to UI).
        timeEndPeriod(1);
        MessageBoxW(nullptr, L"Failed to install keyboard hook", L"NexusKey", MB_ICONERROR);
        return 1;
    }

    NEXTKEY_LOG(L"HookEngine started, entering message loop");

    // Apply system config (icon style, show-on-startup)
    g_trayIcon.SetIconConfig(systemConfig.iconStyle, systemConfig.customColorV, systemConfig.customColorE);
    if (systemConfig.showOnStartup) {
        SpawnSettingsSubprocess();
    }

    // Auto-check for updates on startup (background thread, 3s delay)
    if (systemConfig.autoCheckUpdate) {
        std::thread([]() {
            Sleep(3000);
            auto info = UpdateChecker::CheckForUpdate();
            if (info.available) {
                HWND trayWnd = FindWindowW(L"NexusKeyTrayClass", nullptr);
                if (trayWnd) {
                    auto* pInfo = new (std::nothrow) UpdateInfo(std::move(info));
                    if (pInfo) {
                        SendMessageW(trayWnd, WM_NEXUSKEY_UPDATE_AVAILABLE, 0,
                                     reinterpret_cast<LPARAM>(pInfo));
                    }
                }
            }
        }).detach();
    }

    MSG msg;
    while (g_running.load(std::memory_order_relaxed) && GetMessageW(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    // Cleanup
    g_hookEngine.Stop();
    timeEndPeriod(1);

#else
    // ═══════════════════════════════════════════════════════════
    // TSF Mode — SharedState IPC + DLL
    // ═══════════════════════════════════════════════════════════

    // Initialize COM for TSF registration check
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

    // Check and register TSF if needed
    if (!IsTsfRegistered()) {
        NEXTKEY_LOG(L"TSF not registered, attempting registration...");

        if (!RegisterTsf()) {
            // MessageBox acceptable: requires user consent before UAC elevation prompt.
            // No matching StringId — using English strings (first-run scenario, language not configured yet).
            int result = MessageBoxW(
                nullptr,
                L"NexusKey needs to register its input method.\n\n"
                L"This requires administrator privileges.\n"
                L"Click OK to continue with elevation.",
                L"NexusKey Setup",
                MB_OKCANCEL | MB_ICONINFORMATION
            );

            if (result == IDOK) {
                if (!RegisterTsfElevated()) {
                    MessageBoxW(nullptr,
                        L"Failed to register input method.\n"
                        L"Please run as administrator.",
                        L"NexusKey", MB_ICONERROR);
                }
            }
        }
    } else {
        NEXTKEY_LOG(L"TSF already registered");
    }

    // Initialize shared state for Engine IPC
    bool sharedStateOk = g_sharedState.Create();
    if (!sharedStateOk) {
        NEXTKEY_LOG(L"Failed to create shared memory, TSF will use TOML fallback");
    } else {
        SharedState state;
        state.InitDefaults();
        // TSF-only mode: DLL is the only engine, always active
        state.flags |= SharedFlags::TSF_ACTIVE;
        state.inputMethod = static_cast<uint8_t>(config.inputMethod);
        state.spellCheck = config.spellCheckEnabled ? 1 : 0;
        state.optimizeLevel = config.optimizeLevel;
        state.codeTable = static_cast<uint8_t>(config.codeTable);
        state.SetFeatureFlags(EncodeFeatureFlags(config));
        g_sharedState.Write(state);
        NEXTKEY_LOG(L"SharedState created and initialized (TSF_ACTIVE=1, TSF-only mode)");
    }

    // Tray Icon
    if (!g_trayIcon.Create(hInstance)) {
        // MessageBox acceptable: fatal startup error, app cannot function without tray icon.
        // No matching StringId — using English string (language config not yet applied to UI).
        MessageBoxW(nullptr, L"Failed to create tray icon", L"NexusKey", MB_ICONERROR);
        CoUninitialize();
        return 1;
    }
    g_trayIcon.SetMenuCallback(OnMenuCommand);

    // Wire settings dialog → TSF mode set (cross-process)
    g_trayIcon.SetModeRequestCallback([](bool vietnamese) {
        g_sharedState.SetOrClearFlag(SharedFlags::VIETNAMESE_MODE, vietnamese);
        g_trayIcon.SetVietnameseMode(vietnamese);
    });

    // Wire menu state getter — reads from SharedState only (no TOML)
    g_trayIcon.SetMenuStateGetter([]() -> TrayMenuState {
        SharedState state = g_sharedState.Read();
        uint32_t ff = state.GetFeatureFlags();
        return {
            (state.flags & SharedFlags::VIETNAMESE_MODE) != 0,
            state.spellCheck != 0,
            (ff & FeatureFlags::SMART_SWITCH) != 0,
            (ff & FeatureFlags::MACRO_ENABLED) != 0,
            state.inputMethod,
            static_cast<CodeTable>(state.codeTable)
        };
    });

    // Poll SharedState flags every 250ms to sync icon V/E state
    SetTimer(g_trayIcon.GetMessageWindow(), TIMER_ID_ICON_POLL, 250, IconPollTimerProc);

    // Internal Hotkey
    auto hotkeyOpt = ConfigManager::LoadHotkeyConfig(ConfigManager::GetConfigPath());
    if (hotkeyOpt && (hotkeyOpt->ctrl || hotkeyOpt->shift || hotkeyOpt->alt || hotkeyOpt->win || hotkeyOpt->key != 0)) {
        g_hotkeyManager.Initialize(*hotkeyOpt, g_trayIcon.GetMessageWindow(), hInstance);
        NEXTKEY_LOG(L"Internal hotkey installed (ctrl=%d, shift=%d, alt=%d, win=%d, key=0x%02X)",
                    hotkeyOpt->ctrl, hotkeyOpt->shift, hotkeyOpt->alt, hotkeyOpt->win, hotkeyOpt->key);
    } else {
        NEXTKEY_LOG(L"No internal hotkey configured");
    }

    NEXTKEY_LOG(L"Tray icon created, entering message loop");

    // Apply system config (icon style, show-on-startup)
    g_trayIcon.SetIconConfig(systemConfig.iconStyle, systemConfig.customColorV, systemConfig.customColorE);
    if (systemConfig.showOnStartup) {
        SpawnSettingsSubprocess();
    }

    // Auto-check for updates on startup (background thread, 3s delay)
    if (systemConfig.autoCheckUpdate) {
        std::thread([]() {
            Sleep(3000);
            auto info = UpdateChecker::CheckForUpdate();
            if (info.available) {
                HWND trayWnd = FindWindowW(L"NexusKeyTrayClass", nullptr);
                if (trayWnd) {
                    auto* pInfo = new (std::nothrow) UpdateInfo(std::move(info));
                    if (pInfo) {
                        SendMessageW(trayWnd, WM_NEXUSKEY_UPDATE_AVAILABLE, 0,
                                     reinterpret_cast<LPARAM>(pInfo));
                    }
                }
            }
        }).detach();
    }

    MSG msg;
    while (g_running.load(std::memory_order_relaxed) && GetMessageW(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    // Cleanup
    KillTimer(g_trayIcon.GetMessageWindow(), TIMER_ID_ICON_POLL);
    g_hotkeyManager.Uninstall();

    // Disable engine in SharedState so TSF stops processing
    if (sharedStateOk) {
        SharedState state = g_sharedState.Read();
        if (state.IsValid()) {
            state.flags &= ~SharedFlags::ENGINE_ENABLED;
            g_sharedState.Write(state);
            NEXTKEY_LOG(L"ENGINE_ENABLED cleared in SharedState");
        }
    }

    CoUninitialize();
#endif

    NEXTKEY_LOG(L"Exiting");
    return 0;
}

/// Save config to TOML + sync SharedState + signal ConfigEvent.
/// Used by tray menu toggle handlers to propagate changes.
static void ApplyConfigChange(const TypingConfig& config) {
    // 1. Save to TOML
    (void)ConfigManager::SaveToFile(ConfigManager::GetConfigPath(), config);

    // 2. Sync to SharedState
    SharedStateManager sm;
    if (sm.OpenReadWrite()) {
        SharedState state = sm.Read();
        if (state.IsValid()) {
            state.inputMethod = static_cast<uint8_t>(config.inputMethod);
            state.spellCheck = config.spellCheckEnabled ? 1 : 0;
            state.codeTable = static_cast<uint8_t>(config.codeTable);
            state.SetFeatureFlags(EncodeFeatureFlags(config));
            sm.Write(state);
        }
    }

    // 3. Signal engine to pick up changes
    ConfigEvent event;
    if (event.Initialize()) {
        event.Signal();
    }

    // 4. Notify Settings dialog (if open) to refresh UI
    if (HWND settingsWnd = GetSettingsHwnd()) {
        PostMessageW(settingsWnd, WM_NEXUSKEY_CONFIG_CHANGED, 0, 0);
    }
}

void OnMenuCommand(TrayMenuId id) {
    switch (id) {
        case TrayMenuId::Settings:
            SpawnSettingsSubprocess();
            break;

        case TrayMenuId::About:
            SpawnSubprocess(L"NexusKey - About", L"--about");
            break;

        case TrayMenuId::ToggleMode:
#ifdef NEXUSKEY_HOOK_ENGINE
            g_hookEngine.ToggleVietnameseMode();
#else
            g_sharedState.ToggleFlag(SharedFlags::VIETNAMESE_MODE);
            g_trayIcon.SetVietnameseMode(
                (g_sharedState.ReadFlags() & SharedFlags::VIETNAMESE_MODE) != 0);
#endif
            break;

        case TrayMenuId::SpellCheck: {
            auto config = ConfigManager::LoadOrDefault();
            config.spellCheckEnabled = !config.spellCheckEnabled;
            ApplyConfigChange(config);
            break;
        }

        case TrayMenuId::SmartSwitch: {
            auto config = ConfigManager::LoadOrDefault();
            config.smartSwitch = !config.smartSwitch;
            ApplyConfigChange(config);
            break;
        }

        case TrayMenuId::MacroEnabled: {
            auto config = ConfigManager::LoadOrDefault();
            config.macroEnabled = !config.macroEnabled;
            ApplyConfigChange(config);
            break;
        }

        case TrayMenuId::MacroTable:
            SpawnSubprocess(L"NexusKey - Macro Table", L"--macro");
            break;

        case TrayMenuId::ConvertTool:
            SpawnSubprocess(L"NexusKey - Convert Tool", L"--convert");
            break;

#ifdef NEXUSKEY_HOOK_ENGINE
        case TrayMenuId::QuickConvert:
            if (g_quickConvert) {
                g_quickConvert->Execute();
            }
            break;
#endif

        case TrayMenuId::InputTelex:
        case TrayMenuId::InputVNI:
        case TrayMenuId::InputSimpleTelex: {
            auto config = ConfigManager::LoadOrDefault();
            int method = static_cast<int>(id) - static_cast<int>(TrayMenuId::InputTelex);
            config.inputMethod = static_cast<InputMethod>(method);
            ApplyConfigChange(config);
            break;
        }

        case TrayMenuId::Exit:
            TerminateAllSubprocesses();
            g_running.store(false, std::memory_order_relaxed);
            PostQuitMessage(0);
            break;

        default: {
            // Code table menu items (1010-1014)
            auto rawId = static_cast<UINT>(id);
            if (rawId >= static_cast<UINT>(TrayMenuId::CodeTableUnicode) &&
                rawId <= static_cast<UINT>(TrayMenuId::CodeTableCP1258)) {
                auto ct = static_cast<CodeTable>(rawId - static_cast<UINT>(TrayMenuId::CodeTableUnicode));
#ifdef NEXUSKEY_HOOK_ENGINE
                g_hookEngine.SetCodeTable(ct);
#endif
                // Update SharedState immediately (source of truth at runtime)
                {
                    SharedState state = g_sharedState.Read();
                    if (state.IsValid()) {
                        state.codeTable = static_cast<uint8_t>(ct);
                        g_sharedState.Write(state);
                    }
                }

                // Persist to TOML for next startup
                auto config = ConfigManager::LoadOrDefault();
                config.codeTable = ct;
                (void)ConfigManager::SaveToFile(ConfigManager::GetConfigPath(), config);

                // Notify Settings dialog to refresh UI
                if (HWND settingsWnd = GetSettingsHwnd()) {
                    PostMessageW(settingsWnd, WM_NEXUSKEY_CONFIG_CHANGED, 0, 0);
                }
                NEXTKEY_LOG(L"Code table changed via tray menu: %d", static_cast<int>(ct));
            }
            break;
        }
    }
}

void SpawnSettingsSubprocess() {
    // Check if already open (single-instance)
    HWND existing = GetSettingsHwnd();
    if (existing) {
        SetForegroundWindow(existing);
#ifdef NEXUSKEY_HOOK_ENGINE
        NotifySettingsMode(g_hookEngine.IsVietnameseMode());
#endif
        return;
    }

    // Get exe path
    wchar_t exePath[MAX_PATH];
    GetModuleFileNameW(nullptr, exePath, MAX_PATH);

    // Build command line (pass current V/E mode to subprocess)
    wchar_t cmdLine[MAX_PATH + 64];
#ifdef NEXUSKEY_HOOK_ENGINE
    int mode = g_hookEngine.IsVietnameseMode() ? 1 : 0;
#else
    int mode = (g_sharedState.ReadFlags() & SharedFlags::VIETNAMESE_MODE) ? 1 : 0;
#endif
    swprintf_s(cmdLine, L"\"%s\" --settings --mode %d", exePath, mode);

    // Spawn subprocess
    STARTUPINFOW si = { sizeof(si) };
    PROCESS_INFORMATION pi;

    NEXTKEY_LOG(L"Spawning settings with cmdLine: %s", cmdLine);

    if (CreateProcessW(nullptr, cmdLine, nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi)) {
        CloseHandle(pi.hThread);
        TrackChildProcess(pi.hProcess);
        NEXTKEY_LOG(L"Settings subprocess spawned successfully");
    } else {
        NEXTKEY_LOG(L"Failed to spawn settings subprocess (error=%lu, path=%s)", GetLastError(), exePath);
    }
}
