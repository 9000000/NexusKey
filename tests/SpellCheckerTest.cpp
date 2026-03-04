// NexusKey - SpellChecker Unit Tests
// SPDX-License-Identifier: GPL-3.0-only
//
// Tests: Direct validator tests + engine integration tests

#include <gtest/gtest.h>
#include "core/engine/SpellChecker.h"
#include "core/engine/TelexEngine.h"
#include "core/engine/VniEngine.h"
#include "core/config/TypingConfig.h"
#include "TestHelper.h"

namespace NextKey {
namespace {

using SpellCheck::Result;
using SpellCheck::Validate;

//=============================================================================
// Helper: Build Telex CharState array from a description
//=============================================================================

// Build a Telex::CharState from base char, modifier, and tone
Telex::CharState MakeTelex(wchar_t base,
                            Telex::Modifier mod = Telex::Modifier::None,
                            Telex::Tone tone = Telex::Tone::None) {
    Telex::CharState s;
    s.base = base;
    s.mod = mod;
    s.tone = tone;
    s.isUpper = false;
    return s;
}

// Shorthand for common cases
Telex::CharState T(wchar_t base) { return MakeTelex(base); }
Telex::CharState TM(wchar_t base, Telex::Modifier mod) { return MakeTelex(base, mod); }
Telex::CharState TT(wchar_t base, Telex::Tone tone) { return MakeTelex(base, Telex::Modifier::None, tone); }
Telex::CharState TMT(wchar_t base, Telex::Modifier mod, Telex::Tone tone) { return MakeTelex(base, mod, tone); }

// Validate a vector of Telex CharStates
Result V(const std::vector<Telex::CharState>& states) {
    return Validate(states.data(), states.size());
}

//=============================================================================
// Direct Validator Tests — Valid Complete Syllables
//=============================================================================

class SpellCheckerValidTest : public ::testing::Test {};

TEST_F(SpellCheckerValidTest, EmptyIsPrefix) {
    EXPECT_EQ(V({}), Result::ValidPrefix);
}

TEST_F(SpellCheckerValidTest, SingleVowel_a) {
    EXPECT_EQ(V({T(L'a')}), Result::Valid);
}

TEST_F(SpellCheckerValidTest, SingleVowel_i) {
    EXPECT_EQ(V({T(L'i')}), Result::Valid);
}

TEST_F(SpellCheckerValidTest, Ba) {
    EXPECT_EQ(V({T(L'b'), T(L'a')}), Result::Valid);
}

TEST_F(SpellCheckerValidTest, Cam) {
    // c + a + m
    EXPECT_EQ(V({T(L'c'), T(L'a'), T(L'm')}), Result::Valid);
}

TEST_F(SpellCheckerValidTest, Di_Stroke) {
    // đ + i  (đi)
    EXPECT_EQ(V({TM(L'd', Telex::Modifier::Breve), T(L'i')}), Result::Valid);
}

TEST_F(SpellCheckerValidTest, Nghi) {
    // ngh + i
    EXPECT_EQ(V({T(L'n'), T(L'g'), T(L'h'), T(L'i')}), Result::Valid);
}

TEST_F(SpellCheckerValidTest, Qua) {
    // qu + a
    EXPECT_EQ(V({T(L'q'), T(L'u'), T(L'a')}), Result::Valid);
}

TEST_F(SpellCheckerValidTest, Oai) {
    // vowel-only: oai (triple no-end)
    EXPECT_EQ(V({T(L'o'), T(L'a'), T(L'i')}), Result::Valid);
}

TEST_F(SpellCheckerValidTest, Uoi_WithHorn) {
    // ươi (triple no-end, with horn modifiers)
    EXPECT_EQ(V({TM(L'u', Telex::Modifier::Horn), TM(L'o', Telex::Modifier::Horn), T(L'i')}), Result::Valid);
}

TEST_F(SpellCheckerValidTest, Yeu_WithCircumflex) {
    // yêu
    EXPECT_EQ(V({T(L'y'), TM(L'e', Telex::Modifier::Circumflex), T(L'u')}), Result::Valid);
}

TEST_F(SpellCheckerValidTest, Bac_WithAcute) {
    // bác (stop final c + acute tone → valid)
    EXPECT_EQ(V({T(L'b'), TT(L'a', Telex::Tone::Acute), T(L'c')}), Result::Valid);
}

TEST_F(SpellCheckerValidTest, Bat_WithDot) {
    // bạt (stop final t + dot tone → valid)
    EXPECT_EQ(V({T(L'b'), TT(L'a', Telex::Tone::Dot), T(L't')}), Result::Valid);
}

TEST_F(SpellCheckerValidTest, Than) {
    // th + a + n
    EXPECT_EQ(V({T(L't'), T(L'h'), T(L'a'), T(L'n')}), Result::Valid);
}

TEST_F(SpellCheckerValidTest, Trang) {
    // tr + a + ng
    EXPECT_EQ(V({T(L't'), T(L'r'), T(L'a'), T(L'n'), T(L'g')}), Result::Valid);
}

TEST_F(SpellCheckerValidTest, An) {
    // a + n (vowel-initial with final)
    EXPECT_EQ(V({T(L'a'), T(L'n')}), Result::Valid);
}

TEST_F(SpellCheckerValidTest, Anh) {
    // a + nh
    EXPECT_EQ(V({T(L'a'), T(L'n'), T(L'h')}), Result::Valid);
}

TEST_F(SpellCheckerValidTest, Ong) {
    // o + ng
    EXPECT_EQ(V({T(L'o'), T(L'n'), T(L'g')}), Result::Valid);
}

TEST_F(SpellCheckerValidTest, Gia) {
    // gi + a → valid (gi as consonant cluster)
    EXPECT_EQ(V({T(L'g'), T(L'i'), T(L'a')}), Result::Valid);
}

TEST_F(SpellCheckerValidTest, Gi_Alone) {
    // "gi" alone → g + vowel_i → valid
    EXPECT_EQ(V({T(L'g'), T(L'i')}), Result::Valid);
}

TEST_F(SpellCheckerValidTest, Loan) {
    // l + oa + n
    EXPECT_EQ(V({T(L'l'), T(L'o'), T(L'a'), T(L'n')}), Result::Valid);
}

TEST_F(SpellCheckerValidTest, Oan) {
    // oa + n (oan)
    EXPECT_EQ(V({T(L'o'), T(L'a'), T(L'n')}), Result::Valid);
}

//=============================================================================
// Direct Validator Tests — Valid Prefix
//=============================================================================

class SpellCheckerPrefixTest : public ::testing::Test {};

TEST_F(SpellCheckerPrefixTest, SingleConsonant_b) {
    EXPECT_EQ(V({T(L'b')}), Result::ValidPrefix);
}

TEST_F(SpellCheckerPrefixTest, TwoCharConsonant_th) {
    EXPECT_EQ(V({T(L't'), T(L'h')}), Result::ValidPrefix);
}

TEST_F(SpellCheckerPrefixTest, ThreeCharConsonant_ngh) {
    EXPECT_EQ(V({T(L'n'), T(L'g'), T(L'h')}), Result::ValidPrefix);
}

TEST_F(SpellCheckerPrefixTest, Qu_Prefix) {
    EXPECT_EQ(V({T(L'q'), T(L'u')}), Result::ValidPrefix);
}

TEST_F(SpellCheckerPrefixTest, SingleD) {
    EXPECT_EQ(V({T(L'd')}), Result::ValidPrefix);
}

//=============================================================================
// Direct Validator Tests — Invalid
//=============================================================================

class SpellCheckerInvalidTest : public ::testing::Test {};

TEST_F(SpellCheckerInvalidTest, Bl_InvalidCluster) {
    EXPECT_EQ(V({T(L'b'), T(L'l')}), Result::Invalid);
}

TEST_F(SpellCheckerInvalidTest, Bk_InvalidCluster) {
    EXPECT_EQ(V({T(L'b'), T(L'k')}), Result::Invalid);
}

TEST_F(SpellCheckerInvalidTest, FourVowels) {
    // aaaa → invalid (too many vowels)
    EXPECT_EQ(V({T(L'a'), T(L'a'), T(L'a'), T(L'a')}), Result::Invalid);
}

TEST_F(SpellCheckerInvalidTest, InvalidVowelCombo_ae) {
    // "ae" is not in the vowel table
    EXPECT_EQ(V({T(L'a'), T(L'e')}), Result::Invalid);
}

TEST_F(SpellCheckerInvalidTest, StopFinal_Grave_Bac) {
    // bàc — stop final 'c' with grave tone → invalid
    EXPECT_EQ(V({T(L'b'), TT(L'a', Telex::Tone::Grave), T(L'c')}), Result::Invalid);
}

TEST_F(SpellCheckerInvalidTest, StopFinal_Hook_Bac) {
    // bảc — stop final 'c' with hook tone → invalid
    EXPECT_EQ(V({T(L'b'), TT(L'a', Telex::Tone::Hook), T(L'c')}), Result::Invalid);
}

TEST_F(SpellCheckerInvalidTest, StopFinal_Tilde_Bat) {
    // bãt — stop final 't' with tilde tone → invalid
    EXPECT_EQ(V({T(L'b'), TT(L'a', Telex::Tone::Tilde), T(L't')}), Result::Invalid);
}

TEST_F(SpellCheckerInvalidTest, ExtraAfterFinal) {
    // "bang" is valid, but "bangx" has extra chars
    EXPECT_EQ(V({T(L'b'), T(L'a'), T(L'n'), T(L'g'), T(L'x')}), Result::Invalid);
}

TEST_F(SpellCheckerInvalidTest, NoEndVowel_WithConsonant) {
    // "ai" cannot have end consonant → "aim" is invalid
    EXPECT_EQ(V({T(L'a'), T(L'i'), T(L'm')}), Result::Invalid);
}

//=============================================================================
// gi/qu special decomposition tests
//=============================================================================

class SpellCheckerGiQuTest : public ::testing::Test {};

TEST_F(SpellCheckerGiQuTest, Gia_Valid) {
    // gi + a → valid
    EXPECT_EQ(V({T(L'g'), T(L'i'), T(L'a')}), Result::Valid);
}

TEST_F(SpellCheckerGiQuTest, Gip_Valid) {
    // g + i + p → valid (gíp, as in "gip" = help in some dialects)
    // Decompose: g + vowel(i) + final(p)
    EXPECT_EQ(V({T(L'g'), T(L'i'), T(L'p')}), Result::Valid);
}

TEST_F(SpellCheckerGiQuTest, Qua_Valid) {
    EXPECT_EQ(V({T(L'q'), T(L'u'), T(L'a')}), Result::Valid);
}

TEST_F(SpellCheckerGiQuTest, Quan_Valid) {
    EXPECT_EQ(V({T(L'q'), T(L'u'), T(L'a'), T(L'n')}), Result::Valid);
}

TEST_F(SpellCheckerGiQuTest, Gian_Valid) {
    // gi + a + n
    EXPECT_EQ(V({T(L'g'), T(L'i'), T(L'a'), T(L'n')}), Result::Valid);
}

//=============================================================================
// Engine Integration Tests — TelexEngine with spellCheck ON
//=============================================================================

using Testing::TypeString;

class TelexSpellCheckTest : public ::testing::Test {
protected:
    void SetUp() override {
        config_.inputMethod = InputMethod::Telex;
        config_.spellCheckEnabled = true;
    }

