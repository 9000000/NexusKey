// NexusKey - Core Application Entry Point
// SPDX-License-Identifier: GPL-3.0-only

#include "TrayIcon.h"
#include "SettingsDialog.h"
#include "ExcludedAppsDialog.h"
#include "SubprocessHelper.h"
#include "core/TypingConfig.h"
#include "core/config/ConfigManager.h"
#include "core/Debug.h"

#ifdef NEXUSKEY_HOOK_ENGINE
#include "HookEngine.h"
#include "core/SharedStateManager.h"
#else
#include "HotkeyManager.h"
#include "core/SharedStateManager.h"
#include "tsf/Globals.h"
#endif

#include <Windows.h>
#include <ole2.h>
#include <string>
#include <vector>
#include <atomic>

#pragma comment(lib, "ole32.lib")

#ifndef NEXUSKEY_HOOK_ENGINE
// TSF Registration functions (only needed for TSF mode)
#endif

namespace {

#ifndef NEXUSKEY_HOOK_ENGINE
using DllRegisterServerFn = HRESULT(STDAPICALLTYPE*)();

/// Get path to NextKeyTSF.dll (same directory as exe)
std::wstring GetTsfDllPath() {
    wchar_t exePath[MAX_PATH];
    GetModuleFileNameW(nullptr, exePath, MAX_PATH);
    std::wstring path(exePath);
    size_t pos = path.find_last_of(L"\\/");
    if (pos != std::wstring::npos) {
        path = path.substr(0, pos + 1);
    }
    return path + L"NextKeyTSF.dll";
}

/// Check if TSF is registered by looking for CLSID in registry
bool IsTsfRegistered() {
    // Use shared CLSID string from Globals.h
    std::wstring keyPath = L"CLSID\\";
    keyPath += NextKey::TSF::CLSID_TEXTSERVICE_STRING;

    HKEY hKey;
    LSTATUS ls = RegOpenKeyExW(HKEY_CLASSES_ROOT, keyPath.c_str(), 0, KEY_READ, &hKey);
    if (ls == ERROR_SUCCESS) {
        RegCloseKey(hKey);
        return true;
    }
    return false;
}

/// Register TSF DLL (requires admin for HKEY_CLASSES_ROOT)
bool RegisterTsf() {
    std::wstring dllPath = GetTsfDllPath();

    HMODULE hDll = LoadLibraryW(dllPath.c_str());
    if (!hDll) {
        NEXTKEY_LOG(L"Failed to load NextKeyTSF.dll (error: %lu)", GetLastError());
        return false;
    }

    auto pRegister = reinterpret_cast<DllRegisterServerFn>(
        GetProcAddress(hDll, "DllRegisterServer")
    );

    bool success = false;
    if (pRegister) {
        HRESULT hr = pRegister();
        success = SUCCEEDED(hr);
        if (success) {
            NEXTKEY_LOG(L"TSF registered successfully");
        } else {
            NEXTKEY_LOG(L"TSF registration failed (hr: 0x%08X)", hr);
        }
    }

    FreeLibrary(hDll);
    return success;
}

/// Unregister TSF DLL
bool UnregisterTsf() {
    std::wstring dllPath = GetTsfDllPath();

    HMODULE hDll = LoadLibraryW(dllPath.c_str());
    if (!hDll) {
        NEXTKEY_LOG(L"Failed to load NextKeyTSF.dll for unregister");
        return false;
    }

    auto pUnregister = reinterpret_cast<DllRegisterServerFn>(
        GetProcAddress(hDll, "DllUnregisterServer")
    );

    bool success = false;
    if (pUnregister) {
        HRESULT hr = pUnregister();
        success = SUCCEEDED(hr);
        if (success) {
            NEXTKEY_LOG(L"TSF unregistered successfully");
        } else {
            NEXTKEY_LOG(L"TSF unregister failed (hr: 0x%08X)", hr);
        }
    }

    FreeLibrary(hDll);
    return success;
}

/// Run registration with admin elevation via ShellExecute
bool RegisterTsfElevated() {
    wchar_t exePath[MAX_PATH];
    GetModuleFileNameW(nullptr, exePath, MAX_PATH);

    SHELLEXECUTEINFOW sei = { sizeof(sei) };
    sei.lpVerb = L"runas";
    sei.lpFile = exePath;
    sei.lpParameters = L"--register-tsf";
    sei.nShow = SW_HIDE;
    sei.fMask = SEE_MASK_NOCLOSEPROCESS;

    if (ShellExecuteExW(&sei)) {
        if (sei.hProcess) {
            WaitForSingleObject(sei.hProcess, 10000);  // Wait up to 10 seconds
            CloseHandle(sei.hProcess);
        }
        return IsTsfRegistered();
    }
    return false;
}

/// Run unregistration with admin elevation
bool UnregisterTsfElevated() {
    wchar_t exePath[MAX_PATH];
    GetModuleFileNameW(nullptr, exePath, MAX_PATH);

    SHELLEXECUTEINFOW sei = { sizeof(sei) };
    sei.lpVerb = L"runas";
    sei.lpFile = exePath;
    sei.lpParameters = L"--unregister-tsf";
    sei.nShow = SW_HIDE;
    sei.fMask = SEE_MASK_NOCLOSEPROCESS;

    if (ShellExecuteExW(&sei)) {
        if (sei.hProcess) {
            WaitForSingleObject(sei.hProcess, 10000);
            CloseHandle(sei.hProcess);
        }
        return !IsTsfRegistered();
    }
    return false;
}

#endif  // !NEXUSKEY_HOOK_ENGINE

#ifndef NEXUSKEY_HOOK_ENGINE
/// Diagnostic output — enumerates HKLs, TSF profiles, active profile
void RunDiagnostics() {
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

    std::wstring out;
    out += L"=== NexusKey Diagnostics ===\n\n";

    // 1. TSF Registration check
    out += IsTsfRegistered() ? L"[OK] TSF registered\n" : L"[FAIL] TSF NOT registered\n";

    // 2. Enumerate all keyboard layouts (HKLs)
    out += L"\n--- Keyboard Layouts (HKLs) ---\n";
    int count = GetKeyboardLayoutList(0, nullptr);
    if (count > 0) {
        std::vector<HKL> hklList(count);
        GetKeyboardLayoutList(count, hklList.data());
        for (int i = 0; i < count; i++) {
            auto hklVal = reinterpret_cast<DWORD_PTR>(hklList[i]);
            bool isTip = (HIWORD(hklVal) >= 0xF000);
            wchar_t buf[128];
            swprintf_s(buf, L"  HKL[%d] = 0x%08IX  LOWORD=0x%04X  HIWORD=0x%04X  %s\n",
                        i, hklVal, LOWORD(hklVal), HIWORD(hklVal),
                        isTip ? L"(TIP substitute)" : L"(keyboard layout)");
            out += buf;
        }
    } else {
        out += L"  (none found)\n";
    }

    // 3. Current thread HKL
    {
        HKL cur = GetKeyboardLayout(0);
        wchar_t buf[128];
        swprintf_s(buf, L"\nCurrent thread HKL: 0x%08IX\n",
                    reinterpret_cast<DWORD_PTR>(cur));
        out += buf;
    }

    // 4. Foreground thread HKL
    {
        HWND fg = GetForegroundWindow();
        if (fg) {
            DWORD tid = GetWindowThreadProcessId(fg, nullptr);
            HKL fgHkl = GetKeyboardLayout(tid);
            wchar_t buf[128];
            swprintf_s(buf, L"Foreground thread HKL: 0x%08IX (tid=%lu)\n",
                        reinterpret_cast<DWORD_PTR>(fgHkl), tid);
            out += buf;
        }
    }

    // 5. TSF Active Profile
    out += L"\n--- TSF Active Profile ---\n";
    ITfInputProcessorProfileMgr* pProfileMgr = nullptr;
    HRESULT hr = CoCreateInstance(
        CLSID_TF_InputProcessorProfiles, nullptr, CLSCTX_INPROC_SERVER,
        IID_ITfInputProcessorProfileMgr,
        reinterpret_cast<void**>(&pProfileMgr));
    if (SUCCEEDED(hr) && pProfileMgr) {
        TF_INPUTPROCESSORPROFILE activeProfile = {};
        hr = pProfileMgr->GetActiveProfile(GUID_TFCAT_TIP_KEYBOARD, &activeProfile);
        if (SUCCEEDED(hr)) {
            wchar_t buf[256];
            swprintf_s(buf, L"  Type=%lu  LangID=0x%04X  HKL=0x%08IX\n",
                        activeProfile.dwProfileType, activeProfile.langid,
                        reinterpret_cast<DWORD_PTR>(activeProfile.hkl));
            out += buf;

            // Check CLSID
            wchar_t clsidStr[64];
            StringFromGUID2(activeProfile.clsid, clsidStr, 64);
            out += L"  CLSID=";
            out += clsidStr;
            out += L"\n";

            // NexusKey CLSID for comparison
            static const GUID CLSID_NK = {
                0xD84D1E5B, 0x8F2C, 0x4B1A,
                {0x9D, 0x3E, 0x6F, 0x7A, 0x8B, 0x9C, 0x0D, 0x1E}
            };
            out += IsEqualCLSID(activeProfile.clsid, CLSID_NK)
                ? L"  → This IS NexusKey\n"
                : L"  → This is NOT NexusKey\n";
        } else {
            out += L"  GetActiveProfile failed\n";
        }

        // 6. Enumerate all profiles for 0x0409
        out += L"\n--- All 0x0409 Profiles ---\n";
        IEnumTfInputProcessorProfiles* pEnum = nullptr;
        hr = pProfileMgr->EnumProfiles(0x0409, &pEnum);
        if (SUCCEEDED(hr) && pEnum) {
            TF_INPUTPROCESSORPROFILE profile;
            ULONG fetched = 0;
            int idx = 0;
            while (pEnum->Next(1, &profile, &fetched) == S_OK && fetched == 1) {
                wchar_t clsidStr2[64];
                StringFromGUID2(profile.clsid, clsidStr2, 64);
                wchar_t buf2[256];
                swprintf_s(buf2, L"  [%d] type=%lu  hkl=0x%08IX  clsid=%s\n",
                            idx++, profile.dwProfileType,
                            reinterpret_cast<DWORD_PTR>(profile.hkl), clsidStr2);
                out += buf2;
            }
            pEnum->Release();
        }

        pProfileMgr->Release();
    } else {
        out += L"  Failed to create ITfInputProcessorProfileMgr\n";
    }

    // 7. SharedState check
    out += L"\n--- SharedState ---\n";
    {
        NextKey::SharedStateManager sm;
        if (sm.Open()) {
            NextKey::SharedState state = sm.Read();
            if (state.IsValid()) {
                wchar_t buf[256];
                swprintf_s(buf, L"  magic=0x%08X  epoch=%u  flags=0x%08X\n"
                                L"  VIETNAMESE_MODE=%d  ENGINE_ENABLED=%d\n"
                                L"  inputMethod=%d  spellCheck=%d\n",
                            state.magic, state.epoch, state.flags,
                            (state.flags & NextKey::SharedFlags::VIETNAMESE_MODE) ? 1 : 0,
                            (state.flags & NextKey::SharedFlags::ENGINE_ENABLED) ? 1 : 0,
                            state.inputMethod, state.spellCheck);
                out += buf;
            } else {
                out += L"  SharedState invalid (magic mismatch)\n";
            }
        } else {
            out += L"  SharedState not available (EXE not running?)\n";
        }
    }

    CoUninitialize();
    MessageBoxW(nullptr, out.c_str(), L"NexusKey Diagnostics", MB_OK | MB_ICONINFORMATION);
}
#endif  // !NEXUSKEY_HOOK_ENGINE

}  // anonymous namespace

