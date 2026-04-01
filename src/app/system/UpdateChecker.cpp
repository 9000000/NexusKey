// NexusKey - Update Checker Implementation
// SPDX-License-Identifier: GPL-3.0-only

#include "UpdateChecker.h"
#include "UpdateSecurity.h"
#include "core/Version.h"
#include "core/Strings.h"
#include "core/WinStrings.h"

#include <ole2.h>
#include <urlmon.h>
#include <CommCtrl.h>
#include <ShlObj.h>
#include <fstream>
#include <sstream>
#include <algorithm>

#pragma comment(lib, "urlmon.lib")

namespace NextKey {

namespace {

/// Get a temporary file path with the given filename
std::wstring GetTempFilePath(const wchar_t* filename) {
    wchar_t tempDir[MAX_PATH] = {};
    GetTempPathW(MAX_PATH, tempDir);
    return std::wstring(tempDir) + filename;
}

}  // namespace

std::string UpdateChecker::DownloadToString(const std::wstring& url) noexcept {
    try {
        std::wstring tempFile = GetTempFilePath(L"nexuskey_update_check.tmp");

        // URLDownloadToFileW is the simplest WinAPI HTTP download
        HRESULT hr = URLDownloadToFileW(nullptr, url.c_str(), tempFile.c_str(), 0, nullptr);
        if (FAILED(hr)) {
            DeleteFileW(tempFile.c_str());
            return {};
        }

        // Read the temp file
        std::ifstream file(tempFile, std::ios::binary);
        if (!file.is_open()) {
            DeleteFileW(tempFile.c_str());
            return {};
        }

        std::ostringstream ss;
        ss << file.rdbuf();
        file.close();
        DeleteFileW(tempFile.c_str());

        return ss.str();
    } catch (...) {
        return {};
    }
}

std::string UpdateChecker::ExtractJsonString(const std::string& json, const std::string& key) {
    // Simple JSON string extraction: find "key":"value" or "key": "value"
    std::string searchKey = "\"" + key + "\"";
    auto pos = json.find(searchKey);
    if (pos == std::string::npos) return {};

    // Skip key and find colon
    pos += searchKey.size();
    pos = json.find(':', pos);
    if (pos == std::string::npos) return {};
    pos++;

    // Skip whitespace
    while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\t' || json[pos] == '\n' || json[pos] == '\r')) {
        pos++;
    }

    // Expect opening quote
    if (pos >= json.size() || json[pos] != '"') return {};
    pos++;

    // Find closing quote (handle escaped quotes)
    std::string result;
    while (pos < json.size() && json[pos] != '"') {
        if (json[pos] == '\\' && pos + 1 < json.size()) {
            pos++;  // Skip escape char
            if (json[pos] == 'n') result += '\n';
            else if (json[pos] == 't') result += '\t';
            else result += json[pos];
        } else {
            result += json[pos];
        }
        pos++;
    }

    return result;
}

std::string UpdateChecker::FindAssetUrl(const std::string& json, const std::string& assetName) {
    // Find the asset with matching name in the "assets" array
    // Look for "name":"assetName" then find nearest "browser_download_url"
    std::string searchName = "\"name\":\"" + assetName + "\"";

    // Also try with space after colon
    auto pos = json.find(searchName);
    if (pos == std::string::npos) {
        searchName = "\"name\": \"" + assetName + "\"";
        pos = json.find(searchName);
    }
    if (pos == std::string::npos) return {};

    // Search for browser_download_url starting from the asset name position
    // GitHub API usually puts browser_download_url after the name in the asset object.
    return ExtractJsonString(json.substr(pos), "browser_download_url");
}

uint32_t UpdateChecker::ParseVersion(const std::wstring& versionStr) noexcept {
    if (versionStr.empty()) return 0;

    std::wstring ver = versionStr;

    // Strip 'v' or 'V' prefix
    if (ver[0] == L'v' || ver[0] == L'V') {
        ver = ver.substr(1);
    }

    // Strip suffix like -rc1, -beta, etc.
    auto dashPos = ver.find(L'-');
    if (dashPos != std::wstring::npos) {
        ver = ver.substr(0, dashPos);
    }

    // Parse major.minor.patch
    unsigned int major = 0, minor = 0, patch = 0;
    if (swscanf_s(ver.c_str(), L"%u.%u.%u", &major, &minor, &patch) < 1) {
        return 0;
    }

    return (major << 16) | (minor << 8) | patch;
}

