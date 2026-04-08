// NexusKey Classic — Lite build entry point
// SPDX-License-Identifier: GPL-3.0-only
//
// Simplified entry point for the Classic (Win32 native) UI build.
// No Sciter dependency. Uses HookEngine + TrayIcon + ClassicSettingsDialog.

#include "core/Version.h"
#include "core/config/ConfigManager.h"
#include "core/ipc/SharedState.h"
#include "core/ipc/SharedStateManager.h"
#include "core/ipc/SharedConstants.h"
#include "core/config/ConfigEvent.h"
#include "core/Strings.h"
#include "core/SystemConfig.h"
#include "core/Debug.h"

#include "system/HookEngine.h"
#include "system/QuickConvert.h"
#include "system/TrayIcon.h"
#include "system/FloatingIcon.h"
#include "system/TsfRegistration.h"
#include "system/StartupHelper.h"
#include "system/UpdateChecker.h"
#include "system/UpdateInstaller.h"
#include "system/ToastPopup.h"

#include "classic/ClassicSettingsDialog.h"

#include <Windows.h>
#include <commctrl.h>
#include <ole2.h>
#include <timeapi.h>
#include <memory>
#include <string>
#include <atomic>
#include <thread>

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "winmm.lib")

// Common Controls v6 manifest is embedded via NexusKeyLite.rc + .exe.manifest
// (do NOT add #pragma manifestdependency here — causes duplicate MANIFEST resource)

using namespace NextKey;

// ═══════════════════════════════════════════════════════════
// Global State
// ═══════════════════════════════════════════════════════════

static std::atomic<bool> g_running{true};
static TrayIcon g_trayIcon;
static FloatingIcon g_floatingIcon;
static HookEngine g_hookEngine;
static SharedStateManager g_sharedState;
static std::unique_ptr<QuickConvert> g_quickConvert;
static HINSTANCE g_hInstance = nullptr;

// Forward declarations
static void SpawnSettingsDialog();
static void OnMenuCommand(TrayMenuId id);
static void EnsureFloatingIconCreated();
static void InitFloatingIcon(HINSTANCE hInstance, const SystemConfig& sc);
static void CleanupFloatingIcon() noexcept;

// ═══════════════════════════════════════════════════════════
// Settings Dialog — in-process (no subprocess)
// ═══════════════════════════════════════════════════════════

/// ClassicSettingsDialog state — one instance at a time.
/// Dialog runs its own modal message loop inside Show().
static bool g_settingsOpen = false;

static void SpawnSettingsDialog() {
    if (g_settingsOpen) {
        // Already open — try to bring to front
        return;
    }

    g_settingsOpen = true;

    // Run in a separate thread so the main message loop keeps running
    // (tray icon, hook engine callbacks, etc. must remain responsive).
    std::thread([]() {
        NextKey::Classic::ClassicSettingsDialog dialog;
        dialog.Show(g_hInstance);
        g_settingsOpen = false;

        // After dialog closes, reload config in case settings changed
        auto config = ConfigManager::LoadOrDefault();
        auto hotkeyConfig = ConfigManager::LoadHotkeyConfigOrDefault();
        g_hookEngine.SetConvertHotkey(hotkeyConfig);

        // Reload convert config for QuickConvert
        if (g_quickConvert) {
            auto cc = ConfigManager::LoadConvertConfigOrDefault();
            g_quickConvert->UpdateConfig(cc);
        }

        // Refresh floating icon config
        auto sysConfig = ConfigManager::LoadSystemConfigOrDefault();
        if (sysConfig.showFloatingIcon) {
            EnsureFloatingIconCreated();
            g_floatingIcon.SetVisible(true);
        } else {
            g_floatingIcon.Destroy();
        }

        // Refresh tray icon style
        g_trayIcon.SetIconConfig(sysConfig.iconStyle, sysConfig.customColorV, sysConfig.customColorE);
    }).detach();
}

// ═══════════════════════════════════════════════════════════
// Floating Icon Helpers
// ═══════════════════════════════════════════════════════════

static void EnsureFloatingIconCreated() {
    if (g_floatingIcon.IsCreated()) return;
    (void)g_floatingIcon.Create(g_hInstance);
}