using namespace NextKey;

// Forward declarations
void SpawnSettingsSubprocess();
[[noreturn]] void RunSettingsSubprocess();
[[noreturn]] void RunExcludedAppsSubprocess();
void CloseAllNexusKeyWindows();

// Callback for EnumWindows - closes windows belonging to our executable
static BOOL CALLBACK CloseNexusKeyWindowsProc(HWND hwnd, LPARAM lParam) {
    const wchar_t* ourExePath = reinterpret_cast<const wchar_t*>(lParam);

    // Get the process that owns this window
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (pid == 0) return TRUE;

    // Open process to get its executable path
    HANDLE hProcess = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!hProcess) return TRUE;

    wchar_t processPath[MAX_PATH];
    DWORD pathLen = MAX_PATH;
    BOOL gotPath = QueryFullProcessImageNameW(hProcess, 0, processPath, &pathLen);
    CloseHandle(hProcess);

    if (gotPath && _wcsicmp(processPath, ourExePath) == 0) {
        // This window belongs to our executable - close it
        PostMessageW(hwnd, WM_CLOSE, 0, 0);
    }
    return TRUE;  // Continue enumeration
}

void CloseAllNexusKeyWindows() {
    wchar_t exePath[MAX_PATH];
    GetModuleFileNameW(nullptr, exePath, MAX_PATH);
    EnumWindows(CloseNexusKeyWindowsProc, reinterpret_cast<LPARAM>(exePath));
}

