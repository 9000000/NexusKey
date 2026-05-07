// NexusKey - Phonotactics Unit Tests
// SPDX-License-Identifier: GPL-3.0-only
//
// Tests for IPhonotactics rule engine: tone position, syllable validity,
// completability. Operates on rendered Vietnamese text (wstring_view).

#include <gtest/gtest.h>
#include "core/engine/IPhonotactics.h"
#include "core/engine/Phonotactics.h"
#include "core/engine/TypingEngine.h"
#include "core/config/TypingConfig.h"

namespace NextKey {
namespace Phonology {
namespace {

using ::testing::Test;

//=============================================================================
// TonePosition — locates index in vowelSeq where tone diacritic belongs
//=============================================================================

class PhonotacticsTonePosition : public ::testing::Test {
protected:
    Phonotactics phon_;
    static constexpr bool kClassic = false;
    static constexpr bool kModern  = true;
};

TEST_F(PhonotacticsTonePosition, EmptyVowelReturnsSizeMax) {
    EXPECT_EQ(phon_.TonePosition(L"", L"", kClassic), SIZE_MAX);
}

TEST_F(PhonotacticsTonePosition, SingleVowelReturnsZero) {
    EXPECT_EQ(phon_.TonePosition(L"a", L"", kClassic), 0u);
    EXPECT_EQ(phon_.TonePosition(L"e", L"", kClassic), 0u);
    EXPECT_EQ(phon_.TonePosition(L"i", L"", kClassic), 0u);
    EXPECT_EQ(phon_.TonePosition(L"o", L"", kClassic), 0u);
    EXPECT_EQ(phon_.TonePosition(L"u", L"", kClassic), 0u);
    EXPECT_EQ(phon_.TonePosition(L"y", L"", kClassic), 0u);
}

TEST_F(PhonotacticsTonePosition, SingleHornVowelReturnsZero) {
    // ư (U+01B0), ơ (U+01A1) — horn vowels alone → tone on themselves
    EXPECT_EQ(phon_.TonePosition(L"ư", L"", kClassic), 0u);
    EXPECT_EQ(phon_.TonePosition(L"ơ", L"", kClassic), 0u);
}

TEST_F(PhonotacticsTonePosition, DiphthongAiToneOnFirst) {
    // "ai" (closed) — table rule 1 → first vowel: ái
    EXPECT_EQ(phon_.TonePosition(L"ai", L"", kClassic), 0u);
    EXPECT_EQ(phon_.TonePosition(L"ai", L"", kModern), 0u);
}

TEST_F(PhonotacticsTonePosition, DiphthongAoToneOnFirst) {
    // "ao" (closed) — rule 1 → first: áo
    EXPECT_EQ(phon_.TonePosition(L"ao", L"", kClassic), 0u);
}

TEST_F(PhonotacticsTonePosition, DiphthongOaClassicNoCodaToneOnFirst) {
    // "oa" classic, no coda → "hòa": tone on FIRST (rule 3 → first when no coda)
    EXPECT_EQ(phon_.TonePosition(L"oa", L"", kClassic), 0u);
}

TEST_F(PhonotacticsTonePosition, DiphthongOaModernNoCodaToneOnSecond) {
    // "oa" modern, no coda → "hoà": tone on SECOND (rule 2 in modern table)
    EXPECT_EQ(phon_.TonePosition(L"oa", L"", kModern), 1u);
}

TEST_F(PhonotacticsTonePosition, DiphthongOaClassicWithCodaToneOnSecond) {
    // "oa" classic, with coda → "hoàn": rule 3 with coda → SECOND
    EXPECT_EQ(phon_.TonePosition(L"oa", L"n", kClassic), 1u);
}

TEST_F(PhonotacticsTonePosition, HornDiphthongUOToneOnHorn) {
    // "ươ" (U+01B0 + U+01A1) — P1 horn priority, last horn = ơ
    EXPECT_EQ(phon_.TonePosition(L"ươ", L"", kClassic), 1u);
    EXPECT_EQ(phon_.TonePosition(L"ươ", L"", kModern), 1u);
}

TEST_F(PhonotacticsTonePosition, ModifiedVowelGetsTonePriority) {
    // "ie" with ê (e+circumflex U+00EA) → P2 modified vowel: tone on ê
    EXPECT_EQ(phon_.TonePosition(L"iê", L"", kClassic), 1u);  // iê → tone on ê
    EXPECT_EQ(phon_.TonePosition(L"âu", L"", kClassic), 0u);  // âu → tone on â
}

TEST_F(PhonotacticsTonePosition, TriphthongOaiToneOnMiddle) {
    // "oai" modern → triphthong, tone on MIDDLE: hoài
    EXPECT_EQ(phon_.TonePosition(L"oai", L"", kModern), 1u);
}

TEST_F(PhonotacticsTonePosition, TriphthongUyuToneOnMiddle) {
    // "uyu" modern → triphthong, tone on MIDDLE: khuỷu
    EXPECT_EQ(phon_.TonePosition(L"uyu", L"", kModern), 1u);
}

TEST_F(PhonotacticsTonePosition, ThreeVowelUyeToneOnModifiedThird) {
    // "uyê" — ê is modified (P2 priority) → tone on ê (index 2)
    // (RuleTiengViet exception: 3-vowel default is middle, except uyê)
    EXPECT_EQ(phon_.TonePosition(L"uyê", L"", kModern), 2u);
}

//=============================================================================
// IsValidSyllable — full syllable validity per RuleTiengViet
//=============================================================================

class PhonotacticsIsValidSyllable : public ::testing::Test {
protected:
    Phonotactics phon_;
    static constexpr bool kClassic = false;
    static constexpr bool kModern  = true;
};

TEST_F(PhonotacticsIsValidSyllable, SimpleConsonantVowelIsValid) {
    // "ba" — onset b, vowel a, no coda, no tone → valid
    EXPECT_TRUE(phon_.IsValidSyllable(L"b", L"a", L"", Tone::None, kModern));
}

TEST_F(PhonotacticsIsValidSyllable, VowelInitialIsValid) {
    // "an" — no onset, vowel a, coda n → valid (e.g. "ăn", "an")
    EXPECT_TRUE(phon_.IsValidSyllable(L"", L"a", L"n", Tone::None, kModern));
}

TEST_F(PhonotacticsIsValidSyllable, ClosedVowelRejectsCoda) {
    // "ai" is a closed vowel → cannot have coda. "ain" invalid.
    EXPECT_FALSE(phon_.IsValidSyllable(L"", L"ai", L"n", Tone::None, kModern));
}

TEST_F(PhonotacticsIsValidSyllable, PendingVowelRequiresCoda) {
    // "ă" is pending (must have coda). "bă" alone invalid.
    EXPECT_FALSE(phon_.IsValidSyllable(L"b", L"ă", L"", Tone::None, kModern));
    // "băn" valid.
    EXPECT_TRUE(phon_.IsValidSyllable(L"b", L"ă", L"n", Tone::None, kModern));
}

TEST_F(PhonotacticsIsValidSyllable, CodaCPRestrictsToneToAcuteOrDot) {
    // "bac" with sắc → bác valid
    EXPECT_TRUE(phon_.IsValidSyllable(L"b", L"a", L"c", Tone::Acute, kModern));
    // "bac" with nặng → bạc valid
    EXPECT_TRUE(phon_.IsValidSyllable(L"b", L"a", L"c", Tone::Dot, kModern));
    // "bac" with huyền → invalid
    EXPECT_FALSE(phon_.IsValidSyllable(L"b", L"a", L"c", Tone::Grave, kModern));
    // "bat" with hỏi → invalid
    EXPECT_FALSE(phon_.IsValidSyllable(L"b", L"a", L"t", Tone::Hook, kModern));
    // "bach" with sắc → bách valid
    EXPECT_TRUE(phon_.IsValidSyllable(L"b", L"a", L"ch", Tone::Acute, kModern));
    // "bap" with ngã → invalid
    EXPECT_FALSE(phon_.IsValidSyllable(L"b", L"a", L"p", Tone::Tilde, kModern));
}

TEST_F(PhonotacticsIsValidSyllable, OpenCodaAllowsAnyTone) {
    // "ban" with any tone → valid (n is not c/ch/p/t)
    EXPECT_TRUE(phon_.IsValidSyllable(L"b", L"a", L"n", Tone::Acute, kModern));
    EXPECT_TRUE(phon_.IsValidSyllable(L"b", L"a", L"n", Tone::Grave, kModern));
    EXPECT_TRUE(phon_.IsValidSyllable(L"b", L"a", L"n", Tone::Hook, kModern));
    EXPECT_TRUE(phon_.IsValidSyllable(L"b", L"a", L"n", Tone::Tilde, kModern));
    EXPECT_TRUE(phon_.IsValidSyllable(L"b", L"a", L"n", Tone::Dot, kModern));
}

TEST_F(PhonotacticsIsValidSyllable, OpenSyllableAllowsAnyTone) {
    // "ba" no coda → all tones valid
    EXPECT_TRUE(phon_.IsValidSyllable(L"b", L"a", L"", Tone::Acute, kModern));
    EXPECT_TRUE(phon_.IsValidSyllable(L"b", L"a", L"", Tone::Grave, kModern));
    EXPECT_TRUE(phon_.IsValidSyllable(L"b", L"a", L"", Tone::Hook, kModern));
    EXPECT_TRUE(phon_.IsValidSyllable(L"b", L"a", L"", Tone::Tilde, kModern));
    EXPECT_TRUE(phon_.IsValidSyllable(L"b", L"a", L"", Tone::Dot, kModern));
}

//=============================================================================
// CanComplete — partial syllable extensibility (auto-exclusion gate)
//=============================================================================

class PhonotacticsCanComplete : public ::testing::Test {
protected:
    Phonotactics phon_;
};

TEST_F(PhonotacticsCanComplete, EmptyIsCompletable) {
    // Empty string is trivially extensible into any syllable.
    EXPECT_TRUE(phon_.CanComplete(L""));
}

TEST_F(PhonotacticsCanComplete, ValidSyllableIsCompletable) {
    // Already-valid syllables are by definition completable.
    EXPECT_TRUE(phon_.CanComplete(L"ba"));
    EXPECT_TRUE(phon_.CanComplete(L"ban"));
    EXPECT_TRUE(phon_.CanComplete(L"hoa"));
}

TEST_F(PhonotacticsCanComplete, ValidPrefixIsCompletable) {
    // Bare consonant — can be extended with vowels.
    EXPECT_TRUE(phon_.CanComplete(L"b"));
    EXPECT_TRUE(phon_.CanComplete(L"th"));
    EXPECT_TRUE(phon_.CanComplete(L"ng"));
}

TEST_F(PhonotacticsCanComplete, ClosedVowelPlusVowelRejected) {
    // "gach" is fully closed (a is N3 + ch coda). Adding 'a' (→"gacha") cannot
    // form a valid Vietnamese syllable — auto-exclusion case.
    EXPECT_FALSE(phon_.CanComplete(L"gacha"));
    // Sibling case from same bug: "gachw" cannot extend.
    EXPECT_FALSE(phon_.CanComplete(L"gachw"));
}

TEST_F(PhonotacticsCanComplete, NonsenseClusterRejected) {
    // "bcd" — no vowel, can't form syllable.
    EXPECT_FALSE(phon_.CanComplete(L"bcd"));
}

//=============================================================================
// TypingEngine DI plumbing — verifies G-2.1 wiring:
// TypingEngine accepts a custom IPhonotactics via ctor, default-binds to
// Phonotactics::Default() singleton, behavior unchanged from G-1 baseline.
//=============================================================================

TEST(TypingEngineDI, AcceptsCustomPhonotactics) {
    Phonotactics customPhonotactics;
    TypingConfig config;
    TypingEngine engine(config, customPhonotactics);
    // Smoke: engine constructible + functional through DI ctor.
    engine.PushChar(L'a');
    EXPECT_EQ(engine.Peek(), L"a");
}

TEST(TypingEngineDI, SingleArgCtorBindsDefaultPhonotactics) {
    // Existing single-arg ctor must still compile and behave identically;
    // it delegates to Phonotactics::Default() internally.
    TypingConfig config;
    TypingEngine engine(config);
    engine.PushChar(L'a');
    EXPECT_EQ(engine.Peek(), L"a");
}

TEST(PhonotacticsDefault, ReturnsStableSingleton) {
    // Default() must return the same instance every call (singleton lifetime
    // covers any TypingEngine that bound to it).
    const Phonotactics& a = Phonotactics::Default();
    const Phonotactics& b = Phonotactics::Default();
    EXPECT_EQ(&a, &b);
}

}  // namespace
}  // namespace Phonology
}  // namespace NextKey
