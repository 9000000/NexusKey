// VKey - Unified Hotkey Rebind Dialog Implementation
// SPDX-License-Identifier: GPL-3.0-only

#include "HotkeysDialog.h"

#include "core/config/ConfigManager.h"
#include "core/WinStrings.h"
#include "helpers/AppHelpers.h"
#include "sciter-x-dom.hpp"

#include <string>
#include <unordered_map>

using namespace sciter::dom;

namespace NextKey {

namespace {

constexpr const wchar_t* kIntentCancel = L"cancel-composition";
constexpr const wchar_t* kIntentSkip   = L"skip-macro";
constexpr const wchar_t* kIntentToggle = L"toggle-enabled";

[[nodiscard]] const wchar_t* IntentLabel(Intent intent) noexcept {
    switch (intent) {
    case Intent::CancelComposition: return kIntentCancel;
    case Intent::SkipMacro:         return kIntentSkip;
    case Intent::ToggleEnabled:     return kIntentToggle;
    }
    return L"";
}

[[nodiscard]] bool ParseIntent(const std::wstring& s, Intent& out) noexcept {
    if (s == kIntentCancel) { out = Intent::CancelComposition; return true; }
    if (s == kIntentSkip)   { out = Intent::SkipMacro;         return true; }
    if (s == kIntentToggle) { out = Intent::ToggleEnabled;     return true; }
    return false;
}

/// Render a Trigger as a localized chip label. Examples:
///   {vk=0x1B}                     → "Esc"
///   {vk=0x11, mods=0}             → "Ctrl (giữ-thả)" (implicit modifier-alone)
///   {vk=0x12, mods=0, doubleTap}  → "2×Alt"
///   {vk=0x56, mods=Ctrl|Shift}    → "Ctrl+Shift+V"
[[nodiscard]] std::wstring FormatTriggerLabel(const Trigger& t) {
    std::wstring s;
    if (t.mods & kModCtrl)  s += L"Ctrl+";
    if (t.mods & kModShift) s += L"Shift+";
    if (t.mods & kModAlt)   s += L"Alt+";
    if (t.mods & kModWin)   s += L"Win+";

    // Modifier-alone (no other mods + vk is itself a modifier):
    if (t.mods == 0 && !t.doubleTap && IsModifierKey(t.vk)) {
        switch (t.vk) {
        case 0x11: return L"Ctrl (giữ-thả)";
        case 0x12: return L"Alt (giữ-thả)";
        case 0x10: return L"Shift (giữ-thả)";
        case 0x5B: case 0x5C: return L"Win (giữ-thả)";
        }
    }

    // Friendly name for the main key. Mirror of VK_NAMES in hotkeys.js.
    static const std::unordered_map<uint32_t, const wchar_t*> kVkNames = {
        {0x08, L"Backspace"}, {0x09, L"Tab"},   {0x0D, L"Enter"},
        {0x10, L"Shift"},     {0x11, L"Ctrl"},  {0x12, L"Alt"},
        {0x13, L"Pause"},     {0x14, L"Caps"},  {0x1B, L"Esc"},
        {0x20, L"Space"},
        {0x21, L"PgUp"},      {0x22, L"PgDn"},  {0x23, L"End"},  {0x24, L"Home"},
        {0x25, L"←"},         {0x26, L"↑"},     {0x27, L"→"},    {0x28, L"↓"},
        {0x2C, L"PrtSc"},     {0x2D, L"Insert"}, {0x2E, L"Del"},
        {0x5B, L"Win"},       {0x5C, L"Win"},   {0x5D, L"Menu"},
        {0xBA, L";"}, {0xBB, L"="}, {0xBC, L","}, {0xBD, L"-"}, {0xBE, L"."}, {0xBF, L"/"},
        {0xC0, L"`"}, {0xDB, L"["}, {0xDC, L"\\"}, {0xDD, L"]"}, {0xDE, L"'"},
    };

    std::wstring keyName;
    if (auto it = kVkNames.find(t.vk); it != kVkNames.end()) {
        keyName = it->second;
    } else if (t.vk >= 0x60 && t.vk <= 0x69) {
        keyName = L"Num" + std::to_wstring(t.vk - 0x60);                  // VK_NUMPAD0..9
    } else if (t.vk >= 0x70 && t.vk <= 0x87) {
        keyName = L"F" + std::to_wstring(t.vk - 0x6F);                    // F1..F24
    } else if ((t.vk >= 'A' && t.vk <= 'Z') || (t.vk >= '0' && t.vk <= '9')) {
        keyName.push_back(static_cast<wchar_t>(t.vk));
    } else {
        keyName = L"VK_" + std::to_wstring(t.vk);
    }

    if (t.doubleTap) {
        s = L"2×" + keyName;  // strip any modifiers from prefix — doubleTap implies mods=0
        return s;
    }

    s += keyName;
    return s;
}

}  // namespace

HotkeysDialog::HotkeysDialog(HWND parent)
    : SciterSubDialog({
        L"this://app/hotkeys/hotkeys.html",
        L"VKey - Phím tắt",
        420, 460, parent, true, 36, 40, true
    }) {
    registry_ = ConfigManager::LoadHotkeyRegistryOrDefault();
    populate();
}

void HotkeysDialog::populate() {
    call_function("clearAll");
    constexpr Intent kAll[] = {
        Intent::CancelComposition, Intent::SkipMacro, Intent::ToggleEnabled,
    };
    for (Intent intent : kAll) {
        for (const Trigger& t : registry_.TriggersFor(intent)) {
            const std::wstring label = FormatTriggerLabel(t);
            // Sciter's call_function caps at a few overloads — bundle the 5
            // payload fields into a single array (intent, label, vk, mods,
            // doubleTap). JS side unpacks via positional indexing.
            sciter::value arr;
            arr.set_item(0, sciter::value(IntentLabel(intent)));
            arr.set_item(1, sciter::value(label.c_str()));
            arr.set_item(2, sciter::value(static_cast<int>(t.vk)));
            arr.set_item(3, sciter::value(static_cast<int>(t.mods)));
            arr.set_item(4, sciter::value(t.doubleTap));
            call_function("addTrigger", arr);
        }
    }
    call_function("forceRefresh");
}

void HotkeysDialog::persistAndSignal() {
    auto path = ConfigManager::GetConfigPath();
    (void)ConfigManager::SaveHotkeyRegistry(path, registry_);
    SignalConfigChange();
}

Trigger HotkeysDialog::readPendingTrigger(Intent& outIntent, bool& outValid) {
    outValid = false;
    Trigger t;
    element root = get_root();

    auto readWString = [&](const char* id) -> std::wstring {
        element el = root.find_first(("#" + std::string(id)).c_str());
        if (!el.is_valid()) return L"";
        sciter::value v = el.get_value();
        return v.is_string() ? v.get<std::wstring>() : L"";
    };
    auto readInt = [&](const char* id, int fallback) -> int {
        element el = root.find_first(("#" + std::string(id)).c_str());
        if (!el.is_valid()) return fallback;
        sciter::value v = el.get_value();
        if (v.is_int())    return v.get<int>();
        if (v.is_string()) {
            try { return std::stoi(v.get<std::wstring>()); } catch (...) { return fallback; }
        }
        return fallback;
    };
    auto readBool = [&](const char* id) -> bool {
        element el = root.find_first(("#" + std::string(id)).c_str());
        if (!el.is_valid()) return false;
        sciter::value v = el.get_value();
        if (v.is_bool())   return v.get<bool>();
        if (v.is_int())    return v.get<int>() != 0;
        if (v.is_string()) {
            auto s = v.get<std::wstring>();
            return s == L"true" || s == L"1";
        }
        return false;
    };

    const std::wstring intentStr = readWString("val-intent");
    if (!ParseIntent(intentStr, outIntent)) return t;

    const int vk   = readInt("val-vk",   0);
    const int mods = readInt("val-mods", 0);
    if (vk <= 0 || vk > 0xFF) return t;  // sanity
    t.vk        = static_cast<uint32_t>(vk);
    t.mods      = static_cast<uint32_t>(mods);
    t.doubleTap = readBool("val-double-tap");
    outValid    = true;
    return t;
}

void HotkeysDialog::handleAction(const std::wstring& action) {
    if (action == L"reset") {
        registry_ = HotkeyRegistry::Defaults();
        populate();
        persistAndSignal();
        return;
    }
    if (action == L"close") {
        PostMessage(get_hwnd(), WM_CLOSE, 0, 0);
        return;
    }

    Intent intent;
    bool valid = false;
    Trigger t = readPendingTrigger(intent, valid);
    if (!valid) return;

    if (action == L"add") {
        // Skip if (intent, exact-trigger) already present.
        for (const Trigger& existing : registry_.TriggersFor(intent)) {
            if (existing == t) return;
        }
        registry_.AddTrigger(intent, t);
        populate();
        persistAndSignal();
        return;
    }
    if (action == L"delete") {
        // Rebuild registry without the matching trigger.
        HotkeyRegistry rebuilt;
        constexpr Intent kAll[] = {
            Intent::CancelComposition, Intent::SkipMacro, Intent::ToggleEnabled,
        };
        for (Intent i : kAll) {
            for (const Trigger& existing : registry_.TriggersFor(i)) {
                if (i == intent && existing == t) continue;  // drop
                rebuilt.AddTrigger(i, existing);
            }
        }
        registry_ = std::move(rebuilt);
        populate();
        persistAndSignal();
        return;
    }
}

bool HotkeysDialog::handle_event(HELEMENT he, BEHAVIOR_EVENT_PARAMS& params) {
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
                handleAction(action);
                el.set_value(sciter::value(L""));  // reset to avoid double-fire
            }
            return true;
        }
    }

    return sciter::window::handle_event(he, params);
}

}  // namespace NextKey
