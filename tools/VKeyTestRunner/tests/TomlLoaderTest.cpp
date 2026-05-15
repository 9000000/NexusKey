#include <gtest/gtest.h>

#include "TomlLoader.h"

namespace NextKey::TestRunner::Test {

TEST(TomlLoaderTest, LoadsSingleMinimalCase) {
    constexpr std::string_view kToml = R"(
[[tests]]
name = "minimal"
keys = "abc"
expected = "abc"
)";
    const auto result = TomlLoader::LoadString(kToml);
    ASSERT_TRUE(result.error.empty()) << result.error;
    ASSERT_EQ(result.cases.size(), 1u);

    const auto& tc = result.cases[0];
    EXPECT_EQ(tc.name, "minimal");
    EXPECT_EQ(tc.targetApp, "notepad");        // default
    EXPECT_EQ(tc.keys, std::u16string(u"abc"));
    EXPECT_EQ(tc.expected, std::u16string(u"abc"));
    EXPECT_EQ(tc.interKeyMicros, 10'000u);     // default
    EXPECT_EQ(tc.budgetP99Micros, 0u);         // default
}

TEST(TomlLoaderTest, LoadsAllOptionalFields) {
    constexpr std::string_view kToml = R"(
[[tests]]
name = "full"
target_app = "chrome"
keys = "vieejt"
expected = "việt"
inter_key_us = 500
budget_p99_us = 1500
)";
    const auto result = TomlLoader::LoadString(kToml);
    ASSERT_TRUE(result.error.empty()) << result.error;
    ASSERT_EQ(result.cases.size(), 1u);

    const auto& tc = result.cases[0];
    EXPECT_EQ(tc.name, "full");
    EXPECT_EQ(tc.targetApp, "chrome");
    EXPECT_EQ(tc.keys, std::u16string(u"vieejt"));
    EXPECT_EQ(tc.expected, std::u16string(u"việt"));
    EXPECT_EQ(tc.interKeyMicros, 500u);
    EXPECT_EQ(tc.budgetP99Micros, 1500u);
}

TEST(TomlLoaderTest, LoadsMultipleCases) {
    constexpr std::string_view kToml = R"(
[[tests]]
name = "first"
keys = "abc"
expected = "abc"

[[tests]]
name = "second"
keys = "vieejt"
expected = "việt"
)";
    const auto result = TomlLoader::LoadString(kToml);
    ASSERT_TRUE(result.error.empty()) << result.error;
    ASSERT_EQ(result.cases.size(), 2u);
    EXPECT_EQ(result.cases[0].name, "first");
    EXPECT_EQ(result.cases[1].name, "second");
}

TEST(TomlLoaderTest, BackspaceEscapeInKeysIsResolvedByTomlBasicString) {
    // TOML basic strings ("...") already resolve \b -> U+0008, so by the time
    // we see the value the escape is already a real backspace char. Verify.
    constexpr std::string_view kToml = R"(
[[tests]]
name = "bs"
keys = "ab\bc"
expected = "ac"
)";
    const auto result = TomlLoader::LoadString(kToml);
    ASSERT_TRUE(result.error.empty()) << result.error;
    ASSERT_EQ(result.cases.size(), 1u);
    EXPECT_EQ(result.cases[0].keys, std::u16string(u"ab\bc"));
}

TEST(TomlLoaderTest, BackslashInLiteralStringPreservedThenResolvedByEscapeStage) {
    // Literal strings ('...') do NOT resolve TOML escapes, so '\b' stays as
    // the two chars '\' and 'b'. KeyEscapes::Resolve then converts to U+0008.
    constexpr std::string_view kToml = R"(
[[tests]]
name = "literal-bs"
keys = 'ab\bc'
expected = "ac"
)";
    const auto result = TomlLoader::LoadString(kToml);
    ASSERT_TRUE(result.error.empty()) << result.error;
    ASSERT_EQ(result.cases.size(), 1u);
    EXPECT_EQ(result.cases[0].keys, std::u16string(u"ab\bc"));
}

TEST(TomlLoaderTest, MissingNameFails) {
    constexpr std::string_view kToml = R"(
[[tests]]
keys = "abc"
expected = "abc"
)";
    const auto result = TomlLoader::LoadString(kToml);
    EXPECT_FALSE(result.error.empty());
    EXPECT_TRUE(result.cases.empty());
}

TEST(TomlLoaderTest, MissingKeysFails) {
    constexpr std::string_view kToml = R"(
[[tests]]
name = "x"
expected = "abc"
)";
    const auto result = TomlLoader::LoadString(kToml);
    EXPECT_FALSE(result.error.empty());
    EXPECT_TRUE(result.cases.empty());
}

TEST(TomlLoaderTest, MissingExpectedFails) {
    constexpr std::string_view kToml = R"(
[[tests]]
name = "x"
keys = "abc"
)";
    const auto result = TomlLoader::LoadString(kToml);
    EXPECT_FALSE(result.error.empty());
    EXPECT_TRUE(result.cases.empty());
}

TEST(TomlLoaderTest, NoTestsArrayFails) {
    constexpr std::string_view kToml = R"(
title = "no tests here"
)";
    const auto result = TomlLoader::LoadString(kToml);
    EXPECT_FALSE(result.error.empty());
}

TEST(TomlLoaderTest, EmptyTestsArrayFails) {
    constexpr std::string_view kToml = R"(
tests = []
)";
    const auto result = TomlLoader::LoadString(kToml);
    EXPECT_FALSE(result.error.empty());
}

