#include "HookLogParser.h"

#include <algorithm>
#include <fstream>
#include <numeric>
#include <string>
#include <system_error>

namespace NextKey::TestRunner::HookLogParser {

namespace {

// Skip leading UTF-8 BOM (EF BB BF) if present. NexusKey logs are written
// with `ccs=UTF-8` which prefixes a BOM.
constexpr std::string_view kUtf8Bom = "\xEF\xBB\xBF";

// Returns -1 if not an ASCII digit, else the digit value.
constexpr int Digit(char ch) noexcept {
    return (ch >= '0' && ch <= '9') ? (ch - '0') : -1;
}

// Returns -1 if not an ASCII hex digit, else its 0..15 value.
constexpr int HexDigit(char ch) noexcept {
    if (ch >= '0' && ch <= '9') return ch - '0';
    if (ch >= 'a' && ch <= 'f') return ch - 'a' + 10;
    if (ch >= 'A' && ch <= 'F') return ch - 'A' + 10;
    return -1;
}

// Parses a line of the form:
//   [HH:MM:SS.mmm] KEY vk=0xXX scan=0xXXXX flags=0xXXXXXXXX DOWN
// On success fills `out` and returns true; on any mismatch returns false
// (caller drops the line silently). Uses manual digit parsing so the same
// code compiles cleanly on MSVC /WX (sscanf is C4996).
bool ParseLine(std::string_view line, KeystrokeEntry& out) {
    // Bracket prefix is exactly 14 chars: "[HH:MM:SS.mmm]".
    //                                       0  3  6  9  13
    if (line.size() < 30) return false;
    if (line[0]  != '[' || line[3]  != ':' || line[6]  != ':' ||
        line[9]  != '.' || line[13] != ']') return false;

    const int h1 = Digit(line[1]),  h2 = Digit(line[2]);
    const int m1 = Digit(line[4]),  m2 = Digit(line[5]);
    const int s1 = Digit(line[7]),  s2 = Digit(line[8]);
    const int x1 = Digit(line[10]), x2 = Digit(line[11]), x3 = Digit(line[12]);
    if ((h1 | h2 | m1 | m2 | s1 | s2 | x1 | x2 | x3) < 0) return false;

    const unsigned hh = static_cast<unsigned>(h1) * 10 + static_cast<unsigned>(h2);
    const unsigned mm = static_cast<unsigned>(m1) * 10 + static_cast<unsigned>(m2);
    const unsigned ss = static_cast<unsigned>(s1) * 10 + static_cast<unsigned>(s2);
    const unsigned ms = static_cast<unsigned>(x1) * 100 +
                        static_cast<unsigned>(x2) * 10 +
                        static_cast<unsigned>(x3);
    if (hh > 23 || mm > 59 || ss > 59) return false;

    // After "] " look for "KEY vk=0x" marker.
    const std::string_view rest = line.substr(14);
    const std::size_t keyPos = rest.find("KEY vk=0x");
    if (keyPos == std::string_view::npos) return false;

    // 2-hex-digit VK code after "KEY vk=0x".
    constexpr std::size_t kVkPrefixLen = 9;  // strlen("KEY vk=0x")
    if (keyPos + kVkPrefixLen + 2 > rest.size()) return false;
    const int vkHi = HexDigit(rest[keyPos + kVkPrefixLen]);
    const int vkLo = HexDigit(rest[keyPos + kVkPrefixLen + 1]);
    if (vkHi < 0 || vkLo < 0) return false;

    KeyDirection dir = KeyDirection::Other;
    if (line.size() >= 4 && line.compare(line.size() - 4, 4, "DOWN") == 0) {
        dir = KeyDirection::Down;
    } else if (line.size() >= 2 && line.compare(line.size() - 2, 2, "UP") == 0) {
        dir = KeyDirection::Up;
    }

    out.timestampMs =
        static_cast<uint64_t>(hh) * 3'600'000ULL +
        static_cast<uint64_t>(mm) * 60'000ULL +
        static_cast<uint64_t>(ss) * 1'000ULL +
        static_cast<uint64_t>(ms);
    out.vkCode = static_cast<uint8_t>((vkHi << 4) | vkLo);
    out.direction = dir;
    return true;
}

}  // namespace

std::vector<KeystrokeEntry> ParseString(std::string_view content) {
    if (content.starts_with(kUtf8Bom)) {
        content.remove_prefix(kUtf8Bom.size());
    }

    std::vector<KeystrokeEntry> out;
    std::size_t lineStart = 0;
    for (std::size_t i = 0; i <= content.size(); ++i) {
        const bool atEnd = (i == content.size());
        const bool atNewline = !atEnd && content[i] == '\n';
        if (!atEnd && !atNewline) continue;

        std::string_view line = content.substr(lineStart, i - lineStart);
        // Strip trailing CR (Windows CRLF endings).
        if (!line.empty() && line.back() == '\r') {
            line.remove_suffix(1);
        }

        KeystrokeEntry entry{};
        if (ParseLine(line, entry)) {
            out.push_back(entry);
        }
        lineStart = i + 1;
    }
    return out;
}

std::vector<KeystrokeEntry> ParseFile(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return {};
    std::string content(std::istreambuf_iterator<char>(in), {});
    return ParseString(content);
}

std::vector<KeystrokeEntry> ParseFileSlice(
    const std::filesystem::path& path, std::uint64_t startOffset) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return {};

