// NexusKey - Configuration Manager Implementation
// SPDX-License-Identifier: GPL-3.0-only

#include "ConfigManager.h"

#define TOML_HEADER_ONLY 1
#include "toml.hpp"

#include <fstream>
#include <sstream>
#include <filesystem>

#ifdef _WIN32
#include <Windows.h>
#include <ShlObj.h>
#endif

namespace NextKey {

namespace {

std::string WideToUtf8(const std::wstring& wstr) {
    if (wstr.empty()) return {};
    int len = WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), -1, nullptr, 0, nullptr, nullptr);
    std::string result(len - 1, 0);
    WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), -1, result.data(), len, nullptr, nullptr);
    return result;
}

std::wstring Utf8ToWide(const std::string& str) {
    if (str.empty()) return {};
    int len = MultiByteToWideChar(CP_UTF8, 0, str.c_str(), -1, nullptr, 0);
    std::wstring result(len - 1, 0);
    MultiByteToWideChar(CP_UTF8, 0, str.c_str(), -1, result.data(), len);
    return result;
}

}  // namespace

std::optional<TypingConfig> ConfigManager::LoadFromFile(const std::wstring& path) {
    try {
        std::string utf8Path = WideToUtf8(path);
        auto table = toml::parse_file(utf8Path);
        
        TypingConfig config;
        
        // [input] section
        if (auto input = table["input"].as_table()) {
            if (auto method = input->get("method")) {
                std::string methodStr = method->value_or<std::string>("telex");
                if (methodStr == "vni") {
                    config.inputMethod = InputMethod::VNI;
                } else if (methodStr == "simple_telex") {
                    config.inputMethod = InputMethod::SimpleTelex;
                } else {
                    config.inputMethod = InputMethod::Telex;
                }
            }
            if (auto ct = input->get("code_table")) {
                auto val = ct->value_or(0);
                if (val >= 0 && val <= 4) {
                    config.codeTable = static_cast<CodeTable>(val);
                }
            }
        }

        // [features] section — use node_view [] operator for safe access to optional keys
        if (auto features = table["features"].as_table()) {
            config.spellCheckEnabled = (*features)["spell_check"].value_or(false);
            config.beepOnSwitch = (*features)["beep_on_switch"].value_or(false);
            config.smartSwitch = (*features)["smart_switch"].value_or(false);
            config.excludeApps = (*features)["exclude_apps"].value_or(false);
            config.optimizeLevel = static_cast<uint8_t>(
                (*features)["optimize_level"].value_or(0)
            );
            config.modernOrtho = (*features)["modern_ortho"].value_or(false);
            config.autoCaps = (*features)["auto_caps"].value_or(false);
            config.allowZwjf = (*features)["allow_zwjf"].value_or(true);
        }
        
        return config;
    } catch (const toml::parse_error&) {
        // Fall through to return nullopt
    } catch (const std::exception&) {
        // Fall through to return nullopt
    }
    return std::nullopt;
}

bool ConfigManager::SaveToFile(const std::wstring& path, const TypingConfig& config) {
    try {
        // First, load existing config to preserve other sections (like [ui])
        toml::table tbl;
        std::string utf8Path = WideToUtf8(path);

        try {
            tbl = toml::parse_file(utf8Path);
        } catch (...) {
            // File doesn't exist or is invalid, start fresh
        }

        // Update [input] section
        toml::table input;
        const char* methodStr = "telex";
        if (config.inputMethod == InputMethod::VNI) methodStr = "vni";
        else if (config.inputMethod == InputMethod::SimpleTelex) methodStr = "simple_telex";
        input.insert_or_assign("method", methodStr);
        input.insert_or_assign("code_table", static_cast<int64_t>(config.codeTable));
        tbl.insert_or_assign("input", std::move(input));

        // Update [features] section
        toml::table features;
        features.insert_or_assign("spell_check", config.spellCheckEnabled);
        features.insert_or_assign("beep_on_switch", config.beepOnSwitch);
        features.insert_or_assign("smart_switch", config.smartSwitch);
        features.insert_or_assign("exclude_apps", config.excludeApps);
        features.insert_or_assign("optimize_level", static_cast<int64_t>(config.optimizeLevel));
        features.insert_or_assign("modern_ortho", config.modernOrtho);
        features.insert_or_assign("auto_caps", config.autoCaps);
        features.insert_or_assign("allow_zwjf", config.allowZwjf);
        tbl.insert_or_assign("features", std::move(features));

        // Write to file
        std::ofstream file(utf8Path);
        if (!file.is_open()) return false;

        file << tbl;
        return true;
    } catch (...) {
        return false;
    }
}

