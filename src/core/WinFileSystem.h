// VKey - Windows file handle and module path helpers
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#if defined(_WIN32)

// Deliberately does not define WIN32_LEAN_AND_MEAN — a shared header must not
// change what a later <windows.h> include in the same TU provides. Callers that
// want the lean subset define it before including this.
#include <windows.h>

#include <string>
#include <vector>

namespace NextKey {

class UniqueFile final {
public:
    explicit UniqueFile(HANDLE value = INVALID_HANDLE_VALUE) noexcept : value_(value) {}
    ~UniqueFile() {
        if (value_ != INVALID_HANDLE_VALUE) {
            ::CloseHandle(value_);
        }
    }

    UniqueFile(const UniqueFile&) = delete;
    UniqueFile& operator=(const UniqueFile&) = delete;

    [[nodiscard]] HANDLE get() const noexcept { return value_; }
    [[nodiscard]] bool valid() const noexcept { return value_ != INVALID_HANDLE_VALUE; }

private:
    HANDLE value_;
};

/// Full path of `module` (nullptr = the running executable), or empty on
/// failure. Grows past MAX_PATH so a long install path stays resolvable.
[[nodiscard]] inline std::wstring ModulePath(HMODULE module) {
    std::vector<wchar_t> buffer(512);
    while (buffer.size() <= 32768) {
        const DWORD length = ::GetModuleFileNameW(module, buffer.data(), static_cast<DWORD>(buffer.size()));
        if (length == 0) {
            return {};
        }
        if (static_cast<size_t>(length) < buffer.size()) {
            return std::wstring(buffer.data(), length);
        }
        buffer.resize(buffer.size() * 2);
    }
    return {};
}

/// Directory holding `module`, without a trailing separator; empty on failure.
[[nodiscard]] inline std::wstring ModuleDirectory(HMODULE module) {
    const std::wstring path = ModulePath(module);
    const size_t slash = path.find_last_of(L"\\/");
    return slash == std::wstring::npos ? std::wstring{} : path.substr(0, slash);
}

} // namespace NextKey

#endif
