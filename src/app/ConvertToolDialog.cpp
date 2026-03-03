// NexusKey - Convert Tool Dialog Implementation
// SPDX-License-Identifier: GPL-3.0-only

#include "ConvertToolDialog.h"
#include "core/engine/CodeTableConverter.h"
#include "core/config/ConfigManager.h"
#include "core/config/ConfigEvent.h"
#include "sciter-x-dom.hpp"
#include <windows.h>
#include <vector>

using namespace sciter::dom;

namespace NextKey {

ConvertToolDialog::ConvertToolDialog(HWND parent)
    : SciterSubDialog({
        L"this://app/convert-tool/convert-tool.html",
        L"NexusKey - Convert Tool",
        420, 560, parent, true, 36, 40, true
    }) {
    // Load saved config (UI will be populated in DOCUMENT_COMPLETE)
    config_ = ConfigManager::LoadConvertConfigOrDefault();
}

bool ConvertToolDialog::handle_event(HELEMENT he, BEHAVIOR_EVENT_PARAMS& params) {
    // Document fully loaded — populate UI from saved config
    if (params.cmd == DOCUMENT_COMPLETE) {
        // Set toggle states
        setToggleUI("#toggle-all-caps", "#val-toggle-all-caps", config_.allCaps);
        setToggleUI("#toggle-non-caps", "#val-toggle-non-caps", config_.allLower);
        setToggleUI("#toggle-caps-first", "#val-toggle-caps-first", config_.capsFirst);
        setToggleUI("#toggle-caps-each", "#val-toggle-caps-each", config_.capsEach);
        setToggleUI("#toggle-remove-mark", "#val-toggle-remove-mark", config_.removeMark);
        setToggleUI("#toggle-alert", "#val-toggle-alert", config_.alertDone);
        setToggleUI("#toggle-auto-paste", "#val-toggle-auto-paste", config_.autoPaste);
        setToggleUI("#toggle-sequential", "#val-toggle-sequential", config_.sequential);

        // Set encoding dropdowns
        setDropdownUI("#source-encoding", config_.sourceEncoding);
        setDropdownUI("#dest-encoding", config_.destEncoding);

        // Set hotkey toggles
        setToggleUI("#hotkey-ctrl", "#val-hotkey-ctrl", config_.hotkey.ctrl);
        setToggleUI("#hotkey-alt", "#val-hotkey-alt", config_.hotkey.alt);
        setToggleUI("#hotkey-win", "#val-hotkey-win", config_.hotkey.win);
        setToggleUI("#hotkey-shift", "#val-hotkey-shift", config_.hotkey.shift);

        // Set hotkey character
        if (config_.hotkey.key != 0) {
            wchar_t keyStr[2] = { config_.hotkey.key, 0 };
            setHotkeyCharUI(keyStr);
        }

        return true;
    }

    // Handle BUTTON_CLICK for close and convert buttons
    if (params.cmd == BUTTON_CLICK) {
        sciter::dom::element el(params.heTarget);
        std::wstring id = el.get_attribute("id");

        if (id == L"btn-close") {
            PostMessage(get_hwnd(), WM_CLOSE, 0, 0);
            return true;
        }
        if (id == L"btn-convert") {
            doConvert();
            return true;
        }
    }

    // Handle VALUE_CHANGED for toggles, encodings, hotkeys, and actions
    if (params.cmd == VALUE_CHANGED) {
        sciter::dom::element el(params.heTarget);
        std::wstring id = el.get_attribute("id");

        if (id == L"val-action") {
            sciter::value val = el.get_value();
            std::wstring action = val.is_string() ? val.get<std::wstring>() : L"";

            if (action == L"close") {
                PostMessage(get_hwnd(), WM_CLOSE, 0, 0);
            }

            el.set_value(sciter::value(L""));
            return true;
        }

        // Toggle options — save on change
        bool needSave = false;
        if (id == L"val-toggle-all-caps") { config_.allCaps = getToggleValue("#val-toggle-all-caps"); needSave = true; }
        else if (id == L"val-toggle-non-caps") { config_.allLower = getToggleValue("#val-toggle-non-caps"); needSave = true; }
        else if (id == L"val-toggle-caps-first") { config_.capsFirst = getToggleValue("#val-toggle-caps-first"); needSave = true; }
        else if (id == L"val-toggle-caps-each") { config_.capsEach = getToggleValue("#val-toggle-caps-each"); needSave = true; }
        else if (id == L"val-toggle-remove-mark") { config_.removeMark = getToggleValue("#val-toggle-remove-mark"); needSave = true; }
        else if (id == L"val-toggle-alert") { config_.alertDone = getToggleValue("#val-toggle-alert"); needSave = true; }
        else if (id == L"val-toggle-auto-paste") { config_.autoPaste = getToggleValue("#val-toggle-auto-paste"); needSave = true; }
        else if (id == L"val-toggle-sequential") { config_.sequential = getToggleValue("#val-toggle-sequential"); needSave = true; }
        // Encoding dropdowns
        else if (id == L"source-encoding") { config_.sourceEncoding = static_cast<uint8_t>(getDropdownValue("#source-encoding")); needSave = true; }
        else if (id == L"dest-encoding") { config_.destEncoding = static_cast<uint8_t>(getDropdownValue("#dest-encoding")); needSave = true; }
        // Hotkey modifiers
        else if (id == L"val-hotkey-ctrl") { config_.hotkey.ctrl = getToggleValue("#val-hotkey-ctrl"); needSave = true; }
        else if (id == L"val-hotkey-alt") { config_.hotkey.alt = getToggleValue("#val-hotkey-alt"); needSave = true; }
        else if (id == L"val-hotkey-win") { config_.hotkey.win = getToggleValue("#val-hotkey-win"); needSave = true; }
        else if (id == L"val-hotkey-shift") { config_.hotkey.shift = getToggleValue("#val-hotkey-shift"); needSave = true; }
        // Hotkey character
        else if (id == L"hotkey-char") {
            std::wstring keyStr = getHiddenValue("#hotkey-char");
            if (keyStr == L"Space") {
                config_.hotkey.key = L' ';
            } else if (!keyStr.empty()) {
                config_.hotkey.key = towupper(keyStr[0]);
            } else {
                config_.hotkey.key = 0;
            }
            needSave = true;
        }

        if (needSave) {
            saveConvertConfig();
        }
    }

    return sciter::window::handle_event(he, params);
}

// ═══════════════════════════════════════════════════════════
// UI reading helpers
// ═══════════════════════════════════════════════════════════

bool ConvertToolDialog::getToggleValue(const char* id) {
    sciter::dom::element root = get_root();
    sciter::dom::element el = root.find_first(id);
    if (!el.is_valid()) return false;
    sciter::value val = el.get_value();
    return val.is_string() && val.get<std::wstring>() == L"1";
}

int ConvertToolDialog::getDropdownValue(const char* id) {
    sciter::dom::element root = get_root();
    sciter::dom::element el = root.find_first(id);
    if (!el.is_valid()) return 0;
    sciter::value val = el.get_value();
    if (val.is_string()) {
        std::wstring s = val.get<std::wstring>();
        if (!s.empty()) return std::stoi(s);
    } else if (val.is_int()) {
        return val.get<int>();
    }
    return 0;
}

std::wstring ConvertToolDialog::getHiddenValue(const char* id) {
    sciter::dom::element root = get_root();
    sciter::dom::element el = root.find_first(id);
    if (!el.is_valid()) return L"";
    sciter::value val = el.get_value();
    return val.is_string() ? val.get<std::wstring>() : L"";
}

// ═══════════════════════════════════════════════════════════
// Clipboard I/O
// ═══════════════════════════════════════════════════════════

std::wstring ConvertToolDialog::readClipboardText() {
    if (!OpenClipboard(nullptr)) return L"";
    HANDLE hData = GetClipboardData(CF_UNICODETEXT);
    if (!hData) {
        CloseClipboard();
        return L"";
    }
    auto* pText = static_cast<const wchar_t*>(GlobalLock(hData));
    if (!pText) {
        CloseClipboard();
        return L"";
    }
    std::wstring text(pText);
    GlobalUnlock(hData);
    CloseClipboard();
    return text;
}

bool ConvertToolDialog::writeClipboardText(const std::wstring& text) {
    if (!OpenClipboard(nullptr)) return false;
    EmptyClipboard();
    size_t bytes = (text.size() + 1) * sizeof(wchar_t);
    HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, bytes);
    if (!hMem) {
        CloseClipboard();
        return false;
    }
    auto* pDst = static_cast<wchar_t*>(GlobalLock(hMem));
    if (!pDst) {
        GlobalFree(hMem);
        CloseClipboard();
        return false;
    }
    memcpy(pDst, text.c_str(), bytes);
    GlobalUnlock(hMem);
    SetClipboardData(CF_UNICODETEXT, hMem);
    CloseClipboard();
    return true;
}