    in.seekg(0, std::ios::end);
    const auto endPos = in.tellg();
    if (endPos < 0) return {};

    const auto end = static_cast<std::uint64_t>(endPos);
    if (startOffset >= end) return {};

    in.seekg(static_cast<std::streamoff>(startOffset), std::ios::beg);
    std::string content(end - startOffset, '\0');
    in.read(content.data(), static_cast<std::streamsize>(content.size()));
    content.resize(static_cast<std::size_t>(in.gcount()));
    return ParseString(content);
}

std::uint64_t FileSize(const std::filesystem::path& path) noexcept {
    std::error_code ec;
    const auto sz = std::filesystem::file_size(path, ec);
    if (ec) return 0;
    return static_cast<std::uint64_t>(sz);
}

TimingStats ComputeKeyDownStats(const std::vector<KeystrokeEntry>& entries) {
    TimingStats stats{};
    std::vector<uint64_t> deltas;
    deltas.reserve(entries.size());

    uint64_t prevMs = 0;
    bool havePrev = false;
    for (const auto& e : entries) {
        if (e.direction != KeyDirection::Down) continue;
        if (havePrev) {
            // Defensive: clamp at 0 if log somehow goes backwards (e.g. day rollover).
            const uint64_t delta = (e.timestampMs >= prevMs)
                                       ? (e.timestampMs - prevMs)
                                       : 0;
            deltas.push_back(delta);
        }
        prevMs = e.timestampMs;
        havePrev = true;
    }

    if (deltas.empty()) return stats;

    std::sort(deltas.begin(), deltas.end());

    const auto sum = std::accumulate(deltas.begin(), deltas.end(), uint64_t{0});
    stats.intervals = deltas.size();
    stats.meanMs = sum / deltas.size();
    stats.maxMs = deltas.back();

    auto pct = [&](double p) noexcept -> uint64_t {
        if (deltas.empty()) return 0;
        // Nearest-rank percentile: idx = ceil(p * n) - 1, clamped to [0, n-1].
        const auto n = deltas.size();
        auto idx = static_cast<std::size_t>(p * static_cast<double>(n));
        if (idx >= n) idx = n - 1;
        return deltas[idx];
    };
    stats.p50Ms = pct(0.50);
    stats.p95Ms = pct(0.95);
    stats.p99Ms = pct(0.99);
    return stats;
}

}  // namespace NextKey::TestRunner::HookLogParser
