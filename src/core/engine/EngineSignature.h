// VKey - vkey_engine.dll detached signature: layout and floor checks
// Copyright (c) 2024-2026 PhatMT. All rights reserved.
// SPDX-License-Identifier: AGPL-3.0-only

#pragma once

#include <array>
#include <cstdint>
#include <cstring>
#include <optional>
#include <span>

namespace NextKey::EngineSignature {

/// `vkey_engine.dll.sig` as written by VKey-rs `tools/sign_engine.py`:
///
///     abi      u32 little-endian   (4)
///     counter  u32 little-endian   (4)
///     sig      raw r||s, 32+32     (64)
///
/// Fixed width on purpose — a consumer that never parses cannot be attacked
/// through its parser.
inline constexpr size_t kFileBytes = 72;
inline constexpr size_t kRawSignatureBytes = 64;

/// Domain prefix of the signed payload. Without it, a signature over some other
/// VKey artifact could be replayed as an engine signature.
inline constexpr char kDomain[] = "VKEYENG1";
inline constexpr size_t kDomainBytes = 8;  // no terminator on the wire

/// `kDomain || sha256(engine) || abi_le || counter_le`
inline constexpr size_t kPayloadBytes = kDomainBytes + 32 + 4 + 4;

struct Parsed {
    std::uint32_t abi = 0;
    std::uint32_t counter = 0;
    std::array<std::uint8_t, kRawSignatureBytes> signature{};
};

[[nodiscard]] inline std::optional<Parsed> Parse(std::span<const std::uint8_t> file) noexcept {
    if (file.size() != kFileBytes) {
        return std::nullopt;
    }
    Parsed parsed;
    // Little-endian by hand: the writer is Python struct '<II', and the host
    // being little-endian too is not something to rely on silently.
    parsed.abi = static_cast<std::uint32_t>(file[0]) |
                 (static_cast<std::uint32_t>(file[1]) << 8) |
                 (static_cast<std::uint32_t>(file[2]) << 16) |
                 (static_cast<std::uint32_t>(file[3]) << 24);
    parsed.counter = static_cast<std::uint32_t>(file[4]) |
                     (static_cast<std::uint32_t>(file[5]) << 8) |
                     (static_cast<std::uint32_t>(file[6]) << 16) |
                     (static_cast<std::uint32_t>(file[7]) << 24);
    std::memcpy(parsed.signature.data(), file.data() + 8, kRawSignatureBytes);
    return parsed;
}

/// Rebuild what the signer signed, from a digest this process computed itself —
/// never from a length or hash the signature file claims.
[[nodiscard]] inline std::array<std::uint8_t, kPayloadBytes> BuildPayload(
    const std::array<std::uint8_t, 32>& digest, std::uint32_t abi, std::uint32_t counter) noexcept {
    std::array<std::uint8_t, kPayloadBytes> payload{};
    std::memcpy(payload.data(), kDomain, kDomainBytes);
    std::memcpy(payload.data() + kDomainBytes, digest.data(), digest.size());
    const size_t abiAt = kDomainBytes + digest.size();
    for (size_t i = 0; i < 4; ++i) {
        payload[abiAt + i] = static_cast<std::uint8_t>((abi >> (8 * i)) & 0xFF);
        payload[abiAt + 4 + i] = static_cast<std::uint8_t>((counter >> (8 * i)) & 0xFF);
    }
    return payload;
}

/// Accept an engine at or above the build's floor. `>=` on the ABI is the whole
/// point: a consumer built against ABI 6 must still load an ABI 7 engine, which
/// is safe because every symbol it needs is resolved by name at load time and
/// the engine's ABI contract is additive.
[[nodiscard]] inline bool MeetsFloor(const Parsed& parsed, std::uint32_t abiFloor,
                                     std::uint32_t counterFloor) noexcept {
    return parsed.abi >= abiFloor && parsed.counter >= counterFloor;
}

}  // namespace NextKey::EngineSignature