UpdateInfo UpdateChecker::CheckForUpdate() noexcept {
    UpdateInfo info;

    try {
        std::string response = DownloadToString(API_URL);
        if (response.empty()) return info;

        // Extract tag_name (version)
        std::string tagName = ExtractJsonString(response, "tag_name");
        if (tagName.empty()) return info;

        std::wstring version = Utf8ToWide(tagName);
        uint32_t remoteVersion = ParseVersion(version);
        uint32_t localVersion = NEXUSKEY_VERSION_PACKED;

        info.checkSucceeded = true;  // API call worked

        if (remoteVersion <= localVersion) return info;

        // Extract release page URL
        std::string htmlUrl = ExtractJsonString(response, "html_url");

        // Find architecture-specific asset
#ifdef _WIN64
        std::string assetName = "NextKey-x64.zip";
#else
        std::string assetName = "NextKey-x86.zip";
#endif

        std::string assetUrl = FindAssetUrl(response, assetName);

        // Fallback: try generic name
        if (assetUrl.empty()) {
            assetUrl = FindAssetUrl(response, "NexusKey.zip");
        }

        if (assetUrl.empty()) return info;

        // SEC-003: Validate download URL points to allowed GitHub domain
        if (!IsAllowedDownloadUrl(assetUrl)) {
            return info;  // Reject non-GitHub URLs — possible API response tampering
        }

        info.available = true;
        info.version = version;
        // Strip 'v' prefix for display
        if (!info.version.empty() && (info.version[0] == L'v' || info.version[0] == L'V')) {
            info.version = info.version.substr(1);
        }
        info.downloadUrl = Utf8ToWide(assetUrl);
        info.changelogUrl = Utf8ToWide(htmlUrl);
        info.packedVersion = remoteVersion;
    } catch (...) {
        // Network or parsing error
    }

    return info;
}

bool UpdateChecker::DownloadFile(const std::wstring& url, const std::wstring& localPath) noexcept {
    HRESULT hr = URLDownloadToFileW(nullptr, url.c_str(), localPath.c_str(), 0, nullptr);
    return SUCCEEDED(hr);
}

bool UpdateChecker::ShowUpdateDialog(HWND parent, const UpdateInfo& info) {
    // Format the content string with version
    wchar_t content[256];
    swprintf_s(content, S(StringId::UPDATE_AVAILABLE_BODY), info.version.c_str());

    // Build changelog link text
    std::wstring footerText;
    if (!info.changelogUrl.empty()) {
        footerText = L"<a href=\"";
        footerText += info.changelogUrl;
        footerText += L"\">";
        footerText += S(StringId::UPDATE_CHANGELOG);
        footerText += L"</a>";
    }

    TASKDIALOGCONFIG tdc = {};
    tdc.cbSize = sizeof(tdc);
    tdc.hwndParent = parent;
    tdc.dwFlags = TDF_ENABLE_HYPERLINKS | TDF_USE_COMMAND_LINKS;
    tdc.pszWindowTitle = L"NexusKey";
    tdc.pszMainIcon = TD_INFORMATION_ICON;
    tdc.pszMainInstruction = S(StringId::UPDATE_AVAILABLE_TITLE);
    tdc.pszContent = content;

    if (!footerText.empty()) {
        tdc.pszFooter = footerText.c_str();
        tdc.pszFooterIcon = TD_INFORMATION_ICON;
    }

    TASKDIALOG_BUTTON buttons[] = {
        { 1001, S(StringId::UPDATE_NOW) },
        { 1002, S(StringId::UPDATE_SKIP) },
    };
    tdc.pButtons = buttons;
    tdc.cButtons = 2;
    tdc.nDefaultButton = 1001;

    // Hyperlink callback
    tdc.pfCallback = [](HWND, UINT notification, WPARAM, LPARAM lParam, LONG_PTR) -> HRESULT {
        if (notification == TDN_HYPERLINK_CLICKED) {
            ShellExecuteW(nullptr, L"open", reinterpret_cast<LPCWSTR>(lParam), nullptr, nullptr, SW_SHOW);
        }
        return S_OK;
    };

    int button = 0;
    HRESULT hr = TaskDialogIndirect(&tdc, &button, nullptr, nullptr);
    if (FAILED(hr)) return false;

    return button == 1001;
}

void UpdateChecker::ShowUpToDateMessage(HWND parent) {
    TaskDialog(parent, nullptr, L"NexusKey", S(StringId::UPDATE_TITLE),
               S(StringId::UPDATE_LATEST), TDCBF_OK_BUTTON, TD_INFORMATION_ICON, nullptr);
}

void UpdateChecker::ShowCheckFailedMessage(HWND parent) {
    TaskDialog(parent, nullptr, L"NexusKey", S(StringId::UPDATE_TITLE),
               S(StringId::UPDATE_FAILED), TDCBF_OK_BUTTON, TD_WARNING_ICON, nullptr);
}

bool UpdateChecker::DownloadAndLaunchInstaller(const std::wstring& downloadUrl) noexcept {
    try {
        wchar_t tempDir[MAX_PATH] = {};
        GetTempPathW(MAX_PATH, tempDir);
        std::wstring zipPath = std::wstring(tempDir) + L"NexusKey_update.zip";

        if (!DownloadFile(downloadUrl, zipPath)) return false;

        // SEC-001: Verify ZIP hash against .sha256 sidecar
        if (!VerifyDownloadedZip(downloadUrl, zipPath)) {
            DeleteFileW(zipPath.c_str());
            return false;
        }

        // Launch updater: self --install-update <zip>
        wchar_t exePath[MAX_PATH] = {};
        GetModuleFileNameW(nullptr, exePath, MAX_PATH);

        std::wstring cmdLine = L"\"";
        cmdLine += exePath;
        cmdLine += L"\" --install-update \"";
        cmdLine += zipPath;
        cmdLine += L"\"";

        STARTUPINFOW si = { sizeof(si) };
        PROCESS_INFORMATION pi = {};
        if (!CreateProcessW(nullptr, cmdLine.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi)) {
            return false;
        }
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
        return true;
    } catch (...) {
        return false;
    }
}

}  // namespace NextKey
