// NexusKey - TSF Apps Dialog Implementation
// SPDX-License-Identifier: GPL-3.0-only

#include "TsfAppsDialog.h"
#include "core/config/ConfigManager.h"
#include "helpers/AppHelpers.h"
#include "sciter-x-dom.hpp"
#include <algorithm>
#include <vector>

using namespace sciter::dom;

namespace NextKey {

TsfAppsDialog::TsfAppsDialog(HWND parent)
    : WindowPickerDialog({
        L"this://app/tsfapps/tsfapps.html",
        L"NexusKey - TSF Apps",
        360, 420, parent, true, 36, 40, true
    }) {
    appList_ = ConfigManager::LoadTsfApps(ConfigManager::GetConfigPath());
    populateList();
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
                } else if (action == L"get-running-apps") {
                    auto apps = getRunningApps();
                    sciter::value arr;
                    for (size_t i = 0; i < apps.size(); ++i) {
                        arr.set_item(static_cast<int>(i), sciter::value(apps[i].c_str()));
                    }
                    call_function("setRunningApps", arr);
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

void TsfAppsDialog::onWindowPicked(const std::wstring& exeName) {
    if (exeName == L"nexuskey.exe") {
        MessageBoxW(get_hwnd(), L"Không thể thêm NexusKey vào danh sách.",
                    L"NexusKey", MB_OK | MB_ICONWARNING);
    } else {
        addApp(exeName);
    }
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
    if (std::find(appList_.begin(), appList_.end(), lower) != appList_.end()) return;

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

}  // namespace NextKey