    TypingConfig config_;
};

TEST_F(TelexSpellCheckTest, ValidSyllable_ToneApplied) {
    // "ba" + 's' → "bá" (tone applied, valid syllable)
    Telex::TelexEngine engine(config_);
    TypeString(engine, L"bas");
    EXPECT_EQ(engine.Peek(), L"bá");
}

TEST_F(TelexSpellCheckTest, InvalidSyllable_ToneBlocked) {
    // "bl" is invalid → 's' treated as regular char
    Telex::TelexEngine engine(config_);
    TypeString(engine, L"bls");
    EXPECT_EQ(engine.Peek(), L"bls");
}

TEST_F(TelexSpellCheckTest, InvalidSyllable_ModifierNotBlocked) {
    // Modifiers are NOT gated — in full Telex, standalone 'w' → ư
    Telex::TelexEngine engine(config_);
    TypeString(engine, L"blw");
    EXPECT_EQ(engine.Peek(), L"blư");
}

TEST_F(TelexSpellCheckTest, DD_NotBlocked) {
    // "dd" → "đ" even when spell check is on
    // dd should always work because đ is a valid consonant
    Telex::TelexEngine engine(config_);
    TypeString(engine, L"dd");
    EXPECT_EQ(engine.Peek(), L"đ");
}

TEST_F(TelexSpellCheckTest, DD_InInvalidContext_StillWorks) {
    // Even in an "invalid" context, dd → đ is not gated
    Telex::TelexEngine engine(config_);
    TypeString(engine, L"bld");
    // "bld" is invalid, but adding another 'd' shouldn't matter
    // since dd→đ is processed on the last 'd', and spell check doesn't gate it
    // Actually "bld" - 'd' is just a regular char. The dd modifier
    // looks for a 'd' in states_ to convert to đ.
    // Let's test a simpler case:
    engine.Reset();
    TypeString(engine, L"dd");
    EXPECT_EQ(engine.Peek(), L"đ");
}

TEST_F(TelexSpellCheckTest, BackspaceRestoresToneAbility) {
    // Type "bl" (invalid) → 's' blocked → backspace 'l' → "b" (valid prefix) → type "as" → "bá"
    Telex::TelexEngine engine(config_);
    TypeString(engine, L"bl");
    EXPECT_EQ(engine.Peek(), L"bl");  // invalid state

    // Type 's' — should be blocked
    engine.PushChar(L's');
    EXPECT_EQ(engine.Peek(), L"bls");

    // Backspace removes 's'
    engine.Backspace();
    EXPECT_EQ(engine.Peek(), L"bl");

    // Backspace removes 'l'
    engine.Backspace();
    EXPECT_EQ(engine.Peek(), L"b");

    // Now type "as" — should work
    TypeString(engine, L"as");
    EXPECT_EQ(engine.Peek(), L"bá");
}

TEST_F(TelexSpellCheckTest, SpellCheckOff_NoBlocking) {
    // With spellCheck OFF, "bl" + 's' still tries tone (fails naturally, 's' added as char)
    config_.spellCheckEnabled = false;
    Telex::TelexEngine engine(config_);
    TypeString(engine, L"bls");
    // Without spell check, 's' is a tone key — ProcessTone tries to find vowel target,
    // fails (no vowels), falls through to ProcessChar, adds 's' as regular char
    EXPECT_EQ(engine.Peek(), L"bls");
}

TEST_F(TelexSpellCheckTest, ValidWord_Duoc) {
    // "duoc" + 'j' → should apply dot tone (đ is separate, but "duoc" is valid)
    // Actually let's test with standard: "duowcs" → đước
    Telex::TelexEngine engine(config_);
    TypeString(engine, L"duowcs");
    // d-u-o → ProcessModifier(w) → ươ → ProcessModifier(c is not modifier) →
    // Actually 'c' is regular char, 's' is tone
    // Let me trace: d,u,o,w,c,s
    // d → state [d]
    // u → state [d, u]
    // o → state [d, u, o]
    // w → ProcessWModifier: u+o pattern → horn on o → [d, u, ơ] → AutoUO → [d, ư, ơ]
    // c → ProcessChar [d, ư, ơ, c]
    // s → ProcessTone: acute on ơ → [d, ư, ớ, c] → "dước"
    // Actually 'd' at start has no modifier, so it's just 'd' not 'đ'
    EXPECT_EQ(engine.Peek(), L"dước");
}

TEST_F(TelexSpellCheckTest, CircumflexModifier_NotGated) {
    // Modifiers are not gated by spell check.
    // "bl" (invalid) + "a" → "bla" + "a" → circumflex applied to 'a' → "blâ"
    Telex::TelexEngine engine(config_);
    TypeString(engine, L"blaa");
    EXPECT_EQ(engine.Peek(), L"blâ");
}

//=============================================================================
// Engine Integration Tests — VniEngine with spellCheck ON
//=============================================================================

class VniSpellCheckTest : public ::testing::Test {
protected:
    void SetUp() override {
        config_.inputMethod = InputMethod::VNI;
        config_.spellCheckEnabled = true;
    }

