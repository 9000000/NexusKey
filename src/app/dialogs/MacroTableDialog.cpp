// NexusKey - Macro Table Dialog Implementation
// SPDX-License-Identifier: GPL-3.0-only

#include "MacroTableDialog.h"
#include "DialogUtils.h"
#include "core/config/ConfigManager.h"
#include "helpers/AppHelpers.h"
#include "sciter-x-dom.hpp"
#include <algorithm>
#include <vector>
#include <fstream>

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
                } else if (action == L"import") {
                    importMacros();
                } else if (action == L"export") {
                    exportMacros();
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

void MacroTableDialog::importMacros() {
    std::wstring path = ShowOpenFileDialogW(
        get_hwnd(),
        L"Text file (*.txt)\0*.txt\0All (*.*)\0*.*\0",
        L"txt"
    );
    if (path.empty()) return;

    int msgboxID = MessageBoxW(
        get_hwnd(),
        L"B\u1EA1n c\u00F3 mu\u1ED1n gi\u1EEF l\u1EA1i d\u1EEF li\u1EC7u hi\u1EC7n t\u1EA1i kh\u00F4ng?",
        L"D\u1EEF li\u1EC7u g\u00F5 t\u1EAFt",
        MB_ICONEXCLAMATION | MB_YESNO
    );

    bool append = (msgboxID == IDYES);
    if (!append) {
        macros_.clear();
    }

    // Read UTF-8 file
    std::ifstream infile(path);
    if (!infile.is_open()) return;

    std::string line;
    bool firstLine = true;
    while (std::getline(infile, line)) {
        // Skip header/comment lines
        if (firstLine) { firstLine = false; if (!line.empty() && line[0] == ';') continue; }
        if (line.empty() || line[0] == ';') continue;

        // Split on first ':'
        auto pos = line.find(':');
        if (pos == std::string::npos || pos == 0) continue;

        std::string key = line.substr(0, pos);
        std::string value = line.substr(pos + 1);
        if (value.empty()) continue;

        // Convert UTF-8 to wstring
        int keyLen = MultiByteToWideChar(CP_UTF8, 0, key.c_str(), -1, nullptr, 0);
        int valLen = MultiByteToWideChar(CP_UTF8, 0, value.c_str(), -1, nullptr, 0);
        if (keyLen <= 0 || valLen <= 0) continue;

        std::wstring wKey(keyLen - 1, L'\0');
        std::wstring wVal(valLen - 1, L'\0');
        MultiByteToWideChar(CP_UTF8, 0, key.c_str(), -1, wKey.data(), keyLen);
        MultiByteToWideChar(CP_UTF8, 0, value.c_str(), -1, wVal.data(), valLen);

        macros_[wKey] = wVal;
    }

    populateList();
    persistAndSignal();
}

void MacroTableDialog::exportMacros() {
    std::wstring path = ShowSaveFileDialogW(
        get_hwnd(),
        L"Text file (*.txt)\0*.txt\0",
        L"txt",
        L"NexusKeyMacro"
    );
    if (path.empty()) return;

    // Sort entries for consistent output
    std::vector<std::pair<std::wstring, std::wstring>> sorted(macros_.begin(), macros_.end());
    std::sort(sorted.begin(), sorted.end());

    std::ofstream outfile(path);
    if (!outfile.is_open()) return;

    // Write OpenKey-compatible header
    outfile << ";Compatible OpenKey Macro Data file*** version=1 ***\n";

    for (auto& [key, value] : sorted) {
        // Convert wstring to UTF-8
        int keyLen = WideCharToMultiByte(CP_UTF8, 0, key.c_str(), -1, nullptr, 0, nullptr, nullptr);
        int valLen = WideCharToMultiByte(CP_UTF8, 0, value.c_str(), -1, nullptr, 0, nullptr, nullptr);
        if (keyLen <= 0 || valLen <= 0) continue;

        std::string u8Key(keyLen - 1, '\0');
        std::string u8Val(valLen - 1, '\0');
        WideCharToMultiByte(CP_UTF8, 0, key.c_str(), -1, u8Key.data(), keyLen, nullptr, nullptr);
        WideCharToMultiByte(CP_UTF8, 0, value.c_str(), -1, u8Val.data(), valLen, nullptr, nullptr);

        outfile << u8Key << ":" << u8Val << "\n";
    }
}

}  // namespace NextKey
