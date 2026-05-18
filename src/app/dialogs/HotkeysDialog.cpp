// VKey - Unified Hotkey Rebind Dialog Implementation
// SPDX-License-Identifier: GPL-3.0-only

#include "HotkeysDialog.h"

#include "core/config/ConfigManager.h"
#include "core/WinStrings.h"
#include "sciter-x-dom.hpp"

#include <string>

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
    if (t.mods & MOD_CTRL)  s += L"Ctrl+";
    if (t.mods & MOD_SHIFT) s += L"Shift+";
    if (t.mods & MOD_ALT)   s += L"Alt+";
    if (t.mods & MOD_WIN)   s += L"Win+";

    // Modifier-alone (no other mods + vk is itself a modifier):
    if (t.mods == 0 && !t.doubleTap && IsModifierKey(t.vk)) {
        switch (t.vk) {
        case 0x11: return L"Ctrl (giữ-thả)";
        case 0x12: return L"Alt (giữ-thả)";
        case 0x10: return L"Shift (giữ-thả)";
        case 0x5B: case 0x5C: return L"Win (giữ-thả)";
        }
    }

    // Friendly name for the main key.
    std::wstring keyName;
    switch (t.vk) {
    case 0x1B: keyName = L"Esc"; break;
    case 0x09: keyName = L"Tab"; break;
    case 0x20: keyName = L"Space"; break;
    case 0x0D: keyName = L"Enter"; break;
    case 0x08: keyName = L"Backspace"; break;
    case 0x11: keyName = L"Ctrl"; break;
    case 0x10: keyName = L"Shift"; break;
    case 0x12: keyName = L"Alt"; break;
    case 0x5B: case 0x5C: keyName = L"Win"; break;
    default:
        if (t.vk >= 0x70 && t.vk <= 0x7B) {
            keyName = L"F" + std::to_wstring(t.vk - 0x6F);  // F1..F12
        } else if ((t.vk >= 'A' && t.vk <= 'Z') || (t.vk >= '0' && t.vk <= '9')) {
            keyName.push_back(static_cast<wchar_t>(t.vk));
        } else {
            keyName = L"VK_" + std::to_wstring(t.vk);
        }
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
            call_function("addTrigger",
                          sciter::value(IntentLabel(intent)),
                          sciter::value(label.c_str()),
                          sciter::value(static_cast<int>(t.vk)),
                          sciter::value(static_cast<int>(t.mods)),
                          sciter::value(t.doubleTap));
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