TEST(TomlLoaderTest, MalformedTomlFails) {
    constexpr std::string_view kToml = R"(
[[tests
name = "x"
)";
    const auto result = TomlLoader::LoadString(kToml);
    EXPECT_FALSE(result.error.empty());
    EXPECT_TRUE(result.cases.empty());
}

// ---------------------------------------------------------------------------
// `text` field (auto-converts via Telex::StrToTelex)
// ---------------------------------------------------------------------------

TEST(TomlLoaderTest, TextFieldAutoConvertsToTelex) {
    // "việt" -> StrToTelex -> "vieejt" (per Telex.h kTable: ệ -> eej).
    constexpr std::string_view kToml = R"(
[[tests]]
name = "text-only"
text = "việt"
)";
    const auto result = TomlLoader::LoadString(kToml);
    ASSERT_TRUE(result.error.empty()) << result.error;
    ASSERT_EQ(result.cases.size(), 1u);
    EXPECT_EQ(result.cases[0].keys, std::u16string(u"vieejt"));
    // Default: expected mirrors text when expected omitted.
    EXPECT_EQ(result.cases[0].expected, std::u16string(u"việt"));
}

TEST(TomlLoaderTest, TextFieldAllowsExpectedOverride) {
    // For typo+correction scenarios where final text differs from input text.
    constexpr std::string_view kToml = R"(
[[tests]]
name = "text-with-explicit-expected"
text = "việt"
expected = "viết"
)";
    const auto result = TomlLoader::LoadString(kToml);
    ASSERT_TRUE(result.error.empty()) << result.error;
    ASSERT_EQ(result.cases.size(), 1u);
    EXPECT_EQ(result.cases[0].keys, std::u16string(u"vieejt"));
    EXPECT_EQ(result.cases[0].expected, std::u16string(u"viết"));
}

TEST(TomlLoaderTest, TextAndKeysTogetherIsError) {
    // Mutually exclusive -- pick one source for keys, not both.
    constexpr std::string_view kToml = R"(
[[tests]]
name = "both"
text = "việt"
keys = "vieejt"
expected = "việt"
)";
    const auto result = TomlLoader::LoadString(kToml);
    EXPECT_FALSE(result.error.empty());
    EXPECT_TRUE(result.cases.empty());
}

TEST(TomlLoaderTest, NeitherTextNorKeysIsError) {
    constexpr std::string_view kToml = R"(
[[tests]]
name = "nothing"
expected = "abc"
)";
    const auto result = TomlLoader::LoadString(kToml);
    EXPECT_FALSE(result.error.empty());
    EXPECT_TRUE(result.cases.empty());
}

// ---------------------------------------------------------------------------
// `verdict_mode` field
// ---------------------------------------------------------------------------

TEST(TomlLoaderTest, VerdictModeDefaultsToExact) {
    constexpr std::string_view kToml = R"(
[[tests]]
name = "default-verdict"
keys = "abc"
expected = "abc"
)";
    const auto result = TomlLoader::LoadString(kToml);
    ASSERT_TRUE(result.error.empty()) << result.error;
    ASSERT_EQ(result.cases.size(), 1u);
    EXPECT_EQ(result.cases[0].verdictMode, VerdictMode::Exact);
    EXPECT_DOUBLE_EQ(result.cases[0].thresholdPct, 100.0);
}

TEST(TomlLoaderTest, VerdictModeExactExplicit) {
    constexpr std::string_view kToml = R"(
[[tests]]
name = "explicit-exact"
keys = "abc"
expected = "abc"
verdict_mode = "exact"
)";
    const auto result = TomlLoader::LoadString(kToml);
    ASSERT_TRUE(result.error.empty()) << result.error;
    ASSERT_EQ(result.cases.size(), 1u);
    EXPECT_EQ(result.cases[0].verdictMode, VerdictMode::Exact);
}

TEST(TomlLoaderTest, VerdictModeEditDistanceParses) {
    constexpr std::string_view kToml = R"(
[[tests]]
name = "ed"
text = "việt nam"
verdict_mode = "edit_distance"
threshold_pct = 95.0
)";
    const auto result = TomlLoader::LoadString(kToml);
    ASSERT_TRUE(result.error.empty()) << result.error;
    ASSERT_EQ(result.cases.size(), 1u);
    EXPECT_EQ(result.cases[0].verdictMode, VerdictMode::EditDistance);
    EXPECT_DOUBLE_EQ(result.cases[0].thresholdPct, 95.0);
}

TEST(TomlLoaderTest, VerdictModeUnknownIsError) {
    constexpr std::string_view kToml = R"(
[[tests]]
name = "bad-mode"
keys = "abc"
expected = "abc"
verdict_mode = "fuzzy"
)";
    const auto result = TomlLoader::LoadString(kToml);
    EXPECT_FALSE(result.error.empty());
    EXPECT_TRUE(result.cases.empty());
}

TEST(TomlLoaderTest, ThresholdPctDefaultIsHundred) {
    // edit_distance without threshold_pct -> 100.0 (exact-match in spirit).
    constexpr std::string_view kToml = R"(
[[tests]]
name = "no-threshold"
text = "abc"
verdict_mode = "edit_distance"
)";
    const auto result = TomlLoader::LoadString(kToml);
    ASSERT_TRUE(result.error.empty()) << result.error;
    ASSERT_EQ(result.cases.size(), 1u);
    EXPECT_DOUBLE_EQ(result.cases[0].thresholdPct, 100.0);
}

}  // namespace NextKey::TestRunner::Test
