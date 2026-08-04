// VKey - trusted Rust engine loader
// SPDX-License-Identifier: AGPL-3.0-only

#pragma once

#include <string>

namespace NextKey {

struct RustEngineLibraryResult {
    void* handle = nullptr;
    std::wstring reason;
};

[[nodiscard]] RustEngineLibraryResult LoadRustEngineLibrary();
void CloseRustEngineLibrary(void* handle) noexcept;

}  // namespace NextKey
