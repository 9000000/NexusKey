// VKey - Advanced engine installer
// SPDX-License-Identifier: AGPL-3.0-only

#include "AdvancedEngineInstaller.h"

#include "AdvancedEngineDownloadPolicy.h"
#include "AdvancedEngineStorage.h"
#include "UpdateChecker.h"
#include "core/CrashLog.h"
#include "core/Strings.h"
#include "core/Version.h"
#include "core/WinFileSystem.h"
#include "core/engine/RustEngineTrust.h"

#include <winhttp.h>

#include <array>
#include <atomic>
#include <cstdint>
#include <exception>
#include <limits>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace NextKey {
namespace {

constexpr DWORD kConnectTimeoutMs = 5'000;
constexpr DWORD kSendTimeoutMs = 15'000;
constexpr DWORD kReceiveTimeoutMs = 15'000;
constexpr DWORD kMaxRedirectLocationBytes = 8 * 1024;
constexpr unsigned kMaxRedirects = 5;

enum class InstallResult : std::uint8_t {
    Installed,
    Cancelled,
    NetworkFailure,
    VerificationFailure,
    StorageFailure,
    ActivationFailure,
};

class UniqueInternet final {
public:
    explicit UniqueInternet(HINTERNET value = nullptr) noexcept : value_(value) {}
    ~UniqueInternet() {
        if (value_) {
            ::WinHttpCloseHandle(value_);
        }
    }

    UniqueInternet(const UniqueInternet&) = delete;
    UniqueInternet& operator=(const UniqueInternet&) = delete;

    [[nodiscard]] HINTERNET get() const noexcept { return value_; }

private:
    HINTERNET value_;
};

struct ParsedUrl {
    std::wstring host;
    std::wstring resource;
};

std::wstring EnginePath(const std::wstring& directory) { return directory + L"\\" + kAdvancedEngineAssetName; }

bool IsTrustedInstalledEngine(const std::wstring& path) noexcept {
    UniqueFile file(::CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                                  FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, nullptr));
    return file.valid() && VerifyRustEngineFileHandle(file.get()) == RustEngineTrustStatus::Trusted;
}

bool ParseHttpsUrl(const std::wstring& url, ParsedUrl& parsed) {
    URL_COMPONENTS components{};
    components.dwStructSize = sizeof(components);
    components.dwSchemeLength = static_cast<DWORD>(-1);
    components.dwHostNameLength = static_cast<DWORD>(-1);
    components.dwUrlPathLength = static_cast<DWORD>(-1);
    components.dwExtraInfoLength = static_cast<DWORD>(-1);
    if (!::WinHttpCrackUrl(url.c_str(), static_cast<DWORD>(url.size()), 0, &components) ||
        components.nScheme != INTERNET_SCHEME_HTTPS || components.nPort != INTERNET_DEFAULT_HTTPS_PORT ||
        !components.lpszHostName || components.dwHostNameLength == 0) {
        return false;
    }

    parsed.host.assign(components.lpszHostName, components.dwHostNameLength);
    parsed.resource.assign(components.lpszUrlPath, components.dwUrlPathLength);
    if (parsed.resource.empty()) {
        parsed.resource = L"/";
    }
    if (components.lpszExtraInfo && components.dwExtraInfoLength != 0) {
        parsed.resource.append(components.lpszExtraInfo, components.dwExtraInfoLength);
    }
    return true;
}

bool QueryStatusCode(HINTERNET request, DWORD& status) noexcept {
    DWORD bytes = sizeof(status);
    return ::WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                                 WINHTTP_HEADER_NAME_BY_INDEX, &status, &bytes, WINHTTP_NO_HEADER_INDEX) != FALSE;
}

bool QueryRedirectLocation(HINTERNET request, std::wstring& location) {
    DWORD bytes = 0;
    if (::WinHttpQueryHeaders(request, WINHTTP_QUERY_LOCATION, WINHTTP_HEADER_NAME_BY_INDEX, WINHTTP_NO_OUTPUT_BUFFER,
                              &bytes, WINHTTP_NO_HEADER_INDEX) ||
        ::GetLastError() != ERROR_INSUFFICIENT_BUFFER || bytes == 0 || bytes > kMaxRedirectLocationBytes) {
        return false;
    }

    std::vector<wchar_t> buffer(bytes / sizeof(wchar_t) + 1, L'\0');
    if (!::WinHttpQueryHeaders(request, WINHTTP_QUERY_LOCATION, WINHTTP_HEADER_NAME_BY_INDEX, buffer.data(), &bytes,
                               WINHTTP_NO_HEADER_INDEX)) {
        return false;
    }
    location.assign(buffer.data(), bytes / sizeof(wchar_t));
    while (!location.empty() && location.back() == L'\0') {
        location.pop_back();
    }
    return !location.empty();
}

