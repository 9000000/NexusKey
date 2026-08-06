// VKey - Rust engine artifact trust
// SPDX-License-Identifier: GPL-3.0-only

#include "RustEngineTrust.h"

#include "EngineSignature.h"
#include "VKeyEngineLock.h"

#include <array>
#include <cstdint>
#include <string_view>
#include <vector>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <bcrypt.h>

#include "core/WinFileSystem.h"  // UniqueFile, shared with the loader
#endif

namespace NextKey {
namespace {

// Hashing is no longer bounded by the lock — a signed engine may be any size —
// so the ceiling has to come from somewhere. Same 8 MiB the release tooling caps
// the artifact at.
constexpr long long kMaxEngineBytes = 8LL * 1024 * 1024;

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

class UniqueKey final {
public:
    explicit UniqueKey(BCRYPT_KEY_HANDLE value = nullptr) noexcept : value_(value) {}
    ~UniqueKey() {
        if (value_) {
            ::BCryptDestroyKey(value_);
        }
    }

    UniqueKey(const UniqueKey&) = delete;
    UniqueKey& operator=(const UniqueKey&) = delete;

    [[nodiscard]] BCRYPT_KEY_HANDLE get() const noexcept { return value_; }

private:
    BCRYPT_KEY_HANDLE value_;
};

bool HashBuffer(const std::uint8_t* data, size_t length, std::array<std::uint8_t, 32>& digest) {
    UniqueAlgorithm algorithm;
    if (!BCRYPT_SUCCESS(::BCryptOpenAlgorithmProvider(&algorithm.value, BCRYPT_SHA256_ALGORITHM, nullptr, 0))) {
        return false;
    }

    DWORD objectBytes = 0;
    DWORD resultBytes = 0;
    if (!BCRYPT_SUCCESS(::BCryptGetProperty(algorithm.value, BCRYPT_OBJECT_LENGTH,
                                            reinterpret_cast<PUCHAR>(&objectBytes), sizeof(objectBytes),
                                            &resultBytes, 0))) {
        return false;
    }

    std::vector<std::uint8_t> object(objectBytes);
    UniqueHash hash;
    if (!BCRYPT_SUCCESS(::BCryptCreateHash(algorithm.value, &hash.value, object.data(), objectBytes, nullptr, 0, 0))) {
        return false;
    }
    if (!BCRYPT_SUCCESS(::BCryptHashData(hash.value, const_cast<PUCHAR>(data), static_cast<ULONG>(length), 0))) {
        return false;
    }
    return BCRYPT_SUCCESS(::BCryptFinishHash(hash.value, digest.data(), static_cast<ULONG>(digest.size()), 0));
}

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

int HexValue(char value) noexcept;

/// Decode the baked X||Y hex into the BCRYPT_ECCKEY_BLOB that
/// BCryptImportKeyPair expects: header, then X, then Y.
bool BuildPublicKeyBlob(std::vector<std::uint8_t>& blob) noexcept {
    constexpr std::string_view hex(VKeyEngineLock::kEnginePublicKey);
    if (hex.size() != 128) {
        return false;
    }
    blob.resize(sizeof(BCRYPT_ECCKEY_BLOB) + 64);
    auto* header = reinterpret_cast<BCRYPT_ECCKEY_BLOB*>(blob.data());
    header->dwMagic = BCRYPT_ECDSA_PUBLIC_P256_MAGIC;
    header->cbKey = 32;
    std::uint8_t* coordinates = blob.data() + sizeof(BCRYPT_ECCKEY_BLOB);
    for (size_t i = 0; i < 64; ++i) {
        const int high = HexValue(hex[i * 2]);
        const int low = HexValue(hex[i * 2 + 1]);
        if (high < 0 || low < 0) {
            return false;
        }
        coordinates[i] = static_cast<std::uint8_t>((high << 4) | low);
    }
    return true;
}

/// Read the detached signature. Anything but exactly 72 bytes is refused before
/// a single byte is interpreted.
bool ReadSignatureFile(const wchar_t* path, std::array<std::uint8_t, EngineSignature::kFileBytes>& out) noexcept {
    UniqueFile file(::CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                                  FILE_ATTRIBUTE_NORMAL, nullptr));
    if (!file.valid()) {
        return false;
    }
    LARGE_INTEGER size{};
    if (!::GetFileSizeEx(file.get(), &size) ||
        size.QuadPart != static_cast<LONGLONG>(out.size())) {
        return false;
    }
    DWORD read = 0;
    return ::ReadFile(file.get(), out.data(), static_cast<DWORD>(out.size()), &read, nullptr) &&
           read == out.size();
}

/// ECDSA-P256/SHA-256 over the payload the signer built, using the digest this
/// process computed from the open handle — so the file cannot change between
/// being verified and being loaded.
bool SignatureVerifies(const std::array<std::uint8_t, 32>& digest,
                       const EngineSignature::Parsed& parsed) noexcept {
    UniqueAlgorithm algorithm;
    if (!BCRYPT_SUCCESS(::BCryptOpenAlgorithmProvider(&algorithm.value, BCRYPT_ECDSA_P256_ALGORITHM,
                                                      nullptr, 0))) {
        return false;
    }

    std::vector<std::uint8_t> blob;
    if (!BuildPublicKeyBlob(blob)) {
        return false;
    }

    BCRYPT_KEY_HANDLE rawKey = nullptr;
    if (!BCRYPT_SUCCESS(::BCryptImportKeyPair(algorithm.value, nullptr, BCRYPT_ECCPUBLIC_BLOB, &rawKey,
                                              blob.data(), static_cast<ULONG>(blob.size()), 0))) {
        return false;
    }
    UniqueKey key(rawKey);

    const auto payload = EngineSignature::BuildPayload(digest, parsed.abi, parsed.counter);
    std::array<std::uint8_t, 32> payloadDigest{};
    if (!HashBuffer(payload.data(), payload.size(), payloadDigest)) {
        return false;
    }

    return BCRYPT_SUCCESS(::BCryptVerifySignature(
        key.get(), nullptr, payloadDigest.data(), static_cast<ULONG>(payloadDigest.size()),
        const_cast<PUCHAR>(parsed.signature.data()), static_cast<ULONG>(parsed.signature.size()), 0));
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
    case RustEngineTrustStatus::SignatureMissing:
        return L"vkey_engine.dll.sig is missing or unreadable";
    case RustEngineTrustStatus::SignatureMalformed:
        return L"vkey_engine.dll.sig is not a 72-byte engine signature";
    case RustEngineTrustStatus::SignatureBelowFloor:
        return L"vkey_engine.dll is older than the release this build requires";
    case RustEngineTrustStatus::SignatureInvalid:
        return L"vkey_engine.dll.sig was not produced by the VKey engine key";
    }
    return L"vkey_engine.dll trust status is invalid";
}