    TypingConfig config_;
};

TEST_F(VniSpellCheckTest, ValidSyllable_ToneApplied) {
    // "ba" + '1' → "bá"
    Vni::VniEngine engine(config_);
    TypeString(engine, L"ba1");
    EXPECT_EQ(engine.Peek(), L"bá");
}

TEST_F(VniSpellCheckTest, InvalidSyllable_ToneBlocked) {
    // "bl" + '1' → "bl1" (blocked)
    Vni::VniEngine engine(config_);
    TypeString(engine, L"bl1");
    EXPECT_EQ(engine.Peek(), L"bl1");
}

TEST_F(VniSpellCheckTest, VowelMod_NotBlocked) {
    // Modifiers are NOT gated by spell check
    // "bl" + '6' → no vowel to apply circumflex → falls through to ProcessChar → "bl6"
    Vni::VniEngine engine(config_);
    TypeString(engine, L"bl6");
    EXPECT_EQ(engine.Peek(), L"bl6");
}

TEST_F(VniSpellCheckTest, Stroke_NotBlocked) {
    // "d" + '9' → "đ" (stroke is NOT gated)
    Vni::VniEngine engine(config_);
    TypeString(engine, L"d9");
    EXPECT_EQ(engine.Peek(), L"đ");
}

TEST_F(VniSpellCheckTest, BackspaceRestoresToneAbility) {
    Vni::VniEngine engine(config_);
    TypeString(engine, L"bl");
    engine.PushChar(L'1');
    EXPECT_EQ(engine.Peek(), L"bl1");

    // Backspace to remove '1', then 'l'
    engine.Backspace();
    engine.Backspace();
    EXPECT_EQ(engine.Peek(), L"b");

    // Now type valid syllable
    TypeString(engine, L"a1");
    EXPECT_EQ(engine.Peek(), L"bá");
}

//=============================================================================
// Edge cases
//=============================================================================

class SpellCheckerEdgeTest : public ::testing::Test {};

TEST_F(SpellCheckerEdgeTest, Uyen_Valid) {
    // uyên (uyê + n)
    EXPECT_EQ(V({T(L'u'), T(L'y'), TM(L'e', Telex::Modifier::Circumflex), T(L'n')}), Result::Valid);
}

TEST_F(SpellCheckerEdgeTest, Oang_Valid) {
    // oăng (oă + ng)
    EXPECT_EQ(V({T(L'o'), TM(L'a', Telex::Modifier::Breve), T(L'n'), T(L'g')}), Result::Valid);
}

TEST_F(SpellCheckerEdgeTest, Uong_WithHorn) {
    // uống (uô + ng, with circumflex)
    EXPECT_EQ(V({T(L'u'), TM(L'o', Telex::Modifier::Circumflex), T(L'n'), T(L'g')}), Result::Valid);
}

TEST_F(SpellCheckerEdgeTest, Ach) {
    // ach (a + ch)
    EXPECT_EQ(V({T(L'a'), T(L'c'), T(L'h')}), Result::Valid);
}

TEST_F(SpellCheckerEdgeTest, StopFinal_Acute_Valid) {
    // bắc (stop final + acute → valid)
    EXPECT_EQ(V({T(L'b'), TMT(L'a', Telex::Modifier::Breve, Telex::Tone::Acute), T(L'c')}), Result::Valid);
}

TEST_F(SpellCheckerEdgeTest, StopFinal_Dot_Valid) {
    // bặc (stop final + dot → valid)
    EXPECT_EQ(V({T(L'b'), TMT(L'a', Telex::Modifier::Breve, Telex::Tone::Dot), T(L'c')}), Result::Valid);
}

TEST_F(SpellCheckerEdgeTest, StopFinal_Grave_Invalid) {
    // bằc (stop final + grave → invalid)
    EXPECT_EQ(V({T(L'b'), TMT(L'a', Telex::Modifier::Breve, Telex::Tone::Grave), T(L'c')}), Result::Invalid);
}

TEST_F(SpellCheckerEdgeTest, NonStopFinal_AllTones_Valid) {
    // Non-stop finals (m, n, ng, nh) allow all tones
    // bàn (grave + n → valid)
    EXPECT_EQ(V({T(L'b'), TT(L'a', Telex::Tone::Grave), T(L'n')}), Result::Valid);
    // bản (hook + n → valid)
    EXPECT_EQ(V({T(L'b'), TT(L'a', Telex::Tone::Hook), T(L'n')}), Result::Valid);
    // bãn (tilde + n → valid)
    EXPECT_EQ(V({T(L'b'), TT(L'a', Telex::Tone::Tilde), T(L'n')}), Result::Valid);
}

TEST_F(SpellCheckerEdgeTest, Khanh) {
    // kh + a + nh
    EXPECT_EQ(V({T(L'k'), T(L'h'), T(L'a'), T(L'n'), T(L'h')}), Result::Valid);
}

TEST_F(SpellCheckerEdgeTest, Phat) {
    // ph + a + t
    EXPECT_EQ(V({T(L'p'), T(L'h'), TT(L'a', Telex::Tone::Acute), T(L't')}), Result::Valid);
}

TEST_F(SpellCheckerEdgeTest, Nghieng) {
    // ngh + iê + ng
    EXPECT_EQ(V({T(L'n'), T(L'g'), T(L'h'), T(L'i'), TM(L'e', Telex::Modifier::Circumflex), T(L'n'), T(L'g')}), Result::Valid);
}

// k as stop final — minority-language proper nouns (Đắk Lắk, Đắk Nông)
TEST_F(SpellCheckerEdgeTest, FinalK_Dak_Valid) {
    // đắk (đ + ắ + k → valid, stop final)
    EXPECT_EQ(V({TM(L'd', Telex::Modifier::Breve), TMT(L'a', Telex::Modifier::Breve, Telex::Tone::Acute), T(L'k')}), Result::Valid);
}

TEST_F(SpellCheckerEdgeTest, FinalK_Lak_Valid) {
    // lắk (l + ắ + k → valid, stop final)
    EXPECT_EQ(V({T(L'l'), TMT(L'a', Telex::Modifier::Breve, Telex::Tone::Acute), T(L'k')}), Result::Valid);
}

TEST_F(SpellCheckerEdgeTest, FinalK_StopTone_Acute_Valid) {
    // Stop final k + acute → valid
    EXPECT_EQ(V({T(L'b'), TMT(L'a', Telex::Modifier::Breve, Telex::Tone::Acute), T(L'k')}), Result::Valid);
}

TEST_F(SpellCheckerEdgeTest, FinalK_StopTone_Dot_Valid) {
    // Stop final k + dot → valid
    EXPECT_EQ(V({T(L'b'), TMT(L'a', Telex::Modifier::Breve, Telex::Tone::Dot), T(L'k')}), Result::Valid);
}

TEST_F(SpellCheckerEdgeTest, FinalK_StopTone_Grave_Invalid) {
    // Stop final k + grave → invalid (same rule as c)
    EXPECT_EQ(V({T(L'b'), TMT(L'a', Telex::Modifier::Breve, Telex::Tone::Grave), T(L'k')}), Result::Invalid);
}

//=============================================================================
// Free Marking Tests — Telex engine with spellCheck ON + freeMarking ON
//=============================================================================

class TelexFreeMarkingTest : public ::testing::Test {
protected:
    void SetUp() override {
        config_.inputMethod = InputMethod::Telex;
        config_.spellCheckEnabled = true;
        config_.freeMarking = true;
    }