bool IsRedirectStatus(DWORD status) noexcept {
    return status == 301 || status == 302 || status == 303 || status == 307 || status == 308;
}

InstallResult StreamResponseToFile(HINTERNET request, HANDLE file, std::atomic<bool>& cancel) noexcept {
    const std::uint64_t expectedBytes = ExpectedRustEngineByteLength();
    if (expectedBytes > (std::numeric_limits<DWORD>::max)()) {
        return InstallResult::VerificationFailure;
    }

    DWORD contentLength = 0;
    DWORD contentLengthBytes = sizeof(contentLength);
    if (::WinHttpQueryHeaders(request, WINHTTP_QUERY_CONTENT_LENGTH | WINHTTP_QUERY_FLAG_NUMBER,
                              WINHTTP_HEADER_NAME_BY_INDEX, &contentLength, &contentLengthBytes,
                              WINHTTP_NO_HEADER_INDEX)) {
        if (contentLength != expectedBytes) {
            return InstallResult::VerificationFailure;
        }
    } else if (::GetLastError() != ERROR_WINHTTP_HEADER_NOT_FOUND) {
        return InstallResult::NetworkFailure;
    }

    std::array<std::uint8_t, 64 * 1024> buffer{};
    std::uint64_t totalBytes = 0;
    for (;;) {
        if (cancel.load(std::memory_order_relaxed)) {
            return InstallResult::Cancelled;
        }

        DWORD bytesRead = 0;
        if (!::WinHttpReadData(request, buffer.data(), static_cast<DWORD>(buffer.size()), &bytesRead)) {
            return InstallResult::NetworkFailure;
        }
        if (bytesRead == 0) {
            break;
        }
        if (totalBytes > expectedBytes || bytesRead > expectedBytes - totalBytes) {
            return InstallResult::VerificationFailure;
        }

        DWORD offset = 0;
        while (offset < bytesRead) {
            DWORD bytesWritten = 0;
            if (!::WriteFile(file, buffer.data() + offset, bytesRead - offset, &bytesWritten, nullptr) ||
                bytesWritten == 0) {
                return InstallResult::StorageFailure;
            }
            offset += bytesWritten;
        }
        totalBytes += bytesRead;
    }

    return totalBytes == expectedBytes ? InstallResult::Installed : InstallResult::VerificationFailure;
}

InstallResult DownloadToFile(HANDLE file, std::atomic<bool>& cancel) {
    UniqueInternet session(::WinHttpOpen(L"VKey/" VKEY_VERSION_WSTR, WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                         WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0));
    if (!session.get() ||
        !::WinHttpSetTimeouts(session.get(), kConnectTimeoutMs, kConnectTimeoutMs, kSendTimeoutMs, kReceiveTimeoutMs)) {
        return InstallResult::NetworkFailure;
    }

    std::wstring url = BuildAdvancedEngineReleaseUrl();
    for (unsigned redirectCount = 0; redirectCount <= kMaxRedirects; ++redirectCount) {
        if (cancel.load(std::memory_order_relaxed)) {
            return InstallResult::Cancelled;
        }
        if (!IsAllowedAdvancedEngineUrl(url)) {
            return InstallResult::VerificationFailure;
        }

        ParsedUrl parsed;
        if (!ParseHttpsUrl(url, parsed)) {
            return InstallResult::VerificationFailure;
        }

        UniqueInternet connection(::WinHttpConnect(session.get(), parsed.host.c_str(), INTERNET_DEFAULT_HTTPS_PORT, 0));
        if (!connection.get()) {
            return InstallResult::NetworkFailure;
        }

        UniqueInternet request(::WinHttpOpenRequest(connection.get(), L"GET", parsed.resource.c_str(), nullptr,
                                                    WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
                                                    WINHTTP_FLAG_SECURE));
        if (!request.get()) {
            return InstallResult::NetworkFailure;
        }

        DWORD redirectPolicy = WINHTTP_OPTION_REDIRECT_POLICY_NEVER;
        if (!::WinHttpSetOption(request.get(), WINHTTP_OPTION_REDIRECT_POLICY, &redirectPolicy,
                                sizeof(redirectPolicy)) ||
            !::WinHttpSendRequest(request.get(), WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) ||
            !::WinHttpReceiveResponse(request.get(), nullptr)) {
            return InstallResult::NetworkFailure;
        }

        DWORD status = 0;
        if (!QueryStatusCode(request.get(), status)) {
            return InstallResult::NetworkFailure;
        }
        if (IsRedirectStatus(status)) {
            if (redirectCount == kMaxRedirects || !QueryRedirectLocation(request.get(), url)) {
                return InstallResult::NetworkFailure;
            }
            continue;
        }
        if (status != HTTP_STATUS_OK) {
            return InstallResult::NetworkFailure;
        }
        return StreamResponseToFile(request.get(), file, cancel);
    }
    return InstallResult::NetworkFailure;
}

