// VKey - trusted Rust engine loader
// SPDX-License-Identifier: GPL-3.0-only

#include "RustEngineLoader.h"

#include "RustEngineTrust.h"
#include "VKeyEngineLock.h"
#include "vkey_engine.h"

#include <string>

static_assert(VKEY_ENGINE_ABI_VERSION == NextKey::VKeyEngineLock::kAbiVersion,
              "vkey_engine.h and engine.lock must come from the same sync");

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include "core/WinFileSystem.h"
#else
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <dlfcn.h>
#endif

namespace NextKey {
namespace {

const volatile int kModuleAnchor = 0;

#if defined(_WIN32)

class UniqueModule final {
public:
    explicit UniqueModule(HMODULE value = nullptr) noexcept : value_(value) {}
    ~UniqueModule() {
        if (value_) {
            ::FreeLibrary(value_);
        }
    }

    UniqueModule(const UniqueModule&) = delete;
    UniqueModule& operator=(const UniqueModule&) = delete;

    [[nodiscard]] HMODULE get() const noexcept { return value_; }
    [[nodiscard]] HMODULE release() noexcept {
        HMODULE value = value_;
        value_ = nullptr;
        return value;
    }

private:
    HMODULE value_;
};

std::wstring SiblingLibraryPath() {
    HMODULE self = nullptr;
    if (!::GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                              reinterpret_cast<LPCWSTR>(const_cast<const int*>(&kModuleAnchor)), &self)) {
        return {};
    }

    const std::wstring directory = ModuleDirectory(self);
    if (directory.empty()) {
        return {};
    }
    return directory + L"\\vkey_engine.dll";
}

bool SameFile(HANDLE expected, HMODULE module) {
    const std::wstring loadedPath = ModulePath(module);
    if (loadedPath.empty()) {
        return false;
    }

    UniqueFile loaded(::CreateFileW(loadedPath.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                                    FILE_ATTRIBUTE_NORMAL, nullptr));
    if (!loaded.valid()) {
        return false;
    }

    BY_HANDLE_FILE_INFORMATION expectedInfo{};
    BY_HANDLE_FILE_INFORMATION loadedInfo{};
    return ::GetFileInformationByHandle(expected, &expectedInfo) &&
           ::GetFileInformationByHandle(loaded.get(), &loadedInfo) &&
           expectedInfo.dwVolumeSerialNumber == loadedInfo.dwVolumeSerialNumber &&
           expectedInfo.nFileIndexHigh == loadedInfo.nFileIndexHigh &&
           expectedInfo.nFileIndexLow == loadedInfo.nFileIndexLow;
}

#else

std::string SiblingLibraryPath() {
    Dl_info info{};
    if (::dladdr(const_cast<const int*>(&kModuleAnchor), &info) == 0 || !info.dli_fname) {
        return {};
    }

    std::string path(info.dli_fname);
    const size_t slash = path.find_last_of('/');
    if (slash == std::string::npos) {
        return {};
    }
    path.resize(slash + 1);
    path += "libvkey_engine.so";
    return path;
}

#endif

} // namespace

RustEngineLibraryResult LoadRustEngineLibrary() {
    try {
#if defined(_WIN32)
        const std::wstring path = SiblingLibraryPath();
        if (path.empty()) {
            return {nullptr, L"cannot resolve the module-relative engine path"};
        }

        // Denying write/delete sharing closes the verify-to-load replacement
        // window.
        UniqueFile file(::CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                                      FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, nullptr));
        if (!file.valid()) {
            return {nullptr, L"vkey_engine.dll is not installed next to the host module"};
        }

        const std::wstring signaturePath = path + L".sig";
        const RustEngineTrustStatus trust =
            VerifyRustEngineFileHandle(file.get(), signaturePath.c_str());
        if (trust != RustEngineTrustStatus::Trusted) {
            return {nullptr, RustEngineTrustReason(trust)};
        }

        UniqueModule module(
            ::LoadLibraryExW(path.c_str(), nullptr, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32));
        if (!module.get()) {
            return {nullptr, L"the verified vkey_engine.dll could not be loaded safely"};
        }
        if (!SameFile(file.get(), module.get())) {
            return {nullptr, L"Windows loaded a different vkey_engine.dll than the verified file"};
        }

        return {static_cast<void*>(module.release()), {}};
#else
        const std::string path = SiblingLibraryPath();
        if (path.empty()) {
            return {nullptr, L"cannot resolve the module-relative engine path"};
        }
        // No hash/identity gate here: Linux is a test-only target for this
        // engine, and the trust path (CNG, LoadLibraryExW, FILE_RENAME_INFO) is
        // Windows-only. Shipping Linux means porting the checks first.
        if (void* module = ::dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL)) {
            return {module, {}};
        }
        return {nullptr, L"libvkey_engine.so is not installed next to the host module"};
#endif
    } catch (...) {
        return {nullptr, L"vkey_engine loader initialization failed"};
    }
}

void CloseRustEngineLibrary(void* handle) noexcept {
    if (!handle) {
        return;
    }
#if defined(_WIN32)
    ::FreeLibrary(static_cast<HMODULE>(handle));
#else
    ::dlclose(handle);
#endif
}

} // namespace NextKey
