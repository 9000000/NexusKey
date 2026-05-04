// EditDistance.h -- Levenshtein distance over u16string_view + ErrorPct helper.
//
// Header-only, pure CPU, no platform deps. Used by the corpus runner when a
// test case opts into verdict_mode = "edit_distance" (sustained-typing tests
// where exact match is too brittle but a corruption-rate threshold is useful).

#pragma once

#include <algorithm>
#include <cstddef>
#include <string_view>
#include <vector>

namespace NextKey::TestRunner::EditDistance {

// Levenshtein distance over UTF-16 code units. Symmetric, O(m*n) time,
// O(min(m,n)) space. Vietnamese pre-composed chars are BMP (<=U+1EF9), so
// per-char16_t comparison matches "user-perceived character" for our corpus.
[[nodiscard]] inline std::size_t Levenshtein(std::u16string_view a,
                                              std::u16string_view b) {
    // Ensure `a` is the longer one so the rolling buffer is sized to min.
    if (a.size() < b.size()) {
        std::swap(a, b);
    }
    const std::size_t m = a.size();
    const std::size_t n = b.size();
    if (n == 0) {
        return m;
    }

    std::vector<std::size_t> prev(n + 1);
    std::vector<std::size_t> curr(n + 1);
    for (std::size_t j = 0; j <= n; ++j) {
        prev[j] = j;
    }

    for (std::size_t i = 1; i <= m; ++i) {
        curr[0] = i;
        for (std::size_t j = 1; j <= n; ++j) {
            const std::size_t cost = (a[i - 1] == b[j - 1]) ? 0u : 1u;
            curr[j] = std::min({
                prev[j] + 1u,         // deletion from a
                curr[j - 1] + 1u,     // insertion into a
                prev[j - 1] + cost,   // substitution
            });
        }
        std::swap(prev, curr);
    }
    return prev[n];
}

// Error percentage as Levenshtein / max(1, expected.size()) * 100.
// Special cases:
//   both empty       -> 0.0  (perfect match)
//   expected empty   -> 100.0 if actual non-empty (every char is unexpected)
[[nodiscard]] inline double ErrorPct(std::u16string_view actual,
                                      std::u16string_view expected) {
    if (actual.empty() && expected.empty()) {
        return 0.0;
    }
    if (expected.empty()) {
        return 100.0;
    }
    const std::size_t lev = Levenshtein(actual, expected);
    return static_cast<double>(lev) /
           static_cast<double>(expected.size()) * 100.0;
}

}  // namespace NextKey::TestRunner::EditDistance