static void InitFloatingIcon(HINSTANCE hInstance, const SystemConfig& sc) {
    if (sc.showFloatingIcon) {
        if (g_floatingIcon.Create(hInstance)) {
            g_floatingIcon.SetPosition(sc.floatingIconX, sc.floatingIconY);
            g_floatingIcon.SetVisible(true);
        }
    }

    g_trayIcon.SetIconConfigChangedCallback([]() {
        auto sysConfig = ConfigManager::LoadSystemConfigOrDefault();
        if (sysConfig.showFloatingIcon) {
            EnsureFloatingIconCreated();
            g_floatingIcon.SetVisible(true);
        } else {
            g_floatingIcon.Destroy();
        }
    });
}

static void CleanupFloatingIcon() noexcept {
    if (g_floatingIcon.GetPosX() != INT32_MIN) {
        auto sysConfig = ConfigManager::LoadSystemConfigOrDefault();
        sysConfig.floatingIconX = g_floatingIcon.GetPosX();
        sysConfig.floatingIconY = g_floatingIcon.GetPosY();
        (void)ConfigManager::SaveSystemConfig(ConfigManager::GetConfigPath(), sysConfig);
    }
    g_floatingIcon.Destroy();
}

// ═══════════════════════════════════════════════════════════
// Config Change Helper
// ═══════════════════════════════════════════════════════════

/// Save config to TOML + sync SharedState + signal ConfigEvent.
/// Used by tray menu toggle handlers to propagate changes.
static void ApplyConfigChange(const TypingConfig& config) {
    (void)ConfigManager::SaveToFile(ConfigManager::GetConfigPath(), config);

    SharedState state = g_sharedState.Read();
    if (state.IsValid()) {
        state.inputMethod = static_cast<uint8_t>(config.inputMethod);
        state.spellCheck = config.spellCheckEnabled ? 1 : 0;
        state.codeTable = static_cast<uint8_t>(config.codeTable);
        state.SetFeatureFlags(EncodeFeatureFlags(config));
        state.configGeneration++;
        g_sharedState.Write(state);
    }

    ConfigEvent event;
    if (event.Initialize()) {
        event.Signal();
    }
}

// ═══════════════════════════════════════════════════════════
// Tray Menu Handler
// ═══════════════════════════════════════════════════════════

static void OnMenuCommand(TrayMenuId id) {
    switch (id) {
        case TrayMenuId::Settings:
            SpawnSettingsDialog();
            break;

        case TrayMenuId::About:
            // Lite build: simple MessageBox about dialog
            MessageBoxW(nullptr,
                L"NexusKey Classic\n"
                L"Vietnamese Input Method Editor\n\n"
                L"https://github.com/phatMT97/NextKey",
                L"NexusKey", MB_ICONINFORMATION);
            break;

        case TrayMenuId::ToggleMode:
            g_hookEngine.ToggleVietnameseMode();
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
            // Lite build: no macro table dialog yet
            break;

        case TrayMenuId::ConvertTool:
            // Lite build: no convert tool dialog yet
            break;

        case TrayMenuId::QuickConvert:
            if (g_quickConvert) {
                g_quickConvert->Execute();
            }
            break;

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
            g_running.store(false, std::memory_order_relaxed);
            PostQuitMessage(0);
            break;

        default: {
            // Code table menu items (1010-1014)
            auto rawId = static_cast<UINT>(id);
            if (rawId >= static_cast<UINT>(TrayMenuId::CodeTableUnicode) &&
                rawId <= static_cast<UINT>(TrayMenuId::CodeTableCP1258)) {
                auto ct = static_cast<CodeTable>(rawId - static_cast<UINT>(TrayMenuId::CodeTableUnicode));
                g_hookEngine.SetCodeTable(ct);

                SharedState state = g_sharedState.Read();
                if (state.IsValid()) {
                    state.codeTable = static_cast<uint8_t>(ct);
                    g_sharedState.Write(state);
                }

                auto config = ConfigManager::LoadOrDefault();
                config.codeTable = ct;
                (void)ConfigManager::SaveToFile(ConfigManager::GetConfigPath(), config);
            }
            break;
        }
    }
}

