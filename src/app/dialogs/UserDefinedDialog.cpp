// NexusKey - User Defined Input Dialog Implementation
// SPDX-License-Identifier: GPL-3.0-only

#include "UserDefinedDialog.h"
#include "DialogUtils.h"
#include "core/config/ConfigManager.h"
#include "core/WinStrings.h"
#include "helpers/AppHelpers.h"
#include "sciter-x-dom.hpp"
#include <algorithm>
#include <vector>

using namespace sciter::dom;

namespace NextKey {

UserDefinedDialog::UserDefinedDialog(HWND parent)
    : SciterSubDialog({
        L"this://app/userdefined/userdefined.html",
        L"NexusKey - User Defined Input",
        460, 550, parent, true, 36, 40, true
    }) {
    TypingConfig config = ConfigManager::LoadOrDefault();
    keyMap_ = config.customKeyMap;
    populateList();
}

void UserDefinedDialog::persistAndSignal() {
    auto config = ConfigManager::LoadOrDefault();
    config.customKeyMap = keyMap_;
    (void)ConfigManager::SaveToFile(ConfigManager::GetConfigPath(), config);
    SignalConfigChange();
}

void UserDefinedDialog::populateList() {
    sciter::dom::element root = get_root();
    root.call_method("clearKeyMap");

    for (size_t i = 0; i < 128; ++i) {
        TypingAction action = keyMap_[i];
        if (action != TypingAction::None) {
            std::wstring keyStr;
            keyStr += static_cast<wchar_t>(i);

            std::string actionName = std::string(TypingActionToString(action));
            
            // Get label from UI dropdown to show in list
            std::wstring label = DialogUtils::GetLocalizedString((L"ud.act." + Utf8ToWide(actionName)).c_str());
            if (label.empty()) {
                label = Utf8ToWide(actionName);
            }

            root.call_method("addKeyToMap", sciter::value(keyStr), sciter::value(actionName), sciter::value(label));
        }
    }
}

void UserDefinedDialog::loadTemplate(bool telex) {
    keyMap_.fill(TypingAction::None);

    // Common keys for both (a-z, 0-9, and punctuation used in Telex/VNI)
    std::string chars = "abcdefghijklmnopqrstuvwxyz0123456789[]";
    for (char c : chars) {
        TypingAction action = ClassifyKey(static_cast<wchar_t>(c), telex, !telex);
        if (action != TypingAction::None) {
            keyMap_[static_cast<uint8_t>(c)] = action;
        }
    }

    populateList();
    persistAndSignal();
}

bool UserDefinedDialog::handle_event(HELEMENT he, BEHAVIOR_EVENT_PARAMS& params) {
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
                
                if (action == L"add") {
                    sciter::dom::element keyInput = root.find_first("#val-key");
                    sciter::dom::element actionInput = root.find_first("#val-key-action");

                    if (keyInput.is_valid() && actionInput.is_valid()) {
                        std::wstring keyStr = keyInput.get_value().get<std::wstring>();
                        std::string actionName = actionInput.get_value().get<std::string>();

                        if (!keyStr.empty()) {
                            wchar_t k = towlower(keyStr[0]);
                            if (k < 128) {
                                keyMap_[static_cast<uint8_t>(k)] = StringToTypingAction(actionName);
                                populateList();
                                persistAndSignal();
                            }
                        }
                    }
                } else if (action == L"delete") {
                    sciter::dom::element keyInput = root.find_first("#val-key");
                    if (keyInput.is_valid()) {
                        std::wstring keyStr = keyInput.get_value().get<std::wstring>();
                        if (!keyStr.empty()) {
                            wchar_t k = towlower(keyStr[0]);
                            if (k < 128) {
                                keyMap_[static_cast<uint8_t>(k)] = TypingAction::None;
                                populateList();
                                persistAndSignal();
                            }
                        }
                    }
                } else if (action == L"load_telex") {
                    loadTemplate(true);
                } else if (action == L"load_vni") {
                    loadTemplate(false);
                } else if (action == L"import") {
                    importKeyMap();
                } else if (action == L"export") {
                    exportKeyMap();
                }

                // Reset val-action to allow re-triggering same action
                el.set_value(sciter::value(L""));
                return true;
            }
        }
    }

    return false;
}

void UserDefinedDialog::importKeyMap() {
    // Phase 2: Placeholder for .keymap import
    // Similar to MacroTableDialog::importMacros
    std::wstring path = ShowOpenFileDialogW(get_hwnd(), L"Keymap files (*.keymap)\0*.keymap\0All files (*.*)\0*.*\0", L"keymap");
    if (!path.empty()) {
        TypingConfig dummy;
        if (ConfigManager::ImportCustomKeyMap(path, dummy)) {
            keyMap_ = dummy.customKeyMap;
            populateList();
            persistAndSignal();
        }
    }
}

void UserDefinedDialog::exportKeyMap() {
    std::wstring path = ShowSaveFileDialogW(get_hwnd(), L"Keymap files (*.keymap)\0*.keymap\0", L"keymap", L"custom.keymap");
    if (!path.empty()) {
        TypingConfig dummy;
        dummy.customKeyMap = keyMap_;
        ConfigManager::ExportCustomKeyMap(path, dummy);
    }
}

}  // namespace NextKey
