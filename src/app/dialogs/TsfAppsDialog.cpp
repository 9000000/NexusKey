// NexusKey - TSF Apps Dialog Implementation
// SPDX-License-Identifier: GPL-3.0-only

#include "TsfAppsDialog.h"
#include "core/config/ConfigManager.h"
#include "helpers/AppHelpers.h"
#include "sciter-x-dom.hpp"
#include <algorithm>
#include <vector>
#include <TlHelp32.h>
#include <Psapi.h>

#pragma comment(lib, "psapi.lib")

using namespace sciter::dom;

namespace NextKey {

TsfAppsDialog::TsfAppsDialog(HWND parent)
    : SciterSubDialog({
        L"this://app/tsfapps/tsfapps.html",
        L"NexusKey - TSF Apps",
        360, 420, parent, true, 36, 40, true
    }) {
    appList_ = ConfigManager::LoadTsfApps(ConfigManager::GetConfigPath());
    populateList();
}

void TsfAppsDialog::onBeforeClose() {
    persistAndSignal();
    if (onChanged_) {
        onChanged_();
    }
}

void TsfAppsDialog::persistAndSignal() {
    (void)ConfigManager::SaveTsfApps(ConfigManager::GetConfigPath(), appList_);
    SignalConfigChange();
}

bool TsfAppsDialog::handle_event(HELEMENT he, BEHAVIOR_EVENT_PARAMS& params) {
    // Handle BUTTON_CLICK for close button
    if (params.cmd == BUTTON_CLICK) {
        sciter::dom::element el(params.heTarget);
        std::wstring id = el.get_attribute("id");

        if (id == L"btn-close") {
            PostMessage(get_hwnd(), WM_CLOSE, 0, 0);
            return true;
        }
    }

    // Handle VALUE_CHANGED for #val-action (triggered by JS triggerAction)
    if (params.cmd == VALUE_CHANGED) {
        sciter::dom::element el(params.heTarget);
        std::wstring id = el.get_attribute("id");

        if (id == L"val-action") {
            sciter::value val = el.get_value();
            std::wstring action = val.is_string() ? val.get<std::wstring>() : L"";
            if (!action.empty()) {
                // Read app name from hidden input
                sciter::dom::element root = get_root();
                sciter::dom::element nameInput = root.find_first("#val-app-name");
                std::wstring appName;
                if (nameInput.is_valid()) {
                    sciter::value nv = nameInput.get_value();
                    appName = nv.is_string() ? nv.get<std::wstring>() : L"";
                }

                if (action == L"add-manual") {
                    if (!appName.empty()) {
                        addApp(appName);
                    }
                } else if (action == L"add-current") {
                    startWindowPicking();
                } else if (action == L"delete") {
                    if (!appName.empty()) {
                        removeApp(appName);
                    }
                } else if (action == L"close") {
                    PostMessage(get_hwnd(), WM_CLOSE, 0, 0);
                }

                // Clear the action value to allow re-triggering
                el.set_value(sciter::value(L""));
            }
            return true;
        }
    }

    return sciter::window::handle_event(he, params);
}

void TsfAppsDialog::populateList() {
    call_function("clearAppList");
    for (auto& app : appList_) {
        call_function("addAppToList", sciter::value(app.c_str()));
    }
    call_function("forceRefresh");
}

void TsfAppsDialog::addApp(const std::wstring& name) {
    std::wstring lower = ToLowerAscii(name);

    // Check for duplicates
    for (auto& existing : appList_) {
        if (existing == lower) return;
    }

    appList_.push_back(lower);
    call_function("addAppToList", sciter::value(lower.c_str()));
    call_function("forceRefresh");
    persistAndSignal();
}

void TsfAppsDialog::removeApp(const std::wstring& name) {
    std::wstring lower = ToLowerAscii(name);

    auto it = std::find(appList_.begin(), appList_.end(), lower);
    if (it != appList_.end()) {
        appList_.erase(it);
        call_function("removeAppFromList", sciter::value(lower.c_str()));
        persistAndSignal();
    }
}

std::vector<std::wstring> TsfAppsDialog::getRunningApps() {
    std::vector<std::wstring> apps;
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) return apps;

    PROCESSENTRY32W pe = {};
    pe.dwSize = sizeof(pe);

