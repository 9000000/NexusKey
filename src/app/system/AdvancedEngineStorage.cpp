// VKey - Advanced engine staging and activation
// SPDX-License-Identifier: GPL-3.0-only

#include "AdvancedEngineStorage.h"

#include <bcrypt.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <cwchar>
#include <limits>
#include <utility>
#include <vector>

namespace NextKey {

AdvancedEngineStagingFile::~AdvancedEngineStagingFile() {
    if (handle_ != INVALID_HANDLE_VALUE) {
        ::CloseHandle(handle_);
    }
    if (!activated_ && !ownedPath_.empty()) {
        ::DeleteFileW(ownedPath_.c_str());
    }
}

bool AdvancedEngineStagingFile::Create(const std::wstring& directory) {
    if (handle_ != INVALID_HANDLE_VALUE || !ownedPath_.empty()) {
        return false;
    }

    for (unsigned attempt = 0; attempt < 16; ++attempt) {
        std::uint64_t randomValue = 0;
        if (!BCRYPT_SUCCESS(::BCryptGenRandom(nullptr, reinterpret_cast<PUCHAR>(&randomValue),
                                              static_cast<ULONG>(sizeof(randomValue)),
                                              BCRYPT_USE_SYSTEM_PREFERRED_RNG))) {
            return false;
        }

        wchar_t suffix[64] = {};
        if (swprintf_s(suffix, L".vkey-engine-%08lx-%016llx.tmp", static_cast<unsigned long>(::GetCurrentProcessId()),
                       static_cast<unsigned long long>(randomValue)) < 0) {
            return false;
        }

        std::wstring candidatePath = directory + L"\\" + suffix;
        HANDLE candidateHandle = ::CreateFileW(candidatePath.c_str(), GENERIC_READ | GENERIC_WRITE | DELETE, 0, nullptr,
                                               CREATE_NEW, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
        if (candidateHandle != INVALID_HANDLE_VALUE) {
            ownedPath_ = std::move(candidatePath);
            handle_ = candidateHandle;
            return true;
        }
        const DWORD error = ::GetLastError();
        if (error != ERROR_FILE_EXISTS && error != ERROR_ALREADY_EXISTS) {
            return false;
        }
    }
    return false;
}

bool AdvancedEngineStagingFile::ActivateAs(const std::wstring& destination) {
    if (handle_ == INVALID_HANDLE_VALUE || destination.empty()) {
        return false;
    }

    const size_t nameBytes = destination.size() * sizeof(wchar_t);
    const size_t infoBytes = offsetof(FILE_RENAME_INFO, FileName) + nameBytes;
    if (nameBytes > (std::numeric_limits<DWORD>::max)() || infoBytes > (std::numeric_limits<DWORD>::max)()) {
        return false;
    }

    const size_t words = (infoBytes + sizeof(std::max_align_t) - 1) / sizeof(std::max_align_t);
    std::vector<std::max_align_t> storage(words);
    std::memset(storage.data(), 0, words * sizeof(std::max_align_t));
    auto* info = reinterpret_cast<FILE_RENAME_INFO*>(storage.data());
    info->ReplaceIfExists = TRUE;
    info->RootDirectory = nullptr;
    info->FileNameLength = static_cast<DWORD>(nameBytes);
    std::memcpy(info->FileName, destination.data(), nameBytes);

    if (!::SetFileInformationByHandle(handle_, FileRenameInfo, info, static_cast<DWORD>(infoBytes))) {
        return false;
    }
    activated_ = true;
    return true;
}

} // namespace NextKey