// ═══════════════════════════════════════════════════════════
// File I/O
// ═══════════════════════════════════════════════════════════

std::wstring ConvertToolDialog::readFileContent(const std::wstring& path, CodeTable sourceTable) {
    HANDLE hFile = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ,
                               nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hFile == INVALID_HANDLE_VALUE) return L"";

    DWORD fileSize = GetFileSize(hFile, nullptr);
    if (fileSize == INVALID_FILE_SIZE || fileSize == 0) {
        CloseHandle(hFile);
        return L"";
    }

    std::vector<BYTE> buffer(fileSize);
    DWORD bytesRead = 0;
    if (!ReadFile(hFile, buffer.data(), fileSize, &bytesRead, nullptr)) {
        CloseHandle(hFile);
        return L"";
    }
    CloseHandle(hFile);

    if (sourceTable == CodeTable::Unicode) {
        // Read as UTF-8
        int wideLen = MultiByteToWideChar(CP_UTF8, 0,
            reinterpret_cast<const char*>(buffer.data()), static_cast<int>(bytesRead),
            nullptr, 0);
        if (wideLen <= 0) return L"";
        std::wstring result(wideLen, 0);
        MultiByteToWideChar(CP_UTF8, 0,
            reinterpret_cast<const char*>(buffer.data()), static_cast<int>(bytesRead),
            result.data(), wideLen);
        return result;
    }

    // Legacy encodings: widen each byte to wchar_t
    std::wstring result;
    result.reserve(bytesRead);
    for (DWORD i = 0; i < bytesRead; ++i) {
        result += static_cast<wchar_t>(buffer[i]);
    }
    return result;
}

