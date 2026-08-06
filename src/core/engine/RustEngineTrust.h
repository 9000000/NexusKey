// VKey - Rust engine artifact trust
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <cstdint>

namespace NextKey {

enum class RustEngineTrustStatus : std::uint8_t {
    Trusted,
    InvalidHandle,
    SizeMismatch,
    HashFailure,
    HashMismatch,
    SignatureMissing,
    SignatureMalformed,
    SignatureBelowFloor,
    SignatureInvalid,
};

[[nodiscard]] std::uint64_t ExpectedRustEngineByteLength() noexcept;
[[nodiscard]] const wchar_t* RustEngineTrustReason(RustEngineTrustStatus status) noexcept;

#if defined(_WIN32)
/// Decide whether the opened vkey_engine.dll may be loaded.
///
/// Release trusts the detached `vkey_engine.dll.sig` beside it: signed by the
/// project key, ABI and release counter at or above this build's floor. That is
/// what lets a VKeyTSF.dll left behind by a deferred update load the newer engine
/// installed next to it instead of silently dropping to the C++ engine.
///
/// Debug additionally accepts an exact match against the committed engine.lock
/// hash, because a locally synced engine has no signature — the release workflow
/// holds the key. That path is compiled out of Release, so no machine running a
/// shipped build has a way around the signature.
///
/// `signaturePath` is a wide, NUL-terminated path; nullptr means "no signature".
[[nodiscard]] RustEngineTrustStatus VerifyRustEngineFileHandle(void* fileHandle,
                                                               const wchar_t* signaturePath) noexcept;
#endif

} // namespace NextKey
