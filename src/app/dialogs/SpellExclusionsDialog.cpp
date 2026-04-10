// NexusKey - Spell Check Exclusions Dialog Implementation
// SPDX-License-Identifier: GPL-3.0-only

#include "SpellExclusionsDialog.h"
#include "helpers/AppHelpers.h"
#include "core/config/ConfigManager.h"
#include "sciter-x-dom.hpp"
#include <algorithm>

using namespace sciter::dom;

namespace NextKey {

SpellExclusionsDialog::SpellExclusionsDialog(HWND parent)
    : SciterSubDialog({
        L"this://app/spellexclusions/spellexclusions.html",
        L"NexusKey - Spell Exclusions",
        340, 380, parent, true, 36, 40, true
    }) {
    auto config = ConfigManager::LoadOrDefault();
    entries_ = std::move(config.spellExclusions);
    populateList();
}

void SpellExclusionsDialog::persistAndSignal() {
    auto path = ConfigManager::GetConfigPath();
    auto config = ConfigManager::LoadFromFile(path).value_or(TypingConfig{});
    config.spellExclusions = entries_;
    (void)ConfigManager::SaveToFile(path, config);
    SignalConfigChange();
}

bool SpellExclusionsDialog::handle_event(HELEMENT he, BEHAVIOR_EVENT_PARAMS& params) {
    if (params.cmd == BUTTON_CLICK) {
        element el(params.heTarget);
        std::wstring id = el.get_attribute("id");

        if (id == L"btn-close") {
            PostMessage(get_hwnd(), WM_CLOSE, 0, 0);
            return true;
        }
    }

    if (params.cmd == VALUE_CHANGED) {
        element el(params.heTarget);
        std::wstring id = el.get_attribute("id");

        if (id == L"val-action") {
            sciter::value val = el.get_value();
            std::wstring action = val.is_string() ? val.get<std::wstring>() : L"";
            if (!action.empty()) {
                element root = get_root();
                element nameInput = root.find_first("#val-entry-name");
                std::wstring entryName;
                if (nameInput.is_valid()) {
                    sciter::value nv = nameInput.get_value();
                    entryName = nv.is_string() ? nv.get<std::wstring>() : L"";
                }

                if (action == L"add") {
                    if (!entryName.empty()) {
                        addEntry(entryName);
                    }
                } else if (action == L"delete") {
                    if (!entryName.empty()) {
                        removeEntry(entryName);
                    }
                } else if (action == L"close") {
                    PostMessage(get_hwnd(), WM_CLOSE, 0, 0);
                }

                el.set_value(sciter::value(L""));
            }
            return true;
        }
    }

    return sciter::window::handle_event(he, params);
}

void SpellExclusionsDialog::populateList() {
    call_function("clearList");
    for (auto& entry : entries_) {
        call_function("addToList", sciter::value(entry.c_str()));
    }
    call_function("forceRefresh");
}

void SpellExclusionsDialog::addEntry(const std::wstring& text) {
    // Trim
    size_t s = 0, e = text.size();
    while (s < e && text[s] == L' ') ++s;
    while (e > s && text[e - 1] == L' ') --e;
    if (e - s < 2) return;

    std::wstring entry = text.substr(s, e - s);

    // Dedup (case-insensitive)
    for (auto& existing : entries_) {
        if (_wcsicmp(existing.c_str(), entry.c_str()) == 0) return;
    }

    entries_.push_back(entry);
    std::sort(entries_.begin(), entries_.end());
    populateList();
    persistAndSignal();
}

void SpellExclusionsDialog::removeEntry(const std::wstring& text) {
    auto it = std::find(entries_.begin(), entries_.end(), text);
    if (it != entries_.end()) {
        entries_.erase(it);
        call_function("removeFromList", sciter::value(text.c_str()));
        persistAndSignal();
    }
}

}  // namespace NextKey
