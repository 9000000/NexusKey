// VKey - engine signature layout and floor tests
// SPDX-License-Identifier: GPL-3.0-only

#include <gtest/gtest.h>

#include "core/engine/EngineSignature.h"

#include <array>
#include <cstdint>
#include <numeric>
#include <vector>

namespace NextKey {
namespace {

using EngineSignature::kFileBytes;

std::vector<std::uint8_t> MakeFile(std::uint32_t abi, std::uint32_t counter,
                                   std::uint8_t signatureFill = 0xAB) {
    std::vector<std::uint8_t> file(kFileBytes, signatureFill);
    for (size_t i = 0; i < 4; ++i) {
        file[i] = static_cast<std::uint8_t>((abi >> (8 * i)) & 0xFF);
        file[4 + i] = static_cast<std::uint8_t>((counter >> (8 * i)) & 0xFF);
    }
    return file;
}

TEST(EngineSignatureTest, ParsesTheLittleEndianHeader) {
    const auto file = MakeFile(6, 1'002'003);
    const auto parsed = EngineSignature::Parse(file);
    ASSERT_TRUE(parsed.has_value());
    EXPECT_EQ(parsed->abi, 6u);
    EXPECT_EQ(parsed->counter, 1'002'003u);
    EXPECT_EQ(parsed->signature.size(), EngineSignature::kRawSignatureBytes);
    EXPECT_EQ(parsed->signature[0], 0xAB);
}

TEST(EngineSignatureTest, RejectsAnyLengthButSeventyTwo) {
    for (size_t length : {size_t{0}, kFileBytes - 1, kFileBytes + 1, size_t{8}}) {
        std::vector<std::uint8_t> file(length, 0x01);
        EXPECT_FALSE(EngineSignature::Parse(file).has_value()) << "length " << length;
    }
}

// The reason the whole design exists: a consumer built against an older release
// must accept a newer engine, or a deferred update silently costs the user the
// Rust engine in every TSF app.
TEST(EngineSignatureTest, AcceptsANewerEngineThanThisBuild) {
    const auto parsed = EngineSignature::Parse(MakeFile(7, 1'000'005));
    ASSERT_TRUE(parsed.has_value());
    EXPECT_TRUE(EngineSignature::MeetsFloor(*parsed, /*abiFloor=*/6, /*counterFloor=*/1'000'000));
}

TEST(EngineSignatureTest, AcceptsExactlyTheFloor) {
    const auto parsed = EngineSignature::Parse(MakeFile(6, 1'000'000));
    ASSERT_TRUE(parsed.has_value());
    EXPECT_TRUE(EngineSignature::MeetsFloor(*parsed, 6, 1'000'000));
}

TEST(EngineSignatureTest, RejectsADowngradeBelowTheCounterFloor) {
    const auto parsed = EngineSignature::Parse(MakeFile(6, 999'999));
    ASSERT_TRUE(parsed.has_value());
    EXPECT_FALSE(EngineSignature::MeetsFloor(*parsed, 6, 1'000'000));
}

TEST(EngineSignatureTest, RejectsAnEngineOlderThanTheAbiFloor) {
    const auto parsed = EngineSignature::Parse(MakeFile(5, 1'000'005));
    ASSERT_TRUE(parsed.has_value());
    EXPECT_FALSE(EngineSignature::MeetsFloor(*parsed, 6, 1'000'000));
}

TEST(EngineSignatureTest, PayloadIsDomainSeparatedAndCarriesTheDigest) {
    std::array<std::uint8_t, 32> digest{};
    std::iota(digest.begin(), digest.end(), static_cast<std::uint8_t>(1));

    const auto payload = EngineSignature::BuildPayload(digest, 6, 1'000'002);
    ASSERT_EQ(payload.size(), EngineSignature::kPayloadBytes);
    EXPECT_EQ(std::memcmp(payload.data(), "VKEYENG1", 8), 0);
    EXPECT_EQ(std::memcmp(payload.data() + 8, digest.data(), digest.size()), 0);
    // abi then counter, little-endian, right after the digest.
    EXPECT_EQ(payload[40], 6);
    EXPECT_EQ(payload[41], 0);
    EXPECT_EQ(payload[44], static_cast<std::uint8_t>(1'000'002u & 0xFF));
    EXPECT_EQ(payload[45], static_cast<std::uint8_t>((1'000'002u >> 8) & 0xFF));
}

// Signing covers abi and counter, so editing either in the file changes what a
// verifier reconstructs — which is what makes the downgrade floor unforgeable.
TEST(EngineSignatureTest, PayloadChangesWithAbiAndCounter) {
    std::array<std::uint8_t, 32> digest{};
    const auto base = EngineSignature::BuildPayload(digest, 6, 1'000'002);
    EXPECT_NE(base, EngineSignature::BuildPayload(digest, 7, 1'000'002));
    EXPECT_NE(base, EngineSignature::BuildPayload(digest, 6, 1'000'003));

    std::array<std::uint8_t, 32> other{};
    other[31] = 1;
    EXPECT_NE(base, EngineSignature::BuildPayload(other, 6, 1'000'002));
}

}  // namespace
}  // namespace NextKey
