// VKey - Advanced engine storage tests
// SPDX-License-Identifier: AGPL-3.0-only

#include "app/system/AdvancedEngineStorage.h"
#include "app/system/AdvancedEngineDownloadPolicy.h"
#include "core/WinFileSystem.h"
#include "core/engine/RustEngineTrust.h"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <string>

namespace NextKey {
namespace {

class TempDirectory final {
public:
    ~TempDirectory() {
        if (!destination_.empty()) {
            ::DeleteFileW(destination_.c_str());
        }
        if (!path_.empty()) {
            ::RemoveDirectoryW(path_.c_str());
        }
    }

    TempDirectory(const TempDirectory&) = delete;
    TempDirectory& operator=(const TempDirectory&) = delete;

    TempDirectory() = default;

    [[nodiscard]] bool Create() {
        wchar_t base[MAX_PATH + 1] = {};
        wchar_t placeholder[MAX_PATH + 1] = {};
        if (::GetTempPathW(MAX_PATH, base) == 0 || ::GetTempFileNameW(base, L"VKE", 0, placeholder) == 0 ||
            !::DeleteFileW(placeholder) || !::CreateDirectoryW(placeholder, nullptr)) {
            return false;
        }
        path_ = placeholder;
        destination_ = path_ + L"\\" + kAdvancedEngineAssetName;
        return true;
    }

    [[nodiscard]] const std::wstring& path() const noexcept { return path_; }
    [[nodiscard]] const std::wstring& destination() const noexcept { return destination_; }

private:
    std::wstring path_;
    std::wstring destination_;
};

bool CopyIntoHandle(const std::wstring& sourcePath, HANDLE destination) {
    UniqueFile source(::CreateFileW(sourcePath.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                                    FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, nullptr));
    if (!source.valid()) {
        return false;
    }

    std::array<std::byte, 64 * 1024> buffer{};
    for (;;) {
        DWORD bytesRead = 0;
        if (!::ReadFile(source.get(), buffer.data(), static_cast<DWORD>(buffer.size()), &bytesRead, nullptr)) {
            return false;
        }
        if (bytesRead == 0) {
            return true;
        }
        DWORD offset = 0;
        while (offset < bytesRead) {
            DWORD bytesWritten = 0;
            if (!::WriteFile(destination, buffer.data() + offset, bytesRead - offset, &bytesWritten, nullptr) ||
                bytesWritten == 0) {
                return false;
            }
            offset += bytesWritten;
        }
    }
}

TEST(AdvancedEngineStorageTest, ReplacesUntrustedDestinationWithNormalTrustedFile) {
    const std::wstring executableDirectory = ModuleDirectory(nullptr);
    ASSERT_FALSE(executableDirectory.empty());
    const std::wstring trustedSource = executableDirectory + L"\\" + kAdvancedEngineAssetName;
    // The signature covers the engine's bytes, not its path, so the one deployed
    // beside the test binary verifies a staged copy of the same file.
    const std::wstring trustedSignature = trustedSource + L".sig";

    TempDirectory directory;
    ASSERT_TRUE(directory.Create());
    {
        UniqueFile fake(::CreateFileW(directory.destination().c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
                                      FILE_ATTRIBUTE_NORMAL, nullptr));
        ASSERT_TRUE(fake.valid());
        constexpr std::array fakeBytes{'f', 'a', 'k', 'e'};
        DWORD bytesWritten = 0;
        ASSERT_TRUE(
            ::WriteFile(fake.get(), fakeBytes.data(), static_cast<DWORD>(fakeBytes.size()), &bytesWritten, nullptr));
        ASSERT_EQ(bytesWritten, fakeBytes.size());
    }

    {
        AdvancedEngineStagingFile staging;
        ASSERT_TRUE(staging.Create(directory.path()));
        ASSERT_TRUE(CopyIntoHandle(trustedSource, staging.get()));
        ASSERT_EQ(VerifyRustEngineFileHandle(staging.get(), trustedSignature.c_str()), RustEngineTrustStatus::Trusted);
        ASSERT_TRUE(::FlushFileBuffers(staging.get()));
        ASSERT_TRUE(staging.ActivateAs(directory.destination()));
    }

    UniqueFile installed(::CreateFileW(directory.destination().c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                                       OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, nullptr));
    ASSERT_TRUE(installed.valid());
    EXPECT_EQ(VerifyRustEngineFileHandle(installed.get(), trustedSignature.c_str()), RustEngineTrustStatus::Trusted);

    const DWORD attributes = ::GetFileAttributesW(directory.destination().c_str());
    ASSERT_NE(attributes, INVALID_FILE_ATTRIBUTES);
    EXPECT_EQ(attributes & FILE_ATTRIBUTE_TEMPORARY, 0u);
}

} // namespace
} // namespace NextKey
