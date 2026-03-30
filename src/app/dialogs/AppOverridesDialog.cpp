// NexusKey - App Overrides Dialog Implementation
// SPDX-License-Identifier: GPL-3.0-only

#include "AppOverridesDialog.h"
#include "helpers/AppHelpers.h"
#include "sciter-x-dom.hpp"
#include <algorithm>
#include <TlHelp32.h>
#include <Psapi.h>

#pragma comment(lib, "psapi.lib")

using namespace sciter::dom;

namespace NextKey {

AppOverridesDialog::AppOverridesDialog(HWND parent)
    : SciterSubDialog({
        L"this://app/appoverrides/appoverrides.html",
        L"NexusKey - App Overrides",
        400, 460, parent, true, 36, 40, true
    }) {
    entries_ = ConfigManager::LoadAppOverrides(ConfigManager::GetConfigPath());
    populateList();
}

void AppOverridesDialog::onBeforeClose() {
    persistAndSignal();
}

void AppOverridesDialog::persistAndSignal() {
    (void)ConfigManager::SaveAppOverrides(ConfigManager::GetConfigPath(), entries_);
    SignalConfigChange();
}

bool AppOverridesDialog::handle_event(HELEMENT he, BEHAVIOR_EVENT_PARAMS& params) {
    if (params.cmd == BUTTON_CLICK) {
        sciter::dom::element el(params.heTarget);
        std::wstring id = el.get_attribute("id");
        if (id == L"btn-close") {
            PostMessage(get_hwnd(), WM_CLOSE, 0, 0);
            return true;
        }
    }

    if (params.cmd == VALUE_CHANGED) {
        sciter::dom::element el(params.heTarget);
        std::wstring id = el.get_attribute("id");

        if (id == L"val-action") {
            sciter::value val = el.get_value();
            std::wstring action = val.is_string() ? val.get<std::wstring>() : L"";

            if (!action.empty()) {
                sciter::dom::element root = get_root();
                auto readInput = [&](const wchar_t* inputId) -> std::wstring {
                    sciter::dom::element inp = root.find_first(inputId);
                    if (!inp.is_valid()) return L"";
                    sciter::value v = inp.get_value();
                    return v.is_string() ? v.get<std::wstring>() : L"";
                };
                auto readInt = [&](const wchar_t* inputId, int def) -> int {
                    auto s = readInput(inputId);
                    if (s.empty()) return def;
                    try { return std::stoi(s); } catch (...) { return def; }
                };

                std::wstring appName = readInput(L"#val-app-name");

                if (action == L"add-app") {
                    if (!appName.empty()) {
                        int8_t enc = static_cast<int8_t>(readInt(L"#val-encoding-override", -1));
                        int8_t beh = static_cast<int8_t>(readInt(L"#val-behavior-type", 0));
                        int8_t clp = static_cast<int8_t>(readInt(L"#val-clipboard-method", -1));
                        addEntry(appName, enc, beh, clp);
                    }
                } else if (action == L"delete-app") {
                    if (!appName.empty()) {
                        removeEntry(appName);
                    }
                } else if (action == L"get-running-apps") {
                    auto apps = getRunningApps();
                    sciter::value arr;
                    for (size_t i = 0; i < apps.size(); ++i) {
                        arr.set_item(static_cast<int>(i), sciter::value(apps[i].c_str()));
                    }
                    call_function("setRunningApps", arr);
                } else if (action == L"pick-window") {
                    startWindowPicking();
                }

                el.set_value(sciter::value(L""));
            }
            return true;
        }
    }

    return sciter::window::handle_event(he, params);
}

void AppOverridesDialog::populateList() {
    call_function("clearAppList");
    for (auto& [name, entry] : entries_) {
        call_function("addAppToList",
            sciter::value(name.c_str()),
            sciter::value(static_cast<int>(entry.behaviorType)),
            sciter::value(static_cast<int>(entry.clipboardMethod)),
            sciter::value(static_cast<int>(entry.encodingOverride)));
    }
    call_function("forceRefresh");
}

void AppOverridesDialog::addEntry(const std::wstring& name, int8_t encoding, int8_t behavior, int8_t clipboard) {
    std::wstring lower = ToLowerAscii(name);
    AppOverrideEntry entry;
    entry.encodingOverride = encoding;
    entry.behaviorType = behavior;
    entry.clipboardMethod = clipboard;

    bool isUpdate = (entries_.find(lower) != entries_.end());
    entries_[lower] = entry;

    if (isUpdate) {
        call_function("removeAppFromList", sciter::value(lower.c_str()));
    }
    call_function("addAppToList",
        sciter::value(lower.c_str()),
        sciter::value(static_cast<int>(behavior)),
        sciter::value(static_cast<int>(clipboard)),
        sciter::value(static_cast<int>(encoding)));
    call_function("clearInput");
    call_function("forceRefresh", sciter::value(true));
    persistAndSignal();
}

void AppOverridesDialog::removeEntry(const std::wstring& name) {
    std::wstring lower = ToLowerAscii(name);
    auto it = entries_.find(lower);
    if (it != entries_.end()) {
        entries_.erase(it);
        call_function("removeAppFromList", sciter::value(lower.c_str()));
        persistAndSignal();
    }
}

std::vector<std::wstring> AppOverridesDialog::getRunningApps() {
    std::vector<std::wstring> apps;
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) return apps;

