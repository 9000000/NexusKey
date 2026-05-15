#include <gtest/gtest.h>

#include <string>

#include "HookLogParser.h"

namespace NextKey::TestRunner::Test {

using HookLogParser::ComputeKeyDownStats;
using HookLogParser::KeyDirection;
using HookLogParser::KeystrokeEntry;
using HookLogParser::ParseString;

TEST(HookLogParserTest, EmptyContentReturnsEmpty) {
    EXPECT_TRUE(ParseString("").empty());
}

TEST(HookLogParserTest, NoKeyLinesReturnsEmpty) {
    constexpr std::string_view kLog =
        "[12:34:56.789] === HookEngine::Start ===\n"
        "[12:34:56.790] Hook installed OK (method=1, vietnamese=1)\n";
    EXPECT_TRUE(ParseString(kLog).empty());
}

TEST(HookLogParserTest, ParsesSingleKeyDown) {
    constexpr std::string_view kLog =
        "[12:34:56.789] KEY vk=0x48 scan=0x0023 flags=0x00000000 DOWN\n";
    const auto entries = ParseString(kLog);
    ASSERT_EQ(entries.size(), 1u);
    EXPECT_EQ(entries[0].timestampMs,
              static_cast<uint64_t>(12 * 3'600'000 + 34 * 60'000 + 56 * 1'000 + 789));
    EXPECT_EQ(entries[0].vkCode, 0x48);
    EXPECT_EQ(entries[0].direction, KeyDirection::Down);
}

TEST(HookLogParserTest, ParsesDownAndUp) {
    constexpr std::string_view kLog =
        "[00:00:01.000] KEY vk=0x48 scan=0x0023 flags=0x00000000 DOWN\n"
        "[00:00:01.005] KEY vk=0x48 scan=0x0023 flags=0x80000000 UP\n";
    const auto entries = ParseString(kLog);
    ASSERT_EQ(entries.size(), 2u);
    EXPECT_EQ(entries[0].direction, KeyDirection::Down);
    EXPECT_EQ(entries[1].direction, KeyDirection::Up);
    EXPECT_EQ(entries[1].timestampMs - entries[0].timestampMs, 5u);
}

TEST(HookLogParserTest, HandlesUtf8Bom) {
    const std::string log =
        std::string("\xEF\xBB\xBF") +
        "[00:00:00.001] KEY vk=0x41 scan=0x001E flags=0x00000000 DOWN\n";
    const auto entries = ParseString(log);
    ASSERT_EQ(entries.size(), 1u);
    EXPECT_EQ(entries[0].vkCode, 0x41);
}

TEST(HookLogParserTest, HandlesCrlfLineEndings) {
    constexpr std::string_view kLog =
        "[00:00:00.001] KEY vk=0x41 scan=0x001E flags=0x00000000 DOWN\r\n"
        "[00:00:00.002] KEY vk=0x42 scan=0x0030 flags=0x00000000 DOWN\r\n";
    const auto entries = ParseString(kLog);
    ASSERT_EQ(entries.size(), 2u);
    EXPECT_EQ(entries[0].vkCode, 0x41);
    EXPECT_EQ(entries[1].vkCode, 0x42);
}

TEST(HookLogParserTest, IgnoresMalformedLines) {
    constexpr std::string_view kLog =
        "garbage line without timestamp\n"
        "[12:34:56.789] KEY vk=0xZZ scan=invalid\n"           // bad hex
        "[99:99:99.999] KEY vk=0x41 scan=0x001E flags=0 DOWN\n"  // bad time fields
        "[00:00:01.000] KEY vk=0x42 scan=0x0030 flags=0x0 DOWN\n";  // valid
    const auto entries = ParseString(kLog);
    ASSERT_EQ(entries.size(), 1u);
    EXPECT_EQ(entries[0].vkCode, 0x42);
}

TEST(HookLogParserTest, MixedKeyAndNonKeyLines) {
    constexpr std::string_view kLog =
        "[00:00:00.500] === HookEngine::Start ===\n"
        "[00:00:01.000] KEY vk=0x41 scan=0x001E flags=0x0 DOWN\n"
        "[00:00:01.001]   commit-undo: cancel -- modifier key held\n"
        "[00:00:01.002] KEY vk=0x42 scan=0x0030 flags=0x0 DOWN\n";
    const auto entries = ParseString(kLog);
    ASSERT_EQ(entries.size(), 2u);
}

TEST(HookLogParserTest, ComputeStatsEmpty) {
    const auto stats = ComputeKeyDownStats({});
    EXPECT_EQ(stats.intervals, 0u);
    EXPECT_EQ(stats.meanMs, 0u);
    EXPECT_EQ(stats.maxMs, 0u);
}

TEST(HookLogParserTest, ComputeStatsSingleEntryHasZeroIntervals) {
    std::vector<KeystrokeEntry> entries{{1000, 0x41, KeyDirection::Down}};
    const auto stats = ComputeKeyDownStats(entries);
    EXPECT_EQ(stats.intervals, 0u);
}

TEST(HookLogParserTest, ComputeStatsBasicDeltas) {
    // Three down entries at t=1000, 1010, 1015 -> deltas 10, 5
    std::vector<KeystrokeEntry> entries{
        {1000, 0x41, KeyDirection::Down},
        {1010, 0x42, KeyDirection::Down},
        {1015, 0x43, KeyDirection::Down},
    };
    const auto stats = ComputeKeyDownStats(entries);
    EXPECT_EQ(stats.intervals, 2u);
    EXPECT_EQ(stats.meanMs, 7u);    // (10 + 5) / 2 = 7 (integer)
    EXPECT_EQ(stats.maxMs, 10u);
    EXPECT_EQ(stats.p50Ms, 10u);    // sorted [5, 10], pct(0.5) -> idx 1 -> 10
    EXPECT_EQ(stats.p99Ms, 10u);
}

TEST(HookLogParserTest, ComputeStatsSkipsKeyUpEntries) {
    // Only DOWN events count -- UPs interleaved should be ignored.
    std::vector<KeystrokeEntry> entries{
        {1000, 0x41, KeyDirection::Down},
        {1003, 0x41, KeyDirection::Up},     // skipped
        {1010, 0x42, KeyDirection::Down},
        {1013, 0x42, KeyDirection::Up},     // skipped
    };
    const auto stats = ComputeKeyDownStats(entries);
    EXPECT_EQ(stats.intervals, 1u);
    EXPECT_EQ(stats.meanMs, 10u);   // only down-to-down delta
}

TEST(HookLogParserTest, ComputeStatsClampsBackwardsTimestamps) {
    // If timestamp goes backwards (e.g. midnight rollover), clamp to 0
    // rather than producing a huge unsigned wrap-around.
    std::vector<KeystrokeEntry> entries{
        {86'399'000, 0x41, KeyDirection::Down},
        {1'000, 0x42, KeyDirection::Down},   // backwards
    };
    const auto stats = ComputeKeyDownStats(entries);
    EXPECT_EQ(stats.intervals, 1u);
    EXPECT_EQ(stats.meanMs, 0u);
}

}  // namespace NextKey::TestRunner::Test
