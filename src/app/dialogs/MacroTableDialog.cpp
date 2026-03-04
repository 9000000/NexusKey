// NexusKey - Macro Table Dialog Implementation
// SPDX-License-Identifier: GPL-3.0-only

#include "MacroTableDialog.h"
#include "core/config/ConfigManager.h"
#include "helpers/AppHelpers.h"
#include "sciter-x-dom.hpp"
#include <algorithm>
#include <vector>

using namespace sciter::dom;

namespace NextKey {

MacroTableDialog::MacroTableDialog(HWND parent)
    : SciterSubDialog({
        L"this://app/macro/macro.html",
        L"NexusKey - Macro Table",
        420, 480, parent, true, 36, 40, true
    }) {
    macros_ = ConfigManager::LoadMacros(ConfigManager::GetConfigPath());
    populateList();
}

void MacroTableDialog::onBeforeClose() {
    persistAndSignal();
}

void MacroTableDialog::persistAndSignal() {
    (void)ConfigManager::SaveMacros(ConfigManager::GetConfigPath(), macros_);
    SignalConfigChange();
}

bool MacroTableDialog::handle_event(HELEMENT he, BEHAVIOR_EVENT_PARAMS& params) {
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
                // Read macro name and content from hidden inputs
                sciter::dom::element root = get_root();
                sciter::dom::element nameInput = root.find_first("#val-macro-name");
                sciter::dom::element contentInput = root.find_first("#val-macro-content");

                std::wstring macroName;
                std::wstring macroContent;
                if (nameInput.is_valid()) {
                    sciter::value nv = nameInput.get_value();
                    macroName = nv.is_string() ? nv.get<std::wstring>() : L"";
                }
                if (contentInput.is_valid()) {
                    sciter::value cv = contentInput.get_value();
                    macroContent = cv.is_string() ? cv.get<std::wstring>() : L"";
                }

                if (action == L"add") {
                    if (!macroName.empty() && !macroContent.empty()) {
                        addMacro(macroName, macroContent);
                    }
                } else if (action == L"delete") {
                    if (!macroName.empty()) {
                        removeMacro(macroName);
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

void MacroTableDialog::populateList() {
    call_function("clearMacroList");

    // Sort entries by key for consistent display
    std::vector<std::pair<std::wstring, std::wstring>> sorted(macros_.begin(), macros_.end());
    std::sort(sorted.begin(), sorted.end());

    for (auto& [name, content] : sorted) {
        call_function("addMacroToList", sciter::value(name.c_str()), sciter::value(content.c_str()));
    }
}

void MacroTableDialog::addMacro(const std::wstring& name, const std::wstring& content) {
    macros_[name] = content;
    populateList();
    persistAndSignal();
}

void MacroTableDialog::removeMacro(const std::wstring& name) {
    macros_.erase(name);
    populateList();
    persistAndSignal();
}

}  // namespace NextKey