// ═══════════════════════════════════════════════════════════
// Entry Point
// ═══════════════════════════════════════════════════════════

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, LPWSTR lpCmdLine, int) {
    g_hInstance = hInstance;

    // ── Command-line routes (shared with main build) ──

    if (lpCmdLine && wcsstr(lpCmdLine, L"--unregister-tsf") != nullptr) {
        CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        bool ok = UnregisterTsf();
        CoUninitialize();
        return ok ? 0 : 1;
    }
    if (lpCmdLine && wcsstr(lpCmdLine, L"--register-tsf") != nullptr) {
        CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        bool ok = RegisterTsf();
        CoUninitialize();
        return ok ? 0 : 1;
    }
    if (lpCmdLine && wcsstr(lpCmdLine, L"--diag") != nullptr) {
        RunDiagnostics();
        return 0;
    }

    // Self-update installer mode
    if (lpCmdLine && wcsstr(lpCmdLine, L"--install-update") != nullptr) {
        const wchar_t* arg = wcsstr(lpCmdLine, L"--install-update");
        arg += wcslen(L"--install-update");
        while (*arg == L' ' || *arg == L'\t') arg++;
        std::wstring zipPath(arg);
        if (zipPath.size() >= 2 && zipPath.front() == L'"' && zipPath.back() == L'"') {
            zipPath = zipPath.substr(1, zipPath.size() - 2);
        }
        if (!zipPath.empty()) {
            wchar_t resolvedPath[MAX_PATH] = {};
            if (!GetFullPathNameW(zipPath.c_str(), MAX_PATH, resolvedPath, nullptr)) {
                return 1;
            }
            wchar_t tempDir[MAX_PATH] = {};
            GetTempPathW(MAX_PATH, tempDir);
            int cmpLen = static_cast<int>(wcslen(tempDir));
            int resolvedLen = static_cast<int>(wcslen(resolvedPath));
            if (resolvedLen < cmpLen ||
                CompareStringOrdinal(resolvedPath, cmpLen, tempDir, cmpLen, TRUE) != CSTR_EQUAL) {
                return 1;
            }
            RunUpdateInstaller(resolvedPath);
        }
        return 1;
    }

    // ── Single instance check ──

    // NOTE: Use default DACL (nullptr). CO SID doesn't resolve for non-container objects.
    HANDLE hMutex = CreateMutexW(nullptr, TRUE, L"Local\\NexusKeyLite_Main_Mutex");
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        NEXTKEY_LOG(L"Another Lite instance is already running. Exiting.");
        CloseHandle(hMutex);
        return 0;
    }

    // ── Initialization ──

    OleInitialize(nullptr);
    INITCOMMONCONTROLSEX icc = { sizeof(icc), ICC_TAB_CLASSES | ICC_STANDARD_CLASSES };
    InitCommonControlsEx(&icc);

    // Remove any HKCU CLSID override that malware may have planted
    CleanupHkcuClsidOverride();

    // Load config
    auto config = ConfigManager::LoadOrDefault();
    auto hotkeyConfig = ConfigManager::LoadHotkeyConfigOrDefault();

    // Initialize UI language
    auto systemConfig = ConfigManager::LoadSystemConfigOrDefault();
    SetLanguage(static_cast<Language>(systemConfig.language));

    // Clean up leftover update files
    bool updateJustCompleted = CleanupOldUpdateFiles();
    if (updateJustCompleted) {
        NEXTKEY_LOG(L"Update completed successfully, old files cleaned up");
    }

    // Check for update failure marker
    bool updateJustFailed = false;
    {
        wchar_t exePath[MAX_PATH] = {};
        GetModuleFileNameW(nullptr, exePath, MAX_PATH);
        std::wstring exeDir(exePath);
        auto pos = exeDir.find_last_of(L"\\/");
        if (pos != std::wstring::npos) exeDir = exeDir.substr(0, pos);
        std::wstring markerPath = exeDir + L"\\_update_failed";
        if (DeleteFileW(markerPath.c_str())) {
            updateJustFailed = true;
            NEXTKEY_LOG(L"Update failed marker found and cleaned up");
        }
    }

    // ── SharedState ──

    if (g_sharedState.Create()) {
        SharedState state;
        state.InitDefaults();
        state.inputMethod = static_cast<uint8_t>(config.inputMethod);
        state.spellCheck = config.spellCheckEnabled ? 1 : 0;
        state.optimizeLevel = config.optimizeLevel;
        state.codeTable = static_cast<uint8_t>(config.codeTable);
        state.SetFeatureFlags(EncodeFeatureFlags(config));
        state.SetHotkey(hotkeyConfig);
        g_sharedState.Write(state);
        NEXTKEY_LOG(L"SharedState created for Lite mode");
    }

    // ── Tray Icon ──

    if (!g_trayIcon.Create(hInstance)) {
        MessageBoxW(nullptr, L"Failed to create tray icon", L"NexusKey", MB_ICONERROR);
        OleUninitialize();
        CloseHandle(hMutex);
        return 1;
    }
    g_trayIcon.SetMenuCallback(OnMenuCommand);

    // Wire mode change callback: HookEngine -> tray icon + SharedState + floating icon
    g_hookEngine.SetModeChangeCallback([](bool vietnamese) {
        g_sharedState.SetOrClearFlag(SharedFlags::VIETNAMESE_MODE, vietnamese);
        HWND trayWnd = g_trayIcon.GetMessageWindow();
        if (trayWnd) {
            PostMessageW(trayWnd, WM_NEXUSKEY_TRAY_MODE_SYNC, vietnamese ? 1 : 0, 0);
        }
        g_floatingIcon.SetVietnameseMode(vietnamese);
    });

    // Wire TSF active callback: HookEngine -> SharedState flag for DLL
    g_hookEngine.SetTsfActiveCallback([](bool active) {
        g_sharedState.SetOrClearFlag(SharedFlags::TSF_ACTIVE, active);
    });

    // Wire settings dialog -> HookEngine mode set
    g_trayIcon.SetModeRequestCallback([](bool vietnamese) {
        if (g_hookEngine.IsVietnameseMode() != vietnamese) {
            g_hookEngine.ToggleVietnameseMode();
        }
    });

    // Wire menu state getter
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

    // ── QuickConvert ──

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

    // ── Timer resolution ──

    // Set 1ms timer resolution for smooth Vietnamese input
    timeBeginPeriod(1);

    // ── HookEngine ──

    g_hookEngine.SetSharedStateReader(&g_sharedState);

    if (!g_hookEngine.Start(hInstance, config, hotkeyConfig)) {
        timeEndPeriod(1);
        MessageBoxW(nullptr, L"Failed to install keyboard hook", L"NexusKey", MB_ICONERROR);
        OleUninitialize();
        CloseHandle(hMutex);
        return 1;
    }

    NEXTKEY_LOG(L"HookEngine started (Lite mode), entering message loop");

    // ── Icon config + floating icon ──

    g_trayIcon.SetIconConfig(systemConfig.iconStyle, systemConfig.customColorV, systemConfig.customColorE);
    InitFloatingIcon(hInstance, systemConfig);

    // ── Show settings on startup if configured ──

    if (systemConfig.showOnStartup) {
        SpawnSettingsDialog();
    }

    // ── Post-update notifications ──

    if (updateJustCompleted) {
        std::thread([]() {
            Sleep(1000);
            ToastPopup::Show(S(StringId::UPDATE_SUCCESS), 3000);
        }).detach();
    } else if (updateJustFailed) {
        std::thread([]() {
            Sleep(1000);
            ToastPopup::Show(S(StringId::UPDATE_INSTALL_FAILED), 3000);
        }).detach();
    }

    // ── Auto-check for updates ──

    if (systemConfig.autoCheckUpdate) {
        std::thread([]() {
            Sleep(3000);
            auto info = UpdateChecker::CheckForUpdate();
            if (info.available) {
                HWND trayWnd = FindWindowW(L"NexusKeyTrayClass", nullptr);
                if (trayWnd) {
                    auto* pInfo = new (std::nothrow) UpdateInfo(std::move(info));
                    if (pInfo) {
                        // WndProc returns true (1) on success and takes ownership of pInfo.
                        // If window was destroyed, SendMessageW returns 0 — we still own pInfo.
                        if (!SendMessageW(trayWnd, WM_NEXUSKEY_UPDATE_AVAILABLE, 0,
                                          reinterpret_cast<LPARAM>(pInfo))) {
                            delete pInfo;
                        }
                    }
                }
            }
        }).detach();
    }

    // ── Message Loop ──

    MSG msg;
    while (g_running.load(std::memory_order_relaxed) && GetMessageW(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    // ── Cleanup ──

    CleanupFloatingIcon();
    g_hookEngine.Stop();
    timeEndPeriod(1);
    g_trayIcon.Destroy();

    NEXTKEY_LOG(L"Exiting (Lite mode)");

    OleUninitialize();
    CloseHandle(hMutex);
    return 0;
}