// Global state
static std::atomic<bool> g_running{true};
static TrayIcon g_trayIcon;
static HINSTANCE g_hInstance = nullptr;

#ifdef NEXUSKEY_HOOK_ENGINE
static HookEngine g_hookEngine;
static SharedStateManager g_sharedState;  // Shared memory for Settings subprocess IPC
#else
static SharedStateManager g_sharedState;
static HotkeyManager g_hotkeyManager;
#endif

// Forward declaration
void OnMenuCommand(TrayMenuId id);

#ifndef NEXUSKEY_HOOK_ENGINE
// ═══════════════════════════════════════════════════════════
// V/E icon sync: 250ms poll of SharedState flags (atomic read, no IPC)
// ═══════════════════════════════════════════════════════════
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

#ifndef NEXUSKEY_HOOK_ENGINE
    // TSF Registration (runs elevated, then exits)
    if (lpCmdLine && wcsstr(lpCmdLine, L"--register-tsf") != nullptr) {
        CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        bool ok = RegisterTsf();
        CoUninitialize();
        return ok ? 0 : 1;
    }

    // TSF Unregistration (runs elevated, then exits)
    if (lpCmdLine && wcsstr(lpCmdLine, L"--unregister-tsf") != nullptr) {
        CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        bool ok = UnregisterTsf();
        CoUninitialize();
        return ok ? 0 : 1;
    }
