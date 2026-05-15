// HookLogParser.h -- read VKey_hook.log and extract per-keystroke timing.
//
// VKey writes one line per LL hook callback in DEBUG / NEXTKEY_DEBUG
// builds (see HookEngine.cpp HOOK_LOG macro). Format:
//
//   [HH:MM:SS.mmm] KEY vk=0xXX scan=0xXXXX flags=0xXXXXXXXX DOWN
//
// The leading bracket-timestamp is `GetLocalTime`-based -- millisecond
// granularity, sub-ms not available without further instrumentation. At our
// p99 hot-path budget of 1 ms this is borderline; meaningful for detecting
// gross stalls (>= 2 ms) but not for sub-ms profiling. Phase 1+ may add
// QueryPerformanceCounter-based markers if finer detail is needed.
//
// Use ParseFileSlice + FileSize together to capture only the entries
// produced by a single test run:
//
//   const auto offset = HookLogParser::FileSize(logPath);
//   ... drive the test ...
//   const auto entries = HookLogParser::ParseFileSlice(logPath, offset);
//   const auto stats   = HookLogParser::ComputeKeyDownStats(entries);

#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string_view>
#include <vector>

namespace NextKey::TestRunner::HookLogParser {

enum class KeyDirection : uint8_t {
    Down,
    Up,
    Other,
};

struct KeystrokeEntry {
    uint64_t timestampMs;       // ms since midnight (GetLocalTime-based)
    uint8_t vkCode;             // Win32 virtual key code
    KeyDirection direction;
};

// Parses a log file's contents (in-memory string) into KEY entries. Lines
// that don't match the expected format are silently skipped.
[[nodiscard]] std::vector<KeystrokeEntry> ParseString(std::string_view content);

// Reads the entire file and parses. Returns empty vector if the file does
// not exist or cannot be opened.
[[nodiscard]] std::vector<KeystrokeEntry> ParseFile(const std::filesystem::path& path);

// Reads from `startOffset` to end of file and parses. Used by the corpus
// runner: take FileSize() before each test, then ParseFileSlice() with that
// offset after, to capture only the entries logged during the test.
[[nodiscard]] std::vector<KeystrokeEntry> ParseFileSlice(
    const std::filesystem::path& path,
    std::uint64_t startOffset);

// Returns the byte size of the file, or 0 if the file does not exist.
[[nodiscard]] std::uint64_t FileSize(const std::filesystem::path& path) noexcept;

struct TimingStats {
    std::size_t intervals = 0;  // number of inter-keystroke deltas (entries-1)
    uint64_t meanMs = 0;
    uint64_t p50Ms = 0;
    uint64_t p95Ms = 0;
    uint64_t p99Ms = 0;
    uint64_t maxMs = 0;
};

// Filters entries to KeyDirection::Down, computes inter-keystroke deltas
// in milliseconds, and returns mean / p50 / p95 / p99 / max. With fewer
// than 2 down events the result is zeroed (intervals == 0).
[[nodiscard]] TimingStats ComputeKeyDownStats(const std::vector<KeystrokeEntry>& entries);

}  // namespace NextKey::TestRunner::HookLogParser