std::wstring ConfigManager::GetConfigPath() {
    // Primary: exe directory
    std::wstring exeDir = GetExeDirectory();
    if (DirectoryWritable(exeDir)) {
        return exeDir + L"\\config.toml";
    }
    
    // Fallback: %APPDATA%/NexusKey/
    std::wstring appDataDir = GetAppDataDirectory();
    return appDataDir + L"\\config.toml";
}

TypingConfig ConfigManager::LoadOrDefault() {
    std::wstring configPath = GetConfigPath();
    auto config = LoadFromFile(configPath);
    if (config) {
        return *config;
    }
    
    // Return compiled defaults (FR8 - engine autonomy)
    return TypingConfig{};
}

std::wstring ConfigManager::GetExeDirectory() {
#ifdef _WIN32
    wchar_t path[MAX_PATH] = {0};
    DWORD len = GetModuleFileNameW(nullptr, path, MAX_PATH);
    if (len > 0) {
        std::wstring fullPath(path);
        size_t lastSlash = fullPath.find_last_of(L"\\/");
        if (lastSlash != std::wstring::npos) {
            return fullPath.substr(0, lastSlash);
        }
    }
#endif
    return L".";
}

std::wstring ConfigManager::GetAppDataDirectory() {
#ifdef _WIN32
    wchar_t path[MAX_PATH] = {0};
    if (SUCCEEDED(SHGetFolderPathW(nullptr, CSIDL_APPDATA, nullptr, 0, path))) {
        std::wstring appData(path);
        std::wstring nexusKeyDir = appData + L"\\NexusKey";
        
        // Create directory if it doesn't exist
        CreateDirectoryW(nexusKeyDir.c_str(), nullptr);
        return nexusKeyDir;
    }
#endif
    return L".";
}

bool ConfigManager::DirectoryWritable(const std::wstring& path) {
#ifdef _WIN32
    // Try to create a temp file
    std::wstring testFile = path + L"\\__nexuskey_test_write__.tmp";
    HANDLE hFile = CreateFileW(
        testFile.c_str(),
        GENERIC_WRITE,
        0,
        nullptr,
        CREATE_ALWAYS,
        FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_DELETE_ON_CLOSE,
        nullptr
    );
    if (hFile != INVALID_HANDLE_VALUE) {
        CloseHandle(hFile);
        return true;
    }
#endif
    return false;
}

std::optional<UIConfig> ConfigManager::LoadUIConfig(const std::wstring& path) {
    try {
        std::string utf8Path = WideToUtf8(path);
        auto table = toml::parse_file(utf8Path);

        UIConfig config;

        // [ui] section — use [] for safe access to optional keys
        if (auto ui = table["ui"].as_table()) {
            config.showAdvanced = (*ui)["show_advanced"].value_or(false);
            config.backgroundOpacity = static_cast<uint8_t>(
                (*ui)["background_opacity"].value_or(80)
            );
            config.darkMode = (*ui)["dark_mode"].value_or(true);
            config.pinned = (*ui)["pinned"].value_or(false);
        }

        return config;
    } catch (...) {
        return std::nullopt;
    }
}

bool ConfigManager::SaveUIConfig(const std::wstring& path, const UIConfig& config) {
    try {
        // First, load existing config to preserve other sections
        toml::table tbl;
        std::string utf8Path = WideToUtf8(path);

        try {
            tbl = toml::parse_file(utf8Path);
        } catch (...) {
            // File doesn't exist or is invalid, start fresh
        }

        // Update [ui] section
        toml::table ui;
        ui.insert_or_assign("show_advanced", config.showAdvanced);
        ui.insert_or_assign("background_opacity", static_cast<int64_t>(config.backgroundOpacity));
        ui.insert_or_assign("dark_mode", config.darkMode);
        ui.insert_or_assign("pinned", config.pinned);
        tbl.insert_or_assign("ui", std::move(ui));

        // Write to file
        std::ofstream file(utf8Path);
        if (!file.is_open()) return false;

        file << tbl;
        return true;
    } catch (...) {
        return false;
    }
}

UIConfig ConfigManager::LoadUIConfigOrDefault() {
    std::wstring configPath = GetConfigPath();
    auto config = LoadUIConfig(configPath);
    if (config) {
        return *config;
    }
    return UIConfig{};
}

