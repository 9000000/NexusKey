// NexusKey - TelexEngine Unit Tests
// SPDX-License-Identifier: GPL-3.0-only
// Story 1.2: Comprehensive Telex transformation tests (50+ tests)

#include <gtest/gtest.h>
#include "core/engine/TelexEngine.h"
#include "core/TypingConfig.h"

namespace NextKey {
namespace Telex {
namespace {

// Helper function to type a string into the engine
void TypeString(TelexEngine& engine, const wchar_t* input) {
    for (const wchar_t* p = input; *p; ++p) {
        engine.PushChar(*p);
    }
}

class TelexEngineTest : public ::testing::Test {
protected:
    void SetUp() override {
        config_.inputMethod = InputMethod::Telex;
        config_.spellCheckEnabled = false;
        config_.optimizeLevel = 0;
        engine_ = std::make_unique<TelexEngine>(config_);
    }

    TypingConfig config_;
    std::unique_ptr<TelexEngine> engine_;
};

// ============================================================================
// CIRCUMFLEX TESTS (aa→â, ee→ê, oo→ô)
// ============================================================================

TEST_F(TelexEngineTest, Circumflex_AA_LowerCase) {
    TypeString(*engine_, L"aa");
    EXPECT_EQ(engine_->Peek(), L"â");
}

TEST_F(TelexEngineTest, Circumflex_AA_UpperCase) {
    TypeString(*engine_, L"AA");
    EXPECT_EQ(engine_->Peek(), L"Â");
}

TEST_F(TelexEngineTest, Circumflex_EE_LowerCase) {
    TypeString(*engine_, L"ee");
    EXPECT_EQ(engine_->Peek(), L"ê");
}

TEST_F(TelexEngineTest, Circumflex_EE_UpperCase) {
    TypeString(*engine_, L"EE");
    EXPECT_EQ(engine_->Peek(), L"Ê");
}

TEST_F(TelexEngineTest, Circumflex_OO_LowerCase) {
    TypeString(*engine_, L"oo");
    EXPECT_EQ(engine_->Peek(), L"ô");
}

TEST_F(TelexEngineTest, Circumflex_OO_UpperCase) {
    TypeString(*engine_, L"OO");
    EXPECT_EQ(engine_->Peek(), L"Ô");
}

TEST_F(TelexEngineTest, Circumflex_WithPrefix) {
    TypeString(*engine_, L"coo");
    EXPECT_EQ(engine_->Peek(), L"cô");
}

// ============================================================================
// BREVE TESTS (aw→ă)
// ============================================================================

TEST_F(TelexEngineTest, Breve_AW_LowerCase) {
    TypeString(*engine_, L"aw");
    EXPECT_EQ(engine_->Peek(), L"ă");
}

TEST_F(TelexEngineTest, Breve_AW_UpperCase) {
    TypeString(*engine_, L"AW");
    EXPECT_EQ(engine_->Peek(), L"Ă");
}

TEST_F(TelexEngineTest, Breve_WithPrefix) {
    TypeString(*engine_, L"taw");
    EXPECT_EQ(engine_->Peek(), L"tă");
}

// ============================================================================
// HORN TESTS (ow→ơ, uw→ư)
// ============================================================================

TEST_F(TelexEngineTest, Horn_OW_LowerCase) {
    TypeString(*engine_, L"ow");
    EXPECT_EQ(engine_->Peek(), L"ơ");
}

TEST_F(TelexEngineTest, Horn_OW_UpperCase) {
    TypeString(*engine_, L"OW");
    EXPECT_EQ(engine_->Peek(), L"Ơ");
}

TEST_F(TelexEngineTest, Horn_UW_LowerCase) {
    TypeString(*engine_, L"uw");
    EXPECT_EQ(engine_->Peek(), L"ư");
}

TEST_F(TelexEngineTest, Horn_UW_UpperCase) {
    TypeString(*engine_, L"UW");
    EXPECT_EQ(engine_->Peek(), L"Ư");
}

TEST_F(TelexEngineTest, Horn_UO_DelayedTransform) {
    // uow → uơ (no auto-ươ yet, nothing follows)
    TypeString(*engine_, L"uow");
    EXPECT_EQ(engine_->Peek(), L"uơ");
}

TEST_F(TelexEngineTest, Horn_UO_AutoTransform) {
    // uown → ươn (auto-ươ triggers when 'n' follows 'uơ')
    TypeString(*engine_, L"uown");
    EXPECT_EQ(engine_->Peek(), L"ươn");
}

TEST_F(TelexEngineTest, Horn_UO_AutoTransform_WAfterConsonant) {
    // huonw → hươn (w applied to o, auto-ươ because n already follows)
    TypeString(*engine_, L"huonw");
    EXPECT_EQ(engine_->Peek(), L"hươn");
}

TEST_F(TelexEngineTest, Horn_UA_UndoCircumflex) {
    // giuaaw → giưa (w on u, undo circumflex on a)
    // When 'w' applies horn to 'u' and 'â' follows, undo the circumflex
    TypeString(*engine_, L"giuaaw");
    EXPECT_EQ(engine_->Peek(), L"giưa");
}

TEST_F(TelexEngineTest, Horn_UO_ChainedW) {
    // uow → uơ, then second w applies horn to 'u' → ươ
    TypeString(*engine_, L"uoww");
    EXPECT_EQ(engine_->Peek(), L"ươ");
}

// ============================================================================
// STROKE TESTS (dd→đ)
// ============================================================================

TEST_F(TelexEngineTest, Stroke_DD_LowerCase) {
    TypeString(*engine_, L"dd");
    EXPECT_EQ(engine_->Peek(), L"đ");
}

TEST_F(TelexEngineTest, Stroke_DD_UpperCase) {
    TypeString(*engine_, L"DD");
    EXPECT_EQ(engine_->Peek(), L"Đ");
}

TEST_F(TelexEngineTest, Stroke_WithSuffix) {
    TypeString(*engine_, L"ddi");
    EXPECT_EQ(engine_->Peek(), L"đi");
}

TEST_F(TelexEngineTest, Stroke_NotContinue) {
    TypeString(*engine_, L"did");
    EXPECT_EQ(engine_->Peek(), L"đi");
}
// ============================================================================
// TONE MARKS - Basic Vowels (s=acute, f=grave, r=hook, x=tilde, j=dot)
// ============================================================================

// Acute (s)
TEST_F(TelexEngineTest, Tone_Acute_A) {
    TypeString(*engine_, L"as");
    EXPECT_EQ(engine_->Peek(), L"á");
}

TEST_F(TelexEngineTest, Tone_Acute_E) {
    TypeString(*engine_, L"es");
    EXPECT_EQ(engine_->Peek(), L"é");
}

TEST_F(TelexEngineTest, Tone_Acute_I) {
    TypeString(*engine_, L"is");
    EXPECT_EQ(engine_->Peek(), L"í");
}

TEST_F(TelexEngineTest, Tone_Acute_O) {
    TypeString(*engine_, L"os");
    EXPECT_EQ(engine_->Peek(), L"ó");
}

TEST_F(TelexEngineTest, Tone_Acute_U) {
    TypeString(*engine_, L"us");
    EXPECT_EQ(engine_->Peek(), L"ú");
}

TEST_F(TelexEngineTest, Tone_Acute_Y) {
    TypeString(*engine_, L"ys");
    EXPECT_EQ(engine_->Peek(), L"ý");
}

// Grave (f)
TEST_F(TelexEngineTest, Tone_Grave_A) {
    TypeString(*engine_, L"af");
    EXPECT_EQ(engine_->Peek(), L"à");
}

TEST_F(TelexEngineTest, Tone_Grave_E) {
    TypeString(*engine_, L"ef");
    EXPECT_EQ(engine_->Peek(), L"è");
}

// Hook (r)
TEST_F(TelexEngineTest, Tone_Hook_A) {
    TypeString(*engine_, L"ar");
    EXPECT_EQ(engine_->Peek(), L"ả");
}

TEST_F(TelexEngineTest, Tone_Hook_O) {
    TypeString(*engine_, L"or");
    EXPECT_EQ(engine_->Peek(), L"ỏ");
}

// Tilde (x)
TEST_F(TelexEngineTest, Tone_Tilde_A) {
    TypeString(*engine_, L"ax");
    EXPECT_EQ(engine_->Peek(), L"ã");
}

TEST_F(TelexEngineTest, Tone_Tilde_U) {
    TypeString(*engine_, L"ux");
    EXPECT_EQ(engine_->Peek(), L"ũ");
}

// Dot below (j)
TEST_F(TelexEngineTest, Tone_Dot_A) {
    TypeString(*engine_, L"aj");
    EXPECT_EQ(engine_->Peek(), L"ạ");
}

TEST_F(TelexEngineTest, Tone_Dot_E) {
    TypeString(*engine_, L"ej");
    EXPECT_EQ(engine_->Peek(), L"ẹ");
}

// ============================================================================
// TONE MARKS - Modified Vowels (circumflex, breve, horn)
// ============================================================================

TEST_F(TelexEngineTest, Tone_Circumflex_Acute) {
    TypeString(*engine_, L"aas");
    EXPECT_EQ(engine_->Peek(), L"ấ");
}

TEST_F(TelexEngineTest, Tone_Circumflex_Grave) {
    TypeString(*engine_, L"aaf");
    EXPECT_EQ(engine_->Peek(), L"ầ");
}

TEST_F(TelexEngineTest, Tone_Circumflex_E_Acute) {
    TypeString(*engine_, L"ees");
    EXPECT_EQ(engine_->Peek(), L"ế");
}

TEST_F(TelexEngineTest, Tone_Breve_Acute) {
    TypeString(*engine_, L"aws");
    EXPECT_EQ(engine_->Peek(), L"ắ");
}

TEST_F(TelexEngineTest, Tone_Breve_Grave) {
    TypeString(*engine_, L"awf");
    EXPECT_EQ(engine_->Peek(), L"ằ");
}

TEST_F(TelexEngineTest, Tone_Horn_O_Acute) {
    TypeString(*engine_, L"ows");
    EXPECT_EQ(engine_->Peek(), L"ớ");
}

TEST_F(TelexEngineTest, Tone_Horn_U_Acute) {
    TypeString(*engine_, L"uws");
    EXPECT_EQ(engine_->Peek(), L"ứ");
}

TEST_F(TelexEngineTest, Tone_Horn_U_Grave) {
    TypeString(*engine_, L"uwf");
    EXPECT_EQ(engine_->Peek(), L"ừ");
}

// ============================================================================
// TONE PLACEMENT - Vietnamese vowel pair rules
// ============================================================================

TEST_F(TelexEngineTest, TonePlacement_AO_ToneOnA) {
    TypeString(*engine_, L"aos");
    EXPECT_EQ(engine_->Peek(), L"áo");
}

TEST_F(TelexEngineTest, TonePlacement_EO_ToneOnE) {
    TypeString(*engine_, L"eof");
    EXPECT_EQ(engine_->Peek(), L"èo");
}

TEST_F(TelexEngineTest, TonePlacement_AU_ToneOnA) {
    TypeString(*engine_, L"aur");
    EXPECT_EQ(engine_->Peek(), L"ảu");
}

TEST_F(TelexEngineTest, TonePlacement_AI_ToneOnA) {
    TypeString(*engine_, L"aix");
    EXPECT_EQ(engine_->Peek(), L"ãi");
}

TEST_F(TelexEngineTest, TonePlacement_OI_ToneOnO) {
    TypeString(*engine_, L"oif");
    EXPECT_EQ(engine_->Peek(), L"òi");
}

TEST_F(TelexEngineTest, TonePlacement_UI_ToneOnU) {
    TypeString(*engine_, L"uij");
    EXPECT_EQ(engine_->Peek(), L"ụi");
}

TEST_F(TelexEngineTest, TonePlacement_Chao_Grave) {
    TypeString(*engine_, L"chaof");
    EXPECT_EQ(engine_->Peek(), L"chào");
}

// ============================================================================
// FULL VIETNAMESE WORDS
// ============================================================================

TEST_F(TelexEngineTest, Word_Viet) {
    TypeString(*engine_, L"vieetj");
    EXPECT_EQ(engine_->Peek(), L"việt");
}

TEST_F(TelexEngineTest, Word_Chao) {
    TypeString(*engine_, L"chaof");
    EXPECT_EQ(engine_->Peek(), L"chào");
}

TEST_F(TelexEngineTest, Word_Cam_On) {
    // cảm = c + ả + m, typed as carm (r = hook tone)
    TypeString(*engine_, L"carm");
    EXPECT_EQ(engine_->Peek(), L"cảm");
}

TEST_F(TelexEngineTest, Word_Xin) {
    TypeString(*engine_, L"xin");
    EXPECT_EQ(engine_->Peek(), L"xin");
}

TEST_F(TelexEngineTest, Word_Di) {
    TypeString(*engine_, L"ddi");
    EXPECT_EQ(engine_->Peek(), L"đi");
}

TEST_F(TelexEngineTest, Word_Duong) {
    TypeString(*engine_, L"dduowngf");
    EXPECT_EQ(engine_->Peek(), L"đường");
}

TEST_F(TelexEngineTest, Word_Nguoi) {
    // người = ng + ư + ờ + i
    // Typical typing: nguwowif = ng + uw(→ư) + ow(→ơ) + f(grave) + i
    TypeString(*engine_, L"nguowif");
    EXPECT_EQ(engine_->Peek(), L"người");
}

TEST_F(TelexEngineTest, Word_Huo) {
    // huơ: w only applies to 'o', not auto-ươ since nothing follows
    TypeString(*engine_, L"huow");
    EXPECT_EQ(engine_->Peek(), L"huơ");
}

TEST_F(TelexEngineTest, Word_Huo_WithSecondW) {
    // hươ: second w applies horn to 'u'
    TypeString(*engine_, L"huoww");
    EXPECT_EQ(engine_->Peek(), L"hươ");
}

TEST_F(TelexEngineTest, Word_Hoc) {
    // học = h + ọ + c, typed as hocj (j = dot below)
    TypeString(*engine_, L"hocj");
    EXPECT_EQ(engine_->Peek(), L"học");
}

TEST_F(TelexEngineTest, Word_Duoc_NotContinue) {
    // được = đ + ư + ợ + c, tone on ơ (NOT ư)
    // Typed as: dd + uow + c + j
    TypeString(*engine_, L"duowjdc");
    EXPECT_EQ(engine_->Peek(), L"được");
}

TEST_F(TelexEngineTest, Word_Tieng) {
    TypeString(*engine_, L"tieengs");
    EXPECT_EQ(engine_->Peek(), L"tiếng");
}

TEST_F(TelexEngineTest, Word_Nam) {
    TypeString(*engine_, L"nawm");
    EXPECT_EQ(engine_->Peek(), L"năm");
}

TEST_F(TelexEngineTest, Word_Duoc) {
    // được = đ + ư + ợ + c, tone on ơ (NOT ư)
    // Typed as: dd + uow + c + j
    TypeString(*engine_, L"dduowcj");
    EXPECT_EQ(engine_->Peek(), L"được");
}

// ============================================================================
// BACKSPACE TESTS
// ============================================================================

TEST_F(TelexEngineTest, Backspace_RemovesLastCharacter) {
    TypeString(*engine_, L"vie");
    engine_->Backspace();
    EXPECT_EQ(engine_->Peek(), L"vi");
}

TEST_F(TelexEngineTest, Backspace_AfterCircumflex) {
    TypeString(*engine_, L"aa");  // → â (single state with circumflex)
    engine_->Backspace();         // First backspace removes modifier → a
    EXPECT_EQ(engine_->Peek(), L"a");
    EXPECT_EQ(engine_->Count(), 1u);
    engine_->Backspace();         // Second backspace removes base
    EXPECT_EQ(engine_->Count(), 0u);
}

TEST_F(TelexEngineTest, Backspace_OnEmptyBuffer) {
    engine_->Backspace();  // Should not crash
    EXPECT_EQ(engine_->Count(), 0u);
}

TEST_F(TelexEngineTest, Backspace_AfterTone) {
    TypeString(*engine_, L"as");  // → á (single state with acute tone)
    engine_->Backspace();         // First backspace removes tone → a
    EXPECT_EQ(engine_->Peek(), L"a");
    engine_->Backspace();         // Second backspace removes base
    EXPECT_EQ(engine_->Count(), 0u);
}

TEST_F(TelexEngineTest, Backspace_AfterModifierAndTone) {
    TypeString(*engine_, L"aas"); // → ấ (â with acute)
    engine_->Backspace();         // Removes tone → â
    EXPECT_EQ(engine_->Peek(), L"â");
    engine_->Backspace();         // Removes modifier → a
    EXPECT_EQ(engine_->Peek(), L"a");
    engine_->Backspace();         // Removes base
    EXPECT_EQ(engine_->Count(), 0u);
}

TEST_F(TelexEngineTest, Backspace_MultipleConsecutive) {
    TypeString(*engine_, L"abcde");
    engine_->Backspace();
    engine_->Backspace();
    engine_->Backspace();
    EXPECT_EQ(engine_->Peek(), L"ab");
}

// ============================================================================
// COMMIT AND RESET TESTS
// ============================================================================

TEST_F(TelexEngineTest, Commit_ReturnsCorrectTextAndClears) {
    TypeString(*engine_, L"vieetj");
    std::wstring committed = engine_->Commit();
    
    EXPECT_EQ(committed, L"việt");
    EXPECT_EQ(engine_->Count(), 0u);
    EXPECT_EQ(engine_->Peek(), L"");
}

TEST_F(TelexEngineTest, Reset_ClearsAllState) {
    TypeString(*engine_, L"hello");
    engine_->Reset();
    
    EXPECT_EQ(engine_->Count(), 0u);
    EXPECT_EQ(engine_->Peek(), L"");
    EXPECT_EQ(engine_->GetState(), TelexStates::Valid);
}

TEST_F(TelexEngineTest, AfterCommit_CanTypeNewWord) {
    TypeString(*engine_, L"xin");
    engine_->Commit();
    TypeString(*engine_, L"chaof");
    
    EXPECT_EQ(engine_->Peek(), L"chào");
}

// ============================================================================
// ENGINE INTERFACE TESTS
// ============================================================================

TEST_F(TelexEngineTest, Count_ReturnsCorrectValue) {
    EXPECT_EQ(engine_->Count(), 0u);
    engine_->PushChar(L'a');
    EXPECT_EQ(engine_->Count(), 1u);
    engine_->PushChar(L'b');
    EXPECT_EQ(engine_->Count(), 2u);
}

TEST_F(TelexEngineTest, GetState_IsValidAfterReset) {
    engine_->Reset();
    EXPECT_EQ(engine_->GetState(), TelexStates::Valid);
}

TEST_F(TelexEngineTest, NoGlobalState_MultipleInstances) {
    TelexEngine engine2(config_);
    
    // Use strings without Vietnamese patterns
    TypeString(*engine_, L"abc");
    TypeString(engine2, L"xyz");
    
    EXPECT_EQ(engine_->Peek(), L"abc");
    EXPECT_EQ(engine2.Peek(), L"xyz");
}

// ============================================================================
// ESCAPE TESTS (typing same key twice clears transformation + adds key)
// ============================================================================

TEST_F(TelexEngineTest, Escape_ToneAcute) {
    // "tess" → te(acute on e from s) + s(escape, clears tone, adds s) = "tes"
    TypeString(*engine_, L"tess");
    EXPECT_EQ(engine_->Peek(), L"tes");
}

TEST_F(TelexEngineTest, Escape_ToneAcute_FullWord) {
    // "tesst" → "test" (escape 's' clears tone and adds 's', then 't')
    TypeString(*engine_, L"tesst");
    EXPECT_EQ(engine_->Peek(), L"test");
}

TEST_F(TelexEngineTest, Escape_Circumflex) {
    // "eee" → ê(from ee) + e(escape) = "ee"
    TypeString(*engine_, L"eee");
    EXPECT_EQ(engine_->Peek(), L"ee");
}

TEST_F(TelexEngineTest, Escape_Circumflex_A) {
    // "aaa" → â(from aa) + a(escape) = "aa"
    TypeString(*engine_, L"aaa");
    EXPECT_EQ(engine_->Peek(), L"aa");
}

TEST_F(TelexEngineTest, Escape_Stroke_DD) {
    // "ddd" → đ(from dd) + d(escape) = "dd"
    TypeString(*engine_, L"ddd");
    EXPECT_EQ(engine_->Peek(), L"dd");
}

TEST_F(TelexEngineTest, Escape_Breve_AW) {
    // "aww" → ă(from aw) + w(escape) = "aw"
    TypeString(*engine_, L"aww");
    EXPECT_EQ(engine_->Peek(), L"aw");
}

TEST_F(TelexEngineTest, Escape_Horn_OW) {
    // "oww" → ơ(from ow) + w(escape) = "ow"
    TypeString(*engine_, L"oww");
    EXPECT_EQ(engine_->Peek(), L"ow");
}

TEST_F(TelexEngineTest, Word_Giua_WithTone)  {
    // giữa = g + i + ữ + a (ữ = u with horn and tilde)
    TypeString(*engine_, L"giuawx");
    EXPECT_EQ(engine_->Peek(), L"giữa");
}


// ============================================================================
// EDGE CASES
// ============================================================================

TEST_F(TelexEngineTest, EdgeCase_ConsecutiveTones) {
    // Different tone key replaces existing: á (s=acute) → à (f=grave replaces)
    TypeString(*engine_, L"asf");
    EXPECT_EQ(engine_->Peek(), L"à");
    EXPECT_EQ(engine_->Count(), 1u);
}

TEST_F(TelexEngineTest, EdgeCase_PlainConsonants) {
    TypeString(*engine_, L"bcdgh");
    EXPECT_EQ(engine_->Peek(), L"bcdgh");
}

TEST_F(TelexEngineTest, EdgeCase_WWithoutVowel) {
    TypeString(*engine_, L"w");
    EXPECT_EQ(engine_->Peek(), L"w");
}

TEST_F(TelexEngineTest, RemoveTone_WithTone) {
    TypeString(*engine_, L"tesst");
    EXPECT_EQ(engine_->Peek(), L"test");
}

TEST_F(TelexEngineTest, RemoveCircumflex_WithCircumflex) {
    TypeString(*engine_, L"eee");
    EXPECT_EQ(engine_->Peek(), L"ee");
}

}  // namespace
}  // namespace Telex
}  // namespace NextKey