#endif

#ifndef NEXUSKEY_HOOK_ENGINE
    // Diagnostics mode (shows HKL/TSF/SharedState info in MessageBox)
    if (lpCmdLine && wcsstr(lpCmdLine, L"--diag") != nullptr) {
        RunDiagnostics();
        return 0;
    }
#endif

    // Settings subprocess (Sciter dialog)
    if (lpCmdLine && wcsstr(lpCmdLine, L"--settings") != nullptr) {
        RunSettingsSubprocess();  // [[noreturn]] - never returns
    }

    // Excluded Apps subprocess (Sciter dialog)
    if (lpCmdLine && wcsstr(lpCmdLine, L"--excludedapps") != nullptr) {
        RunExcludedAppsSubprocess();  // [[noreturn]] - never returns
    }

    // ═══════════════════════════════════════════════════════════
    // Main Process
    // ═══════════════════════════════════════════════════════════

    // Load config
    auto config = ConfigManager::LoadOrDefault();
    auto hotkeyConfig = ConfigManager::LoadHotkeyConfigOrDefault();

#ifdef NEXUSKEY_HOOK_ENGINE
    // ═══════════════════════════════════════════════════════════
    // Hook Engine Mode — single process, no DLL/COM needed
    // ═══════════════════════════════════════════════════════════

    // Create SharedState for Settings subprocess IPC
    // (Settings writes featureFlags here; HookEngine reads on ConfigEvent)
    if (g_sharedState.Create()) {
        SharedState state;
        state.InitDefaults();
        state.inputMethod = static_cast<uint8_t>(config.inputMethod);
        state.spellCheck = config.spellCheckEnabled ? 1 : 0;
        state.optimizeLevel = config.optimizeLevel;
        state.featureFlags = EncodeFeatureFlags(config);
        g_sharedState.Write(state);
        NEXTKEY_LOG(L"SharedState created for HookEngine mode");
    }

    // Tray Icon
    if (!g_trayIcon.Create(hInstance)) {
        MessageBoxW(nullptr, L"Failed to create tray icon", L"NexusKey", MB_ICONERROR);
        return 1;
    }
    g_trayIcon.SetMenuCallback(OnMenuCommand);

    // Wire mode change callback: HookEngine → tray icon + settings dialog
    g_hookEngine.SetModeChangeCallback([](bool vietnamese) {
        g_trayIcon.SetVietnameseMode(vietnamese);
        // Notify settings dialog (if open) of mode change
        HWND settingsWnd = FindWindowW(nullptr, L"NexusKey Settings");
        if (settingsWnd) {
            PostMessageW(settingsWnd, WM_NEXUSKEY_MODE_CHANGED, vietnamese ? 1 : 0, 0);
        }
    });

    // Wire settings dialog → HookEngine mode set (cross-process)
    g_trayIcon.SetModeRequestCallback([](bool vietnamese) {
        if (g_hookEngine.IsVietnameseMode() != vietnamese) {
            g_hookEngine.ToggleVietnameseMode();
        }
    });

    // Start keyboard hook engine
    if (!g_hookEngine.Start(hInstance, config, hotkeyConfig)) {
        MessageBoxW(nullptr, L"Failed to install keyboard hook", L"NexusKey", MB_ICONERROR);
        return 1;
    }

    NEXTKEY_LOG(L"HookEngine started, entering message loop");

    MSG msg;
    while (g_running.load(std::memory_order_relaxed) && GetMessageW(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    // Cleanup
    g_hookEngine.Stop();

#else
    // ═══════════════════════════════════════════════════════════
    // TSF Mode — SharedState IPC + DLL
    // ═══════════════════════════════════════════════════════════

    // Initialize COM for TSF registration check
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

    // Check and register TSF if needed
    if (!IsTsfRegistered()) {
        NEXTKEY_LOG(L"TSF not registered, attempting registration...");

        // Try direct registration first (may fail without admin)
        if (!RegisterTsf()) {
            // Ask user for elevation
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
        // Write config to shared state
        SharedState state;
        state.InitDefaults();
        state.inputMethod = static_cast<uint8_t>(config.inputMethod);
        state.spellCheck = config.spellCheckEnabled ? 1 : 0;
        state.optimizeLevel = config.optimizeLevel;
        state.featureFlags = EncodeFeatureFlags(config);
        g_sharedState.Write(state);
        NEXTKEY_LOG(L"SharedState created and initialized");
    }

    // Tray Icon
    if (!g_trayIcon.Create(hInstance)) {
        MessageBoxW(nullptr, L"Failed to create tray icon", L"NexusKey", MB_ICONERROR);
        CoUninitialize();
        return 1;
    }
    g_trayIcon.SetMenuCallback(OnMenuCommand);

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

void OnMenuCommand(TrayMenuId id) {
    switch (id) {
        case TrayMenuId::Settings:
            SpawnSettingsSubprocess();
            break;

        case TrayMenuId::About:
            MessageBoxW(nullptr,
                L"NexusKey Vietnamese Input\n"
                L"Version 1.0.0\n\n"
                L"A modern Vietnamese typing solution for Windows.\n\n"
                L"SPDX-License-Identifier: GPL-3.0-only",
                L"About NexusKey", MB_ICONINFORMATION);
            break;

        case TrayMenuId::ToggleMode:
#ifdef NEXUSKEY_HOOK_ENGINE
            g_hookEngine.ToggleVietnameseMode();
#else
            // Atomic toggle — DLL and 250ms poll both see the change
            g_sharedState.ToggleFlag(SharedFlags::VIETNAMESE_MODE);
            // Update icon immediately (don't wait for poll)
            g_trayIcon.SetVietnameseMode(
                (g_sharedState.ReadFlags() & SharedFlags::VIETNAMESE_MODE) != 0);
#endif
            break;

        case TrayMenuId::Exit:
            CloseAllNexusKeyWindows();
            g_running.store(false, std::memory_order_relaxed);
            PostQuitMessage(0);
            break;
    }
}

void SpawnSettingsSubprocess() {
    // Check if already open (single-instance)
    HWND existing = FindWindowW(nullptr, L"NexusKey Settings");
    if (existing) {
        SetForegroundWindow(existing);
        // Sync current V/E mode to existing settings window
#ifdef NEXUSKEY_HOOK_ENGINE
        PostMessageW(existing, WM_NEXUSKEY_MODE_CHANGED,
                     g_hookEngine.IsVietnameseMode() ? 1 : 0, 0);
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
        CloseHandle(pi.hProcess);
        NEXTKEY_LOG(L"Settings subprocess spawned successfully");
    } else {
        DWORD err = GetLastError();
        wchar_t errMsg[512];
        swprintf_s(errMsg, L"Failed to open settings.\nError: %lu\nPath: %s", err, exePath);
        MessageBoxW(nullptr, errMsg, L"NexusKey", MB_ICONERROR);
    }
}

[[noreturn]] void RunSettingsSubprocess() {
    NEXTKEY_LOG(L"Running settings subprocess");

    InitSciterSubprocess();
    NEXTKEY_LOG(L"Sciter initialized, creating dialog...");

    // Parse initial V/E mode from command line (--mode 0 or --mode 1)
    bool initialVietnamese = true;
    const wchar_t* cmdLine = GetCommandLineW();
    const wchar_t* modeArg = wcsstr(cmdLine, L"--mode ");
    if (modeArg) {
        initialVietnamese = (modeArg[7] != L'0');
    }

    // Create and show dialog
    SettingsDialog dialog;
    dialog.SetVietnameseMode(initialVietnamese);
    dialog.SetOnSettingsChanged([]() {
        NEXTKEY_LOG(L"Settings changed");
    });

    // Message loop
    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);

        if (!IsWindow(dialog.get_hwnd())) break;
    }

    // INTENTIONAL: ExitProcess() is required here because Sciter's internal
    // cleanup triggers assertion failures on normal process exit. This is a
    // known Sciter issue. Since this is a subprocess with no shared state
    // to flush, ExitProcess() is safe and avoids the assertion.
    NEXTKEY_LOG(L"Settings subprocess exiting");
    ExitProcess(0);
}

[[noreturn]] void RunExcludedAppsSubprocess() {
    NEXTKEY_LOG(L"Running excluded apps subprocess");

    InitSciterSubprocess();

    HWND parent = FindWindowW(nullptr, L"NexusKey Settings");
    ExcludedAppsDialog dialog(parent);
    dialog.Show();

    NEXTKEY_LOG(L"Excluded apps subprocess exiting");
    ExitProcess(0);
}
