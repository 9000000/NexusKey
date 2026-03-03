// NexusKey - Excluded Apps Dialog Implementation
// SPDX-License-Identifier: GPL-3.0-only

#include "ExcludedAppsDialog.h"
#include "core/config/ConfigManager.h"
#include "core/config/ConfigEvent.h"
#include "sciter-x-dom.hpp"
#include <algorithm>
#include <vector>
#include <TlHelp32.h>

using namespace sciter::dom;

namespace NextKey {

ExcludedAppsDialog::ExcludedAppsDialog(HWND parent)
    : SciterSubDialog({
        L"this://app/excludedapps/excludedapps.html",
        L"NexusKey - Excluded Apps",
        360, 420, parent, true, 36, 40, true
    }) {
    appList_ = ConfigManager::LoadExcludedApps(ConfigManager::GetConfigPath());
    populateList();
}

void ExcludedAppsDialog::onBeforeClose() {
    persistAndSignal();
    if (onChanged_) {
        onChanged_();
    }
}

void ExcludedAppsDialog::persistAndSignal() {
    (void)ConfigManager::SaveExcludedApps(ConfigManager::GetConfigPath(), appList_);
    ConfigEvent event;
    if (event.Initialize()) {
        event.Signal();
    }
}

bool ExcludedAppsDialog::handle_event(HELEMENT he, BEHAVIOR_EVENT_PARAMS& params) {
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
                    auto apps = getRunningApps();
                    sciter::value arr;
                    for (auto& app : apps) {
                        arr.append(sciter::value(app.c_str()));
                    }
                    call_function("setRunningApps", arr);
                } else if (action == L"delete") {
                    if (!appName.empty()) {
                        removeApp(appName);
                    }
                } else if (action == L"close") {
                    PostMessage(get_hwnd(), WM_CLOSE, 0, 0);
                } else if (action == L"get-running-apps") {
                    auto apps = getRunningApps();
                    sciter::value arr;
                    for (auto& app : apps) {
                        arr.append(sciter::value(app.c_str()));
                    }
                    call_function("setRunningApps", arr);
                }

                // Clear the action value to allow re-triggering
                el.set_value(sciter::value(L""));
            }
            return true;
        }
    }

    return sciter::window::handle_event(he, params);
}

void ExcludedAppsDialog::populateList() {
    call_function("clearAppList");
    for (auto& app : appList_) {
        call_function("addAppToList", sciter::value(app.c_str()));
    }
    call_function("forceRefresh");

    // Also load running apps for the dropdown
    auto running = getRunningApps();
    sciter::value arr;
    for (auto& app : running) {
        arr.append(sciter::value(app.c_str()));
    }
    call_function("setRunningApps", arr);
}

void ExcludedAppsDialog::addApp(const std::wstring& name) {
    // Lowercase for consistent matching
    std::wstring lower = name;
    for (auto& c : lower) c = towlower(c);

    // Check for duplicates
    for (auto& existing : appList_) {
        if (existing == lower) return;
    }

    appList_.push_back(lower);
    call_function("addAppToList", sciter::value(lower.c_str()));
    call_function("forceRefresh");
    persistAndSignal();
}

void ExcludedAppsDialog::removeApp(const std::wstring& name) {
    std::wstring lower = name;
    for (auto& c : lower) c = towlower(c);

    auto it = std::find(appList_.begin(), appList_.end(), lower);
    if (it != appList_.end()) {
        appList_.erase(it);
        call_function("removeAppFromList", sciter::value(lower.c_str()));
        persistAndSignal();
    }
}

std::vector<std::wstring> ExcludedAppsDialog::getRunningApps() {
    std::vector<std::wstring> apps;
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) return apps;

    PROCESSENTRY32W pe = {};
    pe.dwSize = sizeof(pe);

    if (Process32FirstW(snapshot, &pe)) {
        do {
            std::wstring name = pe.szExeFile;
            // Lowercase for consistent matching
            for (auto& c : name) c = towlower(c);

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

}  // namespace NextKey