InstallResult InstallEngine(std::atomic<bool>& cancel) noexcept {
    try {
        const std::wstring directory = ModuleDirectory(nullptr);
        if (directory.empty()) {
            return InstallResult::StorageFailure;
        }

        AdvancedEngineStagingFile staging;
        if (!staging.Create(directory)) {
            return InstallResult::StorageFailure;
        }

        const InstallResult download = DownloadToFile(staging.get(), cancel);
        if (download != InstallResult::Installed) {
            return download;
        }
        if (VerifyRustEngineFileHandle(staging.get()) != RustEngineTrustStatus::Trusted) {
            return InstallResult::VerificationFailure;
        }
        if (!::FlushFileBuffers(staging.get())) {
            return InstallResult::StorageFailure;
        }
        if (cancel.load(std::memory_order_relaxed)) {
            return InstallResult::Cancelled;
        }
        if (!staging.ActivateAs(EnginePath(directory))) {
            return InstallResult::ActivationFailure;
        }
        return InstallResult::Installed;
    } catch (...) {
        return InstallResult::StorageFailure;
    }
}

void ShowInstallFailure(HWND parent, InstallResult result) {
    StringId message = StringId::SPELL_ADVANCED_INSTALL_FAILED;
    if (result == InstallResult::NetworkFailure) {
        message = StringId::SPELL_ADVANCED_NETWORK_FAILED;
    } else if (result == InstallResult::VerificationFailure) {
        message = StringId::SPELL_ADVANCED_VERIFY_FAILED;
    }
    ::MessageBoxW(parent, S(message), L"VKey", MB_OK | MB_ICONWARNING);
}

} // namespace

AdvancedEngineStatus AdvancedEngineInstaller::EnsureInstalledWithUi(HWND parent) {
    try {
        const std::wstring directory = ModuleDirectory(nullptr);
        if (!directory.empty() && IsTrustedInstalledEngine(EnginePath(directory))) {
            return AdvancedEngineStatus::Ready;
        }

        if (::MessageBoxW(parent, S(StringId::SPELL_ADVANCED_DOWNLOAD_PROMPT), L"VKey",
                          MB_YESNO | MB_ICONINFORMATION) != IDYES) {
            return AdvancedEngineStatus::Declined;
        }

        struct State {
            std::atomic<bool> done{false};
            std::atomic<bool> cancel{false};
            InstallResult result = InstallResult::StorageFailure;
        };
        auto state = std::make_shared<State>();
        std::thread worker([state]() {
            try {
                state->result = InstallEngine(state->cancel);
            } catch (const std::exception& error) {
                CrashLog(L"AdvancedEngineInstaller::thread", error.what());
                state->result = InstallResult::StorageFailure;
            } catch (...) {
                CrashLog(L"AdvancedEngineInstaller::thread", "(non-std exception)");
                state->result = InstallResult::StorageFailure;
            }
            state->done.store(true, std::memory_order_release);
        });

        const bool completed =
            UpdateChecker::ShowProgressDialog(parent, S(StringId::SPELL_ADVANCED_DOWNLOADING), state->done);
        if (!completed) {
            state->cancel.store(true, std::memory_order_release);
        }
        worker.join();
        // Result first: cancel is only polled at read-loop boundaries, so a
        // download that finished as the user dismissed the dialog is installed
        // and must not be thrown away.
        if (state->result == InstallResult::Installed) {
            return AdvancedEngineStatus::Ready;
        }
        if (!completed || state->result == InstallResult::Cancelled) {
            return AdvancedEngineStatus::Declined;
        }
        ShowInstallFailure(parent, state->result);
        return AdvancedEngineStatus::Unavailable;
    } catch (const std::exception& error) {
        CrashLog(L"AdvancedEngineInstaller::EnsureInstalledWithUi", error.what());
    } catch (...) {
        CrashLog(L"AdvancedEngineInstaller::EnsureInstalledWithUi", "(non-std exception)");
    }
    ShowInstallFailure(parent, InstallResult::StorageFailure);
    return AdvancedEngineStatus::Unavailable;
}

} // namespace NextKey