bool ConvertToolDialog::writeFileContent(const std::wstring& path, const std::wstring& text,
                                         CodeTable destTable) {
    HANDLE hFile = CreateFileW(path.c_str(), GENERIC_WRITE, 0,
                               nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hFile == INVALID_HANDLE_VALUE) return false;

    DWORD bytesWritten = 0;
    bool ok = false;

    if (destTable == CodeTable::Unicode) {
        // Write as UTF-8
        int utf8Len = WideCharToMultiByte(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()),
                                          nullptr, 0, nullptr, nullptr);
        if (utf8Len > 0) {
            std::vector<char> utf8(utf8Len);
            WideCharToMultiByte(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()),
                                utf8.data(), utf8Len, nullptr, nullptr);
            ok = WriteFile(hFile, utf8.data(), utf8Len, &bytesWritten, nullptr) != FALSE;
        }
    } else {
        // Legacy encodings: narrow each wchar_t to byte
        std::vector<BYTE> bytes;
        bytes.reserve(text.size());
        for (wchar_t ch : text) {
            bytes.push_back(static_cast<BYTE>(ch & 0xFF));
        }
        ok = WriteFile(hFile, bytes.data(), static_cast<DWORD>(bytes.size()),
                       &bytesWritten, nullptr) != FALSE;
    }

    CloseHandle(hFile);
    return ok;
}

// ═══════════════════════════════════════════════════════════
// Conversion pipeline
// ═══════════════════════════════════════════════════════════