    if (Process32FirstW(snapshot, &pe)) {
        do {
            std::wstring name = ToLowerAscii(pe.szExeFile);

            // Skip system processes
            if (name == L"system" || name == L"system idle process" ||
                name == L"svchost.exe" || name == L"csrss.exe" ||
                name == L"smss.exe" || name == L"wininit.exe" ||
                name == L"services.exe" || name == L"lsass.exe" ||
                name == L"conhost.exe" || name == L"dwm.exe" ||
                name == L"nexuskey.exe" || name == L"[system process]") {
                continue;
            }

            // Check for duplicates
            bool found = false;
            for (auto& existing : apps) {
                if (existing == name) { found = true; break; }
            }
            if (!found) {
                apps.push_back(name);
            }
        } while (Process32NextW(snapshot, &pe));
    }

    CloseHandle(snapshot);
    std::sort(apps.begin(), apps.end());
    return apps;
}

void TsfAppsDialog::startWindowPicking() {
    isPickingWindow_ = true;
    SetCapture(get_hwnd());

    // Save original arrow cursor before replacing
    HCURSOR hOriginalArrow = LoadCursor(nullptr, IDC_ARROW);
    savedArrowCursor_ = CopyCursor(hOriginalArrow);

    // Replace system arrow cursor with crosshair globally
    HCURSOR hCross = LoadCursor(nullptr, IDC_CROSS);
    SetSystemCursor(CopyCursor(hCross), OCR_NORMAL);
}

void TsfAppsDialog::stopWindowPicking() {
    isPickingWindow_ = false;
    ReleaseCapture();

    // Restore original arrow cursor
    if (savedArrowCursor_) {
        SetSystemCursor(savedArrowCursor_, OCR_NORMAL);
        savedArrowCursor_ = nullptr;  // SetSystemCursor takes ownership
    }

    SetForegroundWindow(get_hwnd());
}

std::wstring TsfAppsDialog::getExeNameFromWindow(HWND hwnd) {
    if (!hwnd) return L"";

    DWORD processId = 0;
    GetWindowThreadProcessId(hwnd, &processId);
    if (processId == 0) return L"";

    HANDLE hProcess = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ,
                                  FALSE, processId);
    if (!hProcess) return L"";

    WCHAR exePath[MAX_PATH] = {};
    GetProcessImageFileNameW(hProcess, exePath, MAX_PATH);
    CloseHandle(hProcess);

    if (wcslen(exePath) == 0) return L"";

    // Extract filename from device path (e.g. \Device\HarddiskVolume3\...\app.exe)
    const WCHAR* filename = wcsrchr(exePath, L'\\');
    if (filename) filename++;
    else filename = exePath;

    return ToLowerAscii(filename);
}

LRESULT TsfAppsDialog::onCustomMessage(HWND hwnd, UINT msg,
                                         WPARAM wParam, LPARAM lParam) {
    // Window Picker: show crosshair cursor continuously
    if (msg == WM_SETCURSOR && isPickingWindow_) {
        SetCursor(LoadCursor(nullptr, IDC_CROSS));
        return TRUE;
    }

    // Window Picker: click to capture target window
    if (msg == WM_LBUTTONUP && isPickingWindow_) {
        POINT pt;
        GetCursorPos(&pt);
        HWND targetWnd = WindowFromPoint(pt);

        if (targetWnd) {
            targetWnd = GetAncestor(targetWnd, GA_ROOT);
        }

        if (targetWnd && targetWnd != hwnd) {
            std::wstring exeName = getExeNameFromWindow(targetWnd);
            if (!exeName.empty()) {
                if (exeName == L"nexuskey.exe") {
                    stopWindowPicking();
                    MessageBoxW(get_hwnd(),
                        L"Không thể thêm NexusKey vào danh sách.",
                        L"NexusKey", MB_OK | MB_ICONWARNING);
                } else {
                    stopWindowPicking();
                    addApp(exeName);
                }
                return 0;
            }
        }
        stopWindowPicking();
        return 0;
    }

    // Window Picker: ESC to cancel
    if (msg == WM_KEYDOWN && wParam == VK_ESCAPE && isPickingWindow_) {
        stopWindowPicking();
        return 0;
    }

    return -1;  // Unhandled
}

}  // namespace NextKey