#if defined(_WIN32)

RustEngineTrustStatus VerifyRustEngineFileHandle(void* fileHandle,
                                                 const wchar_t* signaturePath) noexcept {
    try {
        const HANDLE file = static_cast<HANDLE>(fileHandle);
        if (!file || file == INVALID_HANDLE_VALUE) {
            return RustEngineTrustStatus::InvalidHandle;
        }

        // No size gate before hashing any more: the whole point of the signature
        // is that this build accepts engines it has never seen, which have a
        // different length. The 8 MiB ceiling below is what keeps hashing bounded.
        LARGE_INTEGER byteLength{};
        if (!::GetFileSizeEx(file, &byteLength) || byteLength.QuadPart <= 0 ||
            byteLength.QuadPart > kMaxEngineBytes) {
            return RustEngineTrustStatus::SizeMismatch;
        }

        std::array<std::uint8_t, 32> digest{};
        if (!HashFile(file, digest)) {
            return RustEngineTrustStatus::HashFailure;
        }

#ifdef VKEY_ENGINE_TRUST_DEV
        // Debug only: a locally synced engine carries no signature, because the
        // release workflow holds the key. Compiled out of Release entirely.
        if (static_cast<std::uint64_t>(byteLength.QuadPart) == VKeyEngineLock::kByteLength &&
            HashMatches(digest)) {
            return RustEngineTrustStatus::Trusted;
        }
#endif

        if (!signaturePath) {
            return RustEngineTrustStatus::SignatureMissing;
        }
        std::array<std::uint8_t, EngineSignature::kFileBytes> raw{};
        if (!ReadSignatureFile(signaturePath, raw)) {
            return RustEngineTrustStatus::SignatureMissing;
        }
        const auto parsed = EngineSignature::Parse(raw);
        if (!parsed) {
            return RustEngineTrustStatus::SignatureMalformed;
        }
        if (!EngineSignature::MeetsFloor(*parsed, VKeyEngineLock::kAbiVersion,
                                         VKeyEngineLock::kCounterFloor)) {
            return RustEngineTrustStatus::SignatureBelowFloor;
        }
        return SignatureVerifies(digest, *parsed) ? RustEngineTrustStatus::Trusted
                                                  : RustEngineTrustStatus::SignatureInvalid;
    } catch (...) {
        return RustEngineTrustStatus::HashFailure;
    }
}

#endif

} // namespace NextKey
