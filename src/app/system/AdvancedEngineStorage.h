// VKey - Advanced engine staging and activation
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <windows.h>

#include <string>

namespace NextKey {

class AdvancedEngineStagingFile final {
public:
    AdvancedEngineStagingFile() = default;
    ~AdvancedEngineStagingFile();

    AdvancedEngineStagingFile(const AdvancedEngineStagingFile&) = delete;
    AdvancedEngineStagingFile& operator=(const AdvancedEngineStagingFile&) = delete;

    [[nodiscard]] bool Create(const std::wstring& directory);
    [[nodiscard]] HANDLE get() const noexcept { return handle_; }
    [[nodiscard]] bool ActivateAs(const std::wstring& destination);

private:
    HANDLE handle_ = INVALID_HANDLE_VALUE;
    std::wstring ownedPath_;
    bool activated_ = false;
};

} // namespace NextKey
