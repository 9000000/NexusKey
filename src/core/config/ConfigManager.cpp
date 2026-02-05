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
                } else {
                    config.inputMethod = InputMethod::Telex;
                }
            }
        }
        
        // [features] section
        if (auto features = table["features"].as_table()) {
            config.spellCheckEnabled = features->get("spell_check")->value_or(false);
            config.optimizeLevel = static_cast<uint8_t>(
                features->get("optimize_level")->value_or(0)
            );
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
        toml::table tbl;
        
        // [input] section
        toml::table input;
        input.insert("method", config.inputMethod == InputMethod::VNI ? "vni" : "telex");
        tbl.insert("input", std::move(input));
        
        // [features] section
        toml::table features;
        features.insert("spell_check", config.spellCheckEnabled);
        features.insert("optimize_level", static_cast<int64_t>(config.optimizeLevel));
        tbl.insert("features", std::move(features));
        
        // Write to file
        std::string utf8Path = WideToUtf8(path);
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

}  // namespace NextKey