    PROCESSENTRY32W pe = {};
    pe.dwSize = sizeof(pe);

    if (Process32FirstW(snapshot, &pe)) {
        do {
            std::wstring name = ToLowerAscii(pe.szExeFile);
            if (name == L"system" || name == L"system idle process" ||
                name == L"svchost.exe" || name == L"csrss.exe" ||
                name == L"smss.exe" || name == L"wininit.exe" ||
                name == L"services.exe" || name == L"lsass.exe" ||
                name == L"conhost.exe" || name == L"dwm.exe" ||
                name == L"nexuskey.exe" || name == L"[system process]") {
                continue;
            }
            bool found = false;
            for (auto& existing : apps) {
                if (existing == name) { found = true; break; }
            }
            if (!found) apps.push_back(name);
        } while (Process32NextW(snapshot, &pe));
    }

    CloseHandle(snapshot);
    std::sort(apps.begin(), apps.end());
    return apps;
}

void AppOverridesDialog::startWindowPicking() {
    isPickingWindow_ = true;
    SetCapture(get_hwnd());
    HCURSOR hOriginalArrow = LoadCursor(nullptr, IDC_ARROW);
    savedArrowCursor_ = CopyCursor(hOriginalArrow);
    HCURSOR hCross = LoadCursor(nullptr, IDC_CROSS);
    SetSystemCursor(CopyCursor(hCross), OCR_NORMAL);
}

void AppOverridesDialog::stopWindowPicking() {
    isPickingWindow_ = false;
    ReleaseCapture();
    if (savedArrowCursor_) {
        SetSystemCursor(savedArrowCursor_, OCR_NORMAL);
        savedArrowCursor_ = nullptr;
    }
    SetForegroundWindow(get_hwnd());
}

std::wstring AppOverridesDialog::getExeNameFromWindow(HWND hwnd) {
    if (!hwnd) return L"";
    DWORD processId = 0;
    GetWindowThreadProcessId(hwnd, &processId);
    if (processId == 0) return L"";
    HANDLE hProcess = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, processId);
    if (!hProcess) return L"";
    WCHAR exePath[MAX_PATH] = {};
    GetProcessImageFileNameW(hProcess, exePath, MAX_PATH);
    CloseHandle(hProcess);
    if (wcslen(exePath) == 0) return L"";
    const WCHAR* filename = wcsrchr(exePath, L'\\');
    if (filename) filename++;
    else filename = exePath;
    return ToLowerAscii(filename);
}

LRESULT AppOverridesDialog::onCustomMessage(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == WM_SETCURSOR && isPickingWindow_) {
        SetCursor(LoadCursor(nullptr, IDC_CROSS));
        return TRUE;
    }
    if (msg == WM_LBUTTONUP && isPickingWindow_) {
        POINT pt;
        GetCursorPos(&pt);
        HWND targetWnd = WindowFromPoint(pt);
        if (targetWnd) targetWnd = GetAncestor(targetWnd, GA_ROOT);
        if (targetWnd && targetWnd != hwnd) {
            std::wstring exeName = getExeNameFromWindow(targetWnd);
            if (!exeName.empty()) {
                stopWindowPicking();
                // Set app name in UI input field
                sciter::dom::element root = get_root();
                sciter::dom::element input = root.find_first("#app-name");
                if (input.is_valid()) {
                    input.set_value(sciter::value(exeName.c_str()));
                }
                return 0;
            }
        }
        stopWindowPicking();
        return 0;
    }
    if (msg == WM_KEYDOWN && wParam == VK_ESCAPE && isPickingWindow_) {
        stopWindowPicking();
        return 0;
    }
    return -1;
}

}  // namespace NextKey
