// VKey - Rust engine artifact trust
// SPDX-License-Identifier: AGPL-3.0-only

#include "RustEngineTrust.h"

#include "VKeyEngineLock.h"

#include <array>
#include <cstdint>
#include <string_view>
#include <vector>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <bcrypt.h>
#endif

namespace NextKey {
namespace {

#if defined(_WIN32)

class UniqueAlgorithm final {
public:
    ~UniqueAlgorithm() {
        if (value) {
            ::BCryptCloseAlgorithmProvider(value, 0);
        }
    }

    UniqueAlgorithm() = default;
    UniqueAlgorithm(const UniqueAlgorithm&) = delete;
    UniqueAlgorithm& operator=(const UniqueAlgorithm&) = delete;

    BCRYPT_ALG_HANDLE value = nullptr;
};

class UniqueHash final {
public:
    ~UniqueHash() {
        if (value) {
            ::BCryptDestroyHash(value);
        }
    }

    UniqueHash() = default;
    UniqueHash(const UniqueHash&) = delete;
    UniqueHash& operator=(const UniqueHash&) = delete;

    BCRYPT_HASH_HANDLE value = nullptr;
};

bool HashFile(HANDLE file, std::array<std::uint8_t, 32>& digest) {
    LARGE_INTEGER start{};
    if (!::SetFilePointerEx(file, start, nullptr, FILE_BEGIN)) {
        return false;
    }

    UniqueAlgorithm algorithm;
    if (!BCRYPT_SUCCESS(::BCryptOpenAlgorithmProvider(&algorithm.value, BCRYPT_SHA256_ALGORITHM, nullptr, 0))) {
        return false;
    }

    DWORD objectBytes = 0;
    DWORD resultBytes = 0;
    if (!BCRYPT_SUCCESS(::BCryptGetProperty(algorithm.value, BCRYPT_OBJECT_LENGTH,
                                            reinterpret_cast<PUCHAR>(&objectBytes), sizeof(objectBytes), &resultBytes,
                                            0))) {
        return false;
    }

    DWORD digestBytes = 0;
    if (!BCRYPT_SUCCESS(::BCryptGetProperty(algorithm.value, BCRYPT_HASH_LENGTH, reinterpret_cast<PUCHAR>(&digestBytes),
                                            sizeof(digestBytes), &resultBytes, 0)) ||
        digestBytes != static_cast<DWORD>(digest.size())) {
        return false;
    }

    std::vector<std::uint8_t> object(objectBytes);
    UniqueHash hash;
    if (!BCRYPT_SUCCESS(::BCryptCreateHash(algorithm.value, &hash.value, object.data(), objectBytes, nullptr, 0, 0))) {
        return false;
    }

    std::array<std::uint8_t, 64 * 1024> buffer{};
    for (;;) {
        DWORD bytesRead = 0;
        if (!::ReadFile(file, buffer.data(), static_cast<DWORD>(buffer.size()), &bytesRead, nullptr)) {
            return false;
        }
        if (bytesRead == 0) {
            break;
        }
        if (!BCRYPT_SUCCESS(::BCryptHashData(hash.value, buffer.data(), bytesRead, 0))) {
            return false;
        }
    }

    return BCRYPT_SUCCESS(::BCryptFinishHash(hash.value, digest.data(), static_cast<ULONG>(digest.size()), 0));
}

int HexValue(char value) noexcept {
    if (value >= '0' && value <= '9') {
        return value - '0';
    }
    if (value >= 'a' && value <= 'f') {
        return value - 'a' + 10;
    }
    return -1;
}

bool HashMatches(const std::array<std::uint8_t, 32>& digest) noexcept {
    constexpr std::string_view expected(VKeyEngineLock::kSha256);
    if (expected.size() != digest.size() * 2) {
        return false;
    }

    std::uint8_t difference = 0;
    for (size_t i = 0; i < digest.size(); ++i) {
        const int high = HexValue(expected[i * 2]);
        const int low = HexValue(expected[i * 2 + 1]);
        if (high < 0 || low < 0) {
            return false;
        }
        difference |= static_cast<std::uint8_t>(digest[i] ^ static_cast<std::uint8_t>((high << 4) | low));
    }
    return difference == 0;
}

#endif

} // namespace

std::uint64_t ExpectedRustEngineByteLength() noexcept { return VKeyEngineLock::kByteLength; }

const wchar_t* RustEngineTrustReason(RustEngineTrustStatus status) noexcept {
    switch (status) {
    case RustEngineTrustStatus::Trusted:
        return L"";
    case RustEngineTrustStatus::InvalidHandle:
        return L"could not open vkey_engine.dll for verification";
    case RustEngineTrustStatus::SizeMismatch:
        return L"vkey_engine.dll size does not match the trusted build";
    case RustEngineTrustStatus::HashFailure:
        return L"could not hash vkey_engine.dll";
    case RustEngineTrustStatus::HashMismatch:
        return L"vkey_engine.dll SHA-256 does not match the trusted build";
    }
    return L"vkey_engine.dll trust status is invalid";
}

#if defined(_WIN32)

RustEngineTrustStatus VerifyRustEngineFileHandle(void* fileHandle) noexcept {
    try {
        const HANDLE file = static_cast<HANDLE>(fileHandle);
        if (!file || file == INVALID_HANDLE_VALUE) {
            return RustEngineTrustStatus::InvalidHandle;
        }

        LARGE_INTEGER byteLength{};
        if (!::GetFileSizeEx(file, &byteLength) || byteLength.QuadPart < 0 ||
            static_cast<std::uint64_t>(byteLength.QuadPart) != VKeyEngineLock::kByteLength) {
            return RustEngineTrustStatus::SizeMismatch;
        }

        std::array<std::uint8_t, 32> digest{};
        if (!HashFile(file, digest)) {
            return RustEngineTrustStatus::HashFailure;
        }
        return HashMatches(digest) ? RustEngineTrustStatus::Trusted : RustEngineTrustStatus::HashMismatch;
    } catch (...) {
        return RustEngineTrustStatus::HashFailure;
    }
}

#endif

} // namespace NextKey