void ConvertToolDialog::doConvert() {
    // 1. Read toggle states
    bool allCaps      = getToggleValue("#val-toggle-all-caps");
    bool allLower     = getToggleValue("#val-toggle-non-caps");
    bool capsFirst    = getToggleValue("#val-toggle-caps-first");
    bool capsEach     = getToggleValue("#val-toggle-caps-each");
    bool removeMark   = getToggleValue("#val-toggle-remove-mark");
    bool alertDone    = getToggleValue("#val-toggle-alert");

    // 2. Read encodings
    int srcIdx = getDropdownValue("#source-encoding");
    int dstIdx = getDropdownValue("#dest-encoding");
    auto srcTable = static_cast<CodeTable>(srcIdx);
    auto dstTable = static_cast<CodeTable>(dstIdx);

    // 3. Read source type and input text
    std::wstring sourceType = getHiddenValue("#val-source-type");
    std::wstring input;

    if (sourceType == L"file") {
        std::wstring srcPath = getHiddenValue("#val-source-file");
        if (srcPath.empty()) {
            MessageBoxW(get_hwnd(), L"Chưa chọn file nguồn.", L"NexusKey", MB_OK | MB_ICONWARNING);
            return;
        }
        input = readFileContent(srcPath, srcTable);
        if (input.empty()) {
            MessageBoxW(get_hwnd(), L"Không thể đọc file nguồn.", L"NexusKey", MB_OK | MB_ICONERROR);
            return;
        }
    } else {
        input = readClipboardText();
        if (input.empty()) {
            MessageBoxW(get_hwnd(), L"Clipboard trống.", L"NexusKey", MB_OK | MB_ICONWARNING);
            return;
        }
    }

    // 4. Decode: source encoding → Unicode
    std::wstring unicode = CodeTableConverter::DecodeString(input, srcTable);

    // 5. Apply text transformations (on Unicode)
    if (removeMark) {
        unicode = CodeTableConverter::RemoveDiacritics(unicode);
    }
    if (allCaps) {
        unicode = CodeTableConverter::ToUpper(unicode);
    } else if (allLower) {
        unicode = CodeTableConverter::ToLower(unicode);
    } else if (capsFirst) {
        unicode = CodeTableConverter::CapitalizeFirstOfSentence(unicode);
    } else if (capsEach) {
        unicode = CodeTableConverter::CapitalizeEachWord(unicode);
    }

    // 6. Encode: Unicode → dest encoding
    std::wstring output = CodeTableConverter::EncodeString(unicode, dstTable);

    // 7. Write output
    if (sourceType == L"file") {
        std::wstring dstPath = getHiddenValue("#val-dest-file");
        if (dstPath.empty()) {
            MessageBoxW(get_hwnd(), L"Chưa chọn file đích.", L"NexusKey", MB_OK | MB_ICONWARNING);
            return;
        }
        if (!writeFileContent(dstPath, output, dstTable)) {
            MessageBoxW(get_hwnd(), L"Không thể ghi file đích.", L"NexusKey", MB_OK | MB_ICONERROR);
            return;
        }
    } else {
        if (!writeClipboardText(output)) {
            MessageBoxW(get_hwnd(), L"Không thể ghi vào clipboard.", L"NexusKey", MB_OK | MB_ICONERROR);
            return;
        }
    }

    // 8. Alert on completion
    if (alertDone) {
        MessageBoxW(get_hwnd(), L"Chuyển mã thành công!", L"NexusKey", MB_OK | MB_ICONINFORMATION);
    }
}

// ═══════════════════════════════════════════════════════════
// Config persistence
// ═══════════════════════════════════════════════════════════

void ConvertToolDialog::saveConvertConfig() {
    (void)ConfigManager::SaveConvertConfig(ConfigManager::GetConfigPath(), config_);

    // Signal HookEngine to reload convert config + hotkey
    ConfigEvent event;
    if (event.Initialize()) {
        event.Signal();
    }
}

void ConvertToolDialog::setToggleUI(const char* toggleId, const char* hiddenId, bool value) {
    sciter::dom::element root = get_root();

    // Set visual toggle class
    sciter::dom::element toggle = root.find_first(toggleId);
    if (toggle.is_valid()) {
        std::wstring cls = toggle.get_attribute("class");
        if (value && cls.find(L"checked") == std::wstring::npos) {
            toggle.set_attribute("class", (cls + L" checked").c_str());
        } else if (!value && cls.find(L"checked") != std::wstring::npos) {
            // Remove "checked" from class string
            auto pos = cls.find(L" checked");
            if (pos != std::wstring::npos) cls.erase(pos, 8);
            pos = cls.find(L"checked ");
            if (pos != std::wstring::npos) cls.erase(pos, 8);
            pos = cls.find(L"checked");
            if (pos != std::wstring::npos) cls.erase(pos, 7);
            toggle.set_attribute("class", cls.c_str());
        }
    }

    // Set hidden input value
    sciter::dom::element hidden = root.find_first(hiddenId);
    if (hidden.is_valid()) {
        hidden.set_value(sciter::value(value ? L"1" : L"0"));
    }
}

void ConvertToolDialog::setDropdownUI(const char* id, int value) {
    sciter::dom::element root = get_root();
    sciter::dom::element dropdown = root.find_first(id);
    if (dropdown.is_valid()) {
        dropdown.set_value(sciter::value(std::to_wstring(value)));
    }
}

void ConvertToolDialog::setHotkeyCharUI(const std::wstring& keyStr) {
    sciter::dom::element root = get_root();
    sciter::dom::element input = root.find_first("#hotkey-char");
    if (input.is_valid()) {
        if (keyStr == L" ") {
            input.set_value(sciter::value(L"Space"));
        } else {
            input.set_value(sciter::value(keyStr));
        }
    }
}

}  // namespace NextKey
