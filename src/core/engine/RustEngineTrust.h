// VKey - Rust engine artifact trust
// SPDX-License-Identifier: AGPL-3.0-only

#pragma once

#include <cstdint>

namespace NextKey {

enum class RustEngineTrustStatus : std::uint8_t {
    Trusted,
    InvalidHandle,
    SizeMismatch,
    HashFailure,
    HashMismatch,
};

[[nodiscard]] std::uint64_t ExpectedRustEngineByteLength() noexcept;
[[nodiscard]] const wchar_t* RustEngineTrustReason(RustEngineTrustStatus status) noexcept;

#if defined(_WIN32)
[[nodiscard]] RustEngineTrustStatus VerifyRustEngineFileHandle(void* fileHandle) noexcept;
#endif

} // namespace NextKey