std::optional<HotkeyConfig> ConfigManager::LoadHotkeyConfig(const std::wstring& path) {
    try {
        std::string utf8Path = WideToUtf8(path);
        auto table = toml::parse_file(utf8Path);

        HotkeyConfig config;

        if (auto hotkey = table["hotkey"].as_table()) {
            config.ctrl = (*hotkey)["ctrl"].value_or(true);
            config.shift = (*hotkey)["shift"].value_or(true);
            config.alt = (*hotkey)["alt"].value_or(false);
            config.win = (*hotkey)["win"].value_or(false);

            auto keyStr = (*hotkey)["key"].value_or<std::string>("");
            if (!keyStr.empty()) {
                auto wideKey = Utf8ToWide(keyStr);
                config.key = wideKey.empty() ? 0 : towupper(wideKey[0]);
            } else {
                config.key = 0;
            }
        }

        return config;
    } catch (...) {
        return std::nullopt;
    }
}

bool ConfigManager::SaveHotkeyConfig(const std::wstring& path, const HotkeyConfig& config) {
    try {
        toml::table tbl;
        std::string utf8Path = WideToUtf8(path);

        try {
            tbl = toml::parse_file(utf8Path);
        } catch (...) {}

        toml::table hotkey;
        hotkey.insert_or_assign("ctrl", config.ctrl);
        hotkey.insert_or_assign("shift", config.shift);
        hotkey.insert_or_assign("alt", config.alt);
        hotkey.insert_or_assign("win", config.win);

        if (config.key != 0) {
            char keyStr[2] = { static_cast<char>(config.key), 0 };
            hotkey.insert_or_assign("key", std::string(keyStr));
        } else {
            hotkey.insert_or_assign("key", "");
        }

        tbl.insert_or_assign("hotkey", std::move(hotkey));

        std::ofstream file(utf8Path);
        if (!file.is_open()) return false;
        file << tbl;
        return true;
    } catch (...) {
        return false;
    }
}

HotkeyConfig ConfigManager::LoadHotkeyConfigOrDefault() {
    std::wstring configPath = GetConfigPath();
    auto config = LoadHotkeyConfig(configPath);
    if (config) {
        return *config;
    }
    return HotkeyConfig{};  // Default: Ctrl+Shift
}

std::vector<std::wstring> ConfigManager::LoadExcludedApps(const std::wstring& path) {
    std::vector<std::wstring> apps;
    try {
        std::string utf8Path = WideToUtf8(path);
        auto table = toml::parse_file(utf8Path);

        if (auto section = table["excluded_apps"].as_table()) {
            if (auto arr = (*section)["list"].as_array()) {
                for (auto& item : *arr) {
                    if (auto str = item.value<std::string>()) {
                        apps.push_back(Utf8ToWide(*str));
                    }
                }
            }
        }
    } catch (...) {}
    return apps;
}

bool ConfigManager::SaveExcludedApps(const std::wstring& path, const std::vector<std::wstring>& apps) {
    try {
        toml::table tbl;
        std::string utf8Path = WideToUtf8(path);

        try { tbl = toml::parse_file(utf8Path); } catch (...) {}

        toml::array arr;
        for (auto& app : apps) {
            arr.push_back(WideToUtf8(app));
        }
        toml::table section;
        section.insert_or_assign("list", std::move(arr));
        tbl.insert_or_assign("excluded_apps", std::move(section));

        std::ofstream file(utf8Path);
        if (!file.is_open()) return false;
        file << tbl;
        return true;
    } catch (...) {
        return false;
    }
}

std::unordered_map<std::wstring, bool> ConfigManager::LoadSmartSwitchData(const std::wstring& path) {
    std::unordered_map<std::wstring, bool> data;
    try {
        std::string utf8Path = WideToUtf8(path);
        auto table = toml::parse_file(utf8Path);

        if (auto section = table["smart_switch_data"].as_table()) {
            for (auto& [key, val] : *section) {
                data[Utf8ToWide(std::string(key.str()))] = val.value_or(true);
            }
        }
    } catch (...) {}
    return data;
}

bool ConfigManager::SaveSmartSwitchData(const std::wstring& path,
                                         const std::unordered_map<std::wstring, bool>& data) {
    try {
        toml::table tbl;
        std::string utf8Path = WideToUtf8(path);

        try { tbl = toml::parse_file(utf8Path); } catch (...) {}

        toml::table section;
        for (auto& [exe, vietnamese] : data) {
            section.insert_or_assign(WideToUtf8(exe), vietnamese);
        }
        tbl.insert_or_assign("smart_switch_data", std::move(section));

        std::ofstream file(utf8Path);
        if (!file.is_open()) return false;
        file << tbl;
        return true;
    } catch (...) {
        return false;
    }
}

}  // namespace NextKey