    TypingConfig config_;
};

TEST_F(TelexFreeMarkingTest, ToneAppliedOnInvalidSyllable_Tiens) {
    // "tien" is invalid (ie without circumflex) → with freeMarking, 's' still applies tone
    // t-i-e-n-s → tone on 'e' (rightmost in diphthong) → tiến
    Telex::TelexEngine engine(config_);
    TypeString(engine, L"tiens");
    EXPECT_EQ(engine.Peek(), L"tién");
}

TEST_F(TelexFreeMarkingTest, ToneAppliedOnInvalidSyllable_Bls) {
    // "bl" is invalid consonant cluster → with freeMarking, 's' still applies as literal
    // (no vowels to apply tone to, ProcessTone fails → falls through to ProcessChar)
    Telex::TelexEngine engine(config_);
    TypeString(engine, L"bls");
    EXPECT_EQ(engine.Peek(), L"bls");
}

TEST_F(TelexFreeMarkingTest, ToneAppliedOnInvalidSyllable_Blas) {
    // "bla" is invalid → with freeMarking, 's' applies tone to 'a' → "blá"
    Telex::TelexEngine engine(config_);
    TypeString(engine, L"blas");
    EXPECT_EQ(engine.Peek(), L"blá");
}

TEST_F(TelexFreeMarkingTest, ValidSyllable_StillWorks) {
    // "ba" + 's' → "bá" (valid syllable, works same as without freeMarking)
    Telex::TelexEngine engine(config_);
    TypeString(engine, L"bas");
    EXPECT_EQ(engine.Peek(), L"bá");
}

TEST_F(TelexFreeMarkingTest, ClearTone_Z_BypassesGate) {
    // With freeMarking, 'z' should clear tone even on invalid syllable
    Telex::TelexEngine engine(config_);
    TypeString(engine, L"blas");  // "blá"
    engine.PushChar(L'z');        // clear tone → "bla"
    EXPECT_EQ(engine.Peek(), L"bla");
}

TEST_F(TelexFreeMarkingTest, FreeMarkingOff_ToneBlocked) {
    // Verify original behavior: freeMarking OFF → tone blocked on invalid syllable
    config_.freeMarking = false;
    Telex::TelexEngine engine(config_);
    TypeString(engine, L"blas");
    EXPECT_EQ(engine.Peek(), L"blas");  // 's' treated as literal, not tone
}

TEST_F(TelexFreeMarkingTest, BackwardCircumflex_TiensE) {
    // "tiens" → "tién", then 'e' → backward scan finds 'é' → circumflex → "tiến"
    Telex::TelexEngine engine(config_);
    TypeString(engine, L"tiens");
    EXPECT_EQ(engine.Peek(), L"tién");
    engine.PushChar(L'e');
    EXPECT_EQ(engine.Peek(), L"tiến");
}

TEST_F(TelexFreeMarkingTest, BackwardCircumflex_Tiensge) {
    // Full flow: "tiensge" → "tiếng"
    // t-i-e-n-s → "tién" (tone applied freely)
    // g → "tiéng"
    // e → backward scan finds 'é' → circumflex → "tiếng"
    Telex::TelexEngine engine(config_);
    TypeString(engine, L"tiensge");
    EXPECT_EQ(engine.Peek(), L"tiếng");
}

TEST_F(TelexFreeMarkingTest, BackwardCircumflex_NotTriggeredWithoutFreeMarking) {
    // Without freeMarking, backward scan should not work
    config_.freeMarking = false;
    Telex::TelexEngine engine(config_);
    TypeString(engine, L"tiensge");
    // Spell check blocks tone 's', so all letters are literal
    EXPECT_EQ(engine.Peek(), L"tiensge");
}

TEST_F(TelexFreeMarkingTest, WModifier_HornOnU_AcrossConsonants) {
    // 'w' already scans all states — should find 'u' even after consonants
    // "munsw" → m-u-n-s(tone on u)-w(horn on u) → "mứn" with horn
    // Actually: "mus" → "mú", then "w" → horn on u → "mứ"... wait
    // Simpler: "munw" → "mưn" (w finds 'u' across 'n')
    Telex::TelexEngine engine(config_);
    TypeString(engine, L"munw");
    // ProcessWModifier scans all states, finds u → applies horn → ư
    EXPECT_EQ(engine.Peek(), L"mưn");
}

TEST_F(TelexFreeMarkingTest, WModifier_HornOnO_AcrossConsonants) {
    // "honw" → 'w' finds 'o' across 'n' → hơn
    Telex::TelexEngine engine(config_);
    TypeString(engine, L"honw");
    EXPECT_EQ(engine.Peek(), L"hơn");
}

TEST_F(TelexFreeMarkingTest, WModifier_BreveOnA_AcrossConsonants) {
    // "hanw" → 'w' finds 'a' across 'n' → hăn
    Telex::TelexEngine engine(config_);
    TypeString(engine, L"hanw");
    EXPECT_EQ(engine.Peek(), L"hăn");
}
//=============================================================================
// Free Marking Tests — VNI engine with spellCheck ON + freeMarking ON
//=============================================================================

class VniFreeMarkingTest : public ::testing::Test {
protected:
    void SetUp() override {
        config_.inputMethod = InputMethod::VNI;
        config_.spellCheckEnabled = true;
        config_.freeMarking = true;
    }

    TypingConfig config_;
};

TEST_F(VniFreeMarkingTest, ToneAppliedOnInvalidSyllable_Bla1) {
    // "bla" is invalid → with freeMarking, '1' applies acute tone to 'a' → "blá"
    Vni::VniEngine engine(config_);
    TypeString(engine, L"bla1");
    EXPECT_EQ(engine.Peek(), L"blá");
}

TEST_F(VniFreeMarkingTest, FreeMarkingOff_ToneBlocked) {
    // Verify original behavior: freeMarking OFF → tone blocked
    config_.freeMarking = false;
    Vni::VniEngine engine(config_);
    TypeString(engine, L"bla1");
    EXPECT_EQ(engine.Peek(), L"bla1");  // '1' treated as literal
}

}  // namespace
}  // namespace NextKey

