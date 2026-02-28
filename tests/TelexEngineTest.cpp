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
    (void)engine_->Commit();  // Discard result, testing post-commit behavior
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

TEST_F(TelexEngineTest, Word_Cua) {
    TypeString(*engine_, L"cuar");
    EXPECT_EQ(engine_->Peek(), L"của");
}

TEST_F(TelexEngineTest, Word_Hoac) {
    TypeString(*engine_, L"hoacwj");
    EXPECT_EQ(engine_->Peek(), L"hoặc");
}

// ============================================================================
// TRIPHTHONG TESTS (From test-specification.md)
// ============================================================================

TEST_F(TelexEngineTest, Triphthong_IEU_Acute) {
    TypeString(*engine_, L"ieeus");  // iếu
    EXPECT_EQ(engine_->Peek(), L"iếu");
}

TEST_F(TelexEngineTest, Triphthong_IEU_Grave) {
    TypeString(*engine_, L"ieeuf");  // iều
    EXPECT_EQ(engine_->Peek(), L"iều");
}

TEST_F(TelexEngineTest, Triphthong_YEU_Acute) {
    TypeString(*engine_, L"yeeus");  // yếu
    EXPECT_EQ(engine_->Peek(), L"yếu");
}

TEST_F(TelexEngineTest, Triphthong_UOI_Acute) {
    TypeString(*engine_, L"uoois");  // uối (muối)
    EXPECT_EQ(engine_->Peek(), L"uối");
}

TEST_F(TelexEngineTest, Triphthong_UOI_Hook) {
    TypeString(*engine_, L"uooir");  // uổi (tuổi)
    EXPECT_EQ(engine_->Peek(), L"uổi");
}

TEST_F(TelexEngineTest, Triphthong_UOI_Horn) {
    TypeString(*engine_, L"uowis");  // ưới (tưới)
    EXPECT_EQ(engine_->Peek(), L"ưới");
}

TEST_F(TelexEngineTest, Triphthong_UOI_Horn_Grave) {
    TypeString(*engine_, L"uowif");  // ười (mười)
    EXPECT_EQ(engine_->Peek(), L"ười");
}

TEST_F(TelexEngineTest, Triphthong_OAI_Grave) {
    TypeString(*engine_, L"oaif");  // oài (hoài)
    EXPECT_EQ(engine_->Peek(), L"oài");
}

TEST_F(TelexEngineTest, Triphthong_OAY_Acute) {
    TypeString(*engine_, L"oays");  // oáy (xoáy)
    EXPECT_EQ(engine_->Peek(), L"oáy");
}

TEST_F(TelexEngineTest, Triphthong_UYE_Dot) {
    TypeString(*engine_, L"uyeetj");  // uyệt (tuyệt)
    EXPECT_EQ(engine_->Peek(), L"uyệt");
}

// ============================================================================
// RISING DIPHTHONG TESTS (Tone on SECOND vowel)
// ============================================================================

TEST_F(TelexEngineTest, RisingDiphthong_OA_Grave) {
    TypeString(*engine_, L"oaf");  // òa (classic: tone on first)
    EXPECT_EQ(engine_->Peek(), L"òa");
}

TEST_F(TelexEngineTest, RisingDiphthong_OA_Acute) {
    TypeString(*engine_, L"oas");  // óa (classic: tone on first)
    EXPECT_EQ(engine_->Peek(), L"óa");
}

TEST_F(TelexEngineTest, RisingDiphthong_OE_Tilde) {
    TypeString(*engine_, L"oex");  // õe (classic: tone on first)
    EXPECT_EQ(engine_->Peek(), L"õe");
}

TEST_F(TelexEngineTest, RisingDiphthong_OE_Grave) {
    TypeString(*engine_, L"oef");  // òe (classic: tone on first)
    EXPECT_EQ(engine_->Peek(), L"òe");
}

TEST_F(TelexEngineTest, RisingDiphthong_UY_Grave) {
    TypeString(*engine_, L"uyf");  // uỳ (quỳ)
    EXPECT_EQ(engine_->Peek(), L"uỳ");
}

TEST_F(TelexEngineTest, RisingDiphthong_Hoa) {
    TypeString(*engine_, L"hoaf");  // hòa (classic: tone on first)
    EXPECT_EQ(engine_->Peek(), L"hòa");
}

TEST_F(TelexEngineTest, RisingDiphthong_Quy) {
    TypeString(*engine_, L"quyf");  // quỳ
    EXPECT_EQ(engine_->Peek(), L"quỳ");
}

// ============================================================================
// W-MODIFIER PRIORITY TESTS (7 levels from spec)
// ============================================================================

TEST_F(TelexEngineTest, WPriority_1_UA_Horn) {
    // Priority 1: ua → ưa (horn on u, not breve on a)
    TypeString(*engine_, L"muaw");
    EXPECT_EQ(engine_->Peek(), L"mưa");
}

TEST_F(TelexEngineTest, WPriority_1_UA_Horn_2) {
    TypeString(*engine_, L"cuaw");
    EXPECT_EQ(engine_->Peek(), L"cưa");
}

TEST_F(TelexEngineTest, WPriority_1_UA_Horn_3) {
    TypeString(*engine_, L"thuaw");
    EXPECT_EQ(engine_->Peek(), L"thưa");
}

TEST_F(TelexEngineTest, WPriority_2_OA_Breve) {
    // Priority 2: oa → oă (breve on a)
    TypeString(*engine_, L"hoaw");
    EXPECT_EQ(engine_->Peek(), L"hoă");
}

TEST_F(TelexEngineTest, WPriority_2_OA_Breve_2) {
    TypeString(*engine_, L"toaw");
    EXPECT_EQ(engine_->Peek(), L"toă");
}

TEST_F(TelexEngineTest, WPriority_3_SingleU) {
    // Priority 3: standalone u → ư
    TypeString(*engine_, L"tuw");
    EXPECT_EQ(engine_->Peek(), L"tư");
}

TEST_F(TelexEngineTest, WPriority_4_SingleO) {
    // Priority 4: standalone o → ơ (not in oa pattern)
    TypeString(*engine_, L"tow");
    EXPECT_EQ(engine_->Peek(), L"tơ");
}

TEST_F(TelexEngineTest, WPriority_5_SingleA) {
    // Priority 5: standalone a → ă
    TypeString(*engine_, L"taw");
    EXPECT_EQ(engine_->Peek(), L"tă");
}

TEST_F(TelexEngineTest, WPriority_6_Escape_U) {
    // Priority 6: escape - clear existing horn
    TypeString(*engine_, L"uww");
    EXPECT_EQ(engine_->Peek(), L"uw");
}

TEST_F(TelexEngineTest, WPriority_6_Escape_O) {
    TypeString(*engine_, L"oww");
    EXPECT_EQ(engine_->Peek(), L"ow");
}

TEST_F(TelexEngineTest, WPriority_6_Escape_A) {
    TypeString(*engine_, L"aww");
    EXPECT_EQ(engine_->Peek(), L"aw");
}

// ============================================================================
// TONE PLACEMENT PRIORITY TESTS
// ============================================================================

TEST_F(TelexEngineTest, TonePriority_1_Horn_Last) {
    // Priority 1: Last horn vowel (ơ, ư)
    TypeString(*engine_, L"uowf");  // tone on ơ (last horn)
    EXPECT_EQ(engine_->Peek(), L"uờ");
}

TEST_F(TelexEngineTest, TonePriority_1_UO_Cluster) {
    TypeString(*engine_, L"dduowcj");  // được
    EXPECT_EQ(engine_->Peek(), L"được");
}

TEST_F(TelexEngineTest, TonePriority_1_Nguoi) {
    TypeString(*engine_, L"nguowif");  // người
    EXPECT_EQ(engine_->Peek(), L"người");
}

TEST_F(TelexEngineTest, TonePriority_1_Muoi) {
    TypeString(*engine_, L"muowif");  // mười
    EXPECT_EQ(engine_->Peek(), L"mười");
}

TEST_F(TelexEngineTest, TonePriority_2_Circumflex) {
    // Priority 2: Modified vowel (â, ê, ô, ă)
    TypeString(*engine_, L"caaps");  // cấp
    EXPECT_EQ(engine_->Peek(), L"cấp");
}

TEST_F(TelexEngineTest, TonePriority_2_Breve) {
    TypeString(*engine_, L"awcs");  // ắc
    EXPECT_EQ(engine_->Peek(), L"ắc");
}

TEST_F(TelexEngineTest, TonePriority_3_Falling_AO) {
    // Priority 3: Diphthong rules
    TypeString(*engine_, L"caos");  // cáo
    EXPECT_EQ(engine_->Peek(), L"cáo");
}

TEST_F(TelexEngineTest, TonePriority_3_Rising_OA) {
    TypeString(*engine_, L"hoas");  // hóa (classic: tone on first)
    EXPECT_EQ(engine_->Peek(), L"hóa");
}

TEST_F(TelexEngineTest, TonePriority_4_Default) {
    // Priority 4: Default (rightmost)
    TypeString(*engine_, L"mas");  // má
    EXPECT_EQ(engine_->Peek(), L"má");
}

// ============================================================================
// TONE RELOCATION TESTS
// ============================================================================

TEST_F(TelexEngineTest, ToneReloc_Cua_W) {
    // của + w → cửa (tone relocates from u to ư)
    TypeString(*engine_, L"cuarw");
    EXPECT_EQ(engine_->Peek(), L"cửa");
}

TEST_F(TelexEngineTest, ToneReloc_Lua) {
    // lửa
    TypeString(*engine_, L"luwar");
    EXPECT_EQ(engine_->Peek(), L"lửa");
}

// ============================================================================
// ADDITIONAL ESCAPE TESTS
// ============================================================================

TEST_F(TelexEngineTest, Escape_UWAW) {
    // ư(uw) → ưa(uwa) → uaw (w escapes horn)
    TypeString(*engine_, L"uwaw");
    EXPECT_EQ(engine_->Peek(), L"uaw");
}

// ============================================================================
// REAL WORD TESTS (From spec)
// ============================================================================

TEST_F(TelexEngineTest, RealWord_Nuoc) {
    TypeString(*engine_, L"nuowcs");  // nước
    EXPECT_EQ(engine_->Peek(), L"nước");
}

TEST_F(TelexEngineTest, RealWord_Yeu) {
    TypeString(*engine_, L"yeeu");  // yêu
    EXPECT_EQ(engine_->Peek(), L"yêu");
}

TEST_F(TelexEngineTest, RealWord_Yeu_Tone) {
    TypeString(*engine_, L"yeeus");  // yếu
    EXPECT_EQ(engine_->Peek(), L"yếu");
}

TEST_F(TelexEngineTest, RealWord_Tieu) {
    TypeString(*engine_, L"tieeur");  // tiểu
    EXPECT_EQ(engine_->Peek(), L"tiểu");
}

TEST_F(TelexEngineTest, RealWord_Tuyet) {
    TypeString(*engine_, L"tuyeetj");  // tuyệt
    EXPECT_EQ(engine_->Peek(), L"tuyệt");
}

TEST_F(TelexEngineTest, RealWord_Nguyen) {
    TypeString(*engine_, L"nguyeen");  // nguyên
    EXPECT_EQ(engine_->Peek(), L"nguyên");
}

TEST_F(TelexEngineTest, RealWord_Khuya) {
    TypeString(*engine_, L"khuya");  // khuya
    EXPECT_EQ(engine_->Peek(), L"khuya");
}

TEST_F(TelexEngineTest, RealWord_Hoai) {
    TypeString(*engine_, L"hoaif");  // hoài
    EXPECT_EQ(engine_->Peek(), L"hoài");
}

TEST_F(TelexEngineTest, RealWord_Xoay) {
    TypeString(*engine_, L"xoays");  // xoáy
    EXPECT_EQ(engine_->Peek(), L"xoáy");
}

TEST_F(TelexEngineTest, RealWord_Tuoi) {
    TypeString(*engine_, L"tuooir");  // tuổi
    EXPECT_EQ(engine_->Peek(), L"tuổi");
}

TEST_F(TelexEngineTest, RealWord_Muoi) {
    TypeString(*engine_, L"muoois");  // muối
    EXPECT_EQ(engine_->Peek(), L"muối");
}

TEST_F(TelexEngineTest, RealWord_Toan) {
    TypeString(*engine_, L"toawnf");  // toằn (oă + grave)
    EXPECT_EQ(engine_->Peek(), L"toằn");
}

TEST_F(TelexEngineTest, RealWord_Hoan) {
    TypeString(*engine_, L"hoawnf");  // hoằn
    EXPECT_EQ(engine_->Peek(), L"hoằn");
}

TEST_F(TelexEngineTest, RealWord_An) {
    TypeString(*engine_, L"awn");  // ăn
    EXPECT_EQ(engine_->Peek(), L"ăn");
}

TEST_F(TelexEngineTest, RealWord_Lang) {
    TypeString(*engine_, L"lawngj");  // lặng
    EXPECT_EQ(engine_->Peek(), L"lặng");
}

TEST_F(TelexEngineTest, RealWord_Bat) {
    TypeString(*engine_, L"bawts");  // bắt
    EXPECT_EQ(engine_->Peek(), L"bắt");
}

TEST_F(TelexEngineTest, RealWord_Dac) {
    TypeString(*engine_, L"ddawcj");  // đặc
    EXPECT_EQ(engine_->Peek(), L"đặc");
}

TEST_F(TelexEngineTest, RealWord_Thang) {
    TypeString(*engine_, L"thawngs");  // thắng
    EXPECT_EQ(engine_->Peek(), L"thắng");
}

TEST_F(TelexEngineTest, RealWord_Mua) {
    TypeString(*engine_, L"muaw");  // mưa
    EXPECT_EQ(engine_->Peek(), L"mưa");
}

TEST_F(TelexEngineTest, RealWord_Thua) {
    TypeString(*engine_, L"thuaw");  // thưa
    EXPECT_EQ(engine_->Peek(), L"thưa");
}

TEST_F(TelexEngineTest, RealWord_Giua_Full) {
    TypeString(*engine_, L"giuwax");  // giữa
    EXPECT_EQ(engine_->Peek(), L"giữa");
}

TEST_F(TelexEngineTest, RealWord_Lua) {
    TypeString(*engine_, L"luwar");  // lửa
    EXPECT_EQ(engine_->Peek(), L"lửa");
}

TEST_F(TelexEngineTest, RealWord_Su) {
    TypeString(*engine_, L"suwr");  // sử
    EXPECT_EQ(engine_->Peek(), L"sử");
}

TEST_F(TelexEngineTest, RealWord_Pho) {
    TypeString(*engine_, L"phowr");  // phở
    EXPECT_EQ(engine_->Peek(), L"phở");
}

TEST_F(TelexEngineTest, RealWord_Tho) {
    TypeString(*engine_, L"thow");  // thơ
    EXPECT_EQ(engine_->Peek(), L"thơ");
}

TEST_F(TelexEngineTest, RealWord_Son) {
    TypeString(*engine_, L"sown");  // sơn
    EXPECT_EQ(engine_->Peek(), L"sơn");
}

TEST_F(TelexEngineTest, RealWord_Hoi) {
    TypeString(*engine_, L"howi");  // hơi
    EXPECT_EQ(engine_->Peek(), L"hơi");
}

// ============================================================================
// FALLING DIPHTHONG TESTS
// ============================================================================

TEST_F(TelexEngineTest, FallingDiphthong_AI_Acute) {
    TypeString(*engine_, L"ais");
    EXPECT_EQ(engine_->Peek(), L"ái");
}

TEST_F(TelexEngineTest, FallingDiphthong_AO_Grave) {
    TypeString(*engine_, L"aof");
    EXPECT_EQ(engine_->Peek(), L"ào");
}

TEST_F(TelexEngineTest, FallingDiphthong_AU_Hook) {
    TypeString(*engine_, L"aur");
    EXPECT_EQ(engine_->Peek(), L"ảu");
}

TEST_F(TelexEngineTest, FallingDiphthong_AY_Grave) {
    TypeString(*engine_, L"ayf");
    EXPECT_EQ(engine_->Peek(), L"ày");
}

TEST_F(TelexEngineTest, FallingDiphthong_AU_Circum) {
    TypeString(*engine_, L"aaus");  // ấu
    EXPECT_EQ(engine_->Peek(), L"ấu");
}

TEST_F(TelexEngineTest, FallingDiphthong_AY_Circum) {
    TypeString(*engine_, L"aays");  // ấy
    EXPECT_EQ(engine_->Peek(), L"ấy");
}

TEST_F(TelexEngineTest, FallingDiphthong_EO_Hook) {
    TypeString(*engine_, L"eor");
    EXPECT_EQ(engine_->Peek(), L"ẻo");
}

TEST_F(TelexEngineTest, FallingDiphthong_EU_Circum) {
    TypeString(*engine_, L"eeus");  // ếu
    EXPECT_EQ(engine_->Peek(), L"ếu");
}

TEST_F(TelexEngineTest, FallingDiphthong_IU_Dot) {
    TypeString(*engine_, L"iuj");  // ịu (dịu)
    EXPECT_EQ(engine_->Peek(), L"ịu");
}

TEST_F(TelexEngineTest, FallingDiphthong_OI_Circum) {
    TypeString(*engine_, L"oois");  // ối (tối)
    EXPECT_EQ(engine_->Peek(), L"ối");
}

TEST_F(TelexEngineTest, FallingDiphthong_OI_Horn) {
    TypeString(*engine_, L"owis");  // ới (trời)
    EXPECT_EQ(engine_->Peek(), L"ới");
}

TEST_F(TelexEngineTest, FallingDiphthong_UI_Horn) {
    TypeString(*engine_, L"uwix");  // ữi (gửi)
    EXPECT_EQ(engine_->Peek(), L"ữi");
}

// ============================================================================
// EDGE CASE TESTS
// ============================================================================

TEST_F(TelexEngineTest, EdgeCase_ToneReplace) {
    // Different tone replaces: á → à
    TypeString(*engine_, L"asf");
    EXPECT_EQ(engine_->Peek(), L"à");
}

TEST_F(TelexEngineTest, EdgeCase_ToneReplace_2) {
    // á → ả
    TypeString(*engine_, L"asr");
    EXPECT_EQ(engine_->Peek(), L"ả");
}

TEST_F(TelexEngineTest, EdgeCase_GI_Plus_U) {
    TypeString(*engine_, L"giuw");
    EXPECT_EQ(engine_->Peek(), L"giư");
}

TEST_F(TelexEngineTest, EdgeCase_QU_Plus_E) {
    TypeString(*engine_, L"quew");
    EXPECT_EQ(engine_->Peek(), L"quew");
}

TEST_F(TelexEngineTest, EdgeCase_AllVowels) {
    TypeString(*engine_, L"aeiou");
    EXPECT_EQ(engine_->Peek(), L"aeiou");
}

// ============================================================================
// COMPLETE TONE TESTS - All vowels × All tones
// ============================================================================

// Acute (s) - remaining vowels
TEST_F(TelexEngineTest, Tone_Acute_Circumflex_A) {
    TypeString(*engine_, L"aas");
    EXPECT_EQ(engine_->Peek(), L"ấ");
}

TEST_F(TelexEngineTest, Tone_Acute_Breve_A) {
    TypeString(*engine_, L"aws");
    EXPECT_EQ(engine_->Peek(), L"ắ");
}

TEST_F(TelexEngineTest, Tone_Acute_Circumflex_E) {
    TypeString(*engine_, L"ees");
    EXPECT_EQ(engine_->Peek(), L"ế");
}

TEST_F(TelexEngineTest, Tone_Acute_Circumflex_O) {
    TypeString(*engine_, L"oos");
    EXPECT_EQ(engine_->Peek(), L"ố");
}

TEST_F(TelexEngineTest, Tone_Acute_Horn_O) {
    TypeString(*engine_, L"ows");
    EXPECT_EQ(engine_->Peek(), L"ớ");
}

TEST_F(TelexEngineTest, Tone_Acute_Horn_U) {
    TypeString(*engine_, L"uws");
    EXPECT_EQ(engine_->Peek(), L"ứ");
}

// Grave (f) - all vowels
TEST_F(TelexEngineTest, Tone_Grave_Circumflex_A) {
    TypeString(*engine_, L"aaf");
    EXPECT_EQ(engine_->Peek(), L"ầ");
}

TEST_F(TelexEngineTest, Tone_Grave_Breve_A) {
    TypeString(*engine_, L"awf");
    EXPECT_EQ(engine_->Peek(), L"ằ");
}

TEST_F(TelexEngineTest, Tone_Grave_Circumflex_E) {
    TypeString(*engine_, L"eef");
    EXPECT_EQ(engine_->Peek(), L"ề");
}

TEST_F(TelexEngineTest, Tone_Grave_I) {
    TypeString(*engine_, L"if");
    EXPECT_EQ(engine_->Peek(), L"ì");
}

TEST_F(TelexEngineTest, Tone_Grave_Circumflex_O) {
    TypeString(*engine_, L"oof");
    EXPECT_EQ(engine_->Peek(), L"ồ");
}

TEST_F(TelexEngineTest, Tone_Grave_Horn_O) {
    TypeString(*engine_, L"owf");
    EXPECT_EQ(engine_->Peek(), L"ờ");
}

TEST_F(TelexEngineTest, Tone_Grave_U) {
    TypeString(*engine_, L"uf");
    EXPECT_EQ(engine_->Peek(), L"ù");
}

TEST_F(TelexEngineTest, Tone_Grave_Horn_U) {
    TypeString(*engine_, L"uwf");
    EXPECT_EQ(engine_->Peek(), L"ừ");
}

TEST_F(TelexEngineTest, Tone_Grave_Y) {
    TypeString(*engine_, L"yf");
    EXPECT_EQ(engine_->Peek(), L"ỳ");
}

// Hook (r) - all vowels
TEST_F(TelexEngineTest, Tone_Hook_Circumflex_A) {
    TypeString(*engine_, L"aar");
    EXPECT_EQ(engine_->Peek(), L"ẩ");
}

TEST_F(TelexEngineTest, Tone_Hook_Breve_A) {
    TypeString(*engine_, L"awr");
    EXPECT_EQ(engine_->Peek(), L"ẳ");
}

TEST_F(TelexEngineTest, Tone_Hook_E) {
    TypeString(*engine_, L"er");
    EXPECT_EQ(engine_->Peek(), L"ẻ");
}

TEST_F(TelexEngineTest, Tone_Hook_Circumflex_E) {
    TypeString(*engine_, L"eer");
    EXPECT_EQ(engine_->Peek(), L"ể");
}

TEST_F(TelexEngineTest, Tone_Hook_I) {
    TypeString(*engine_, L"ir");
    EXPECT_EQ(engine_->Peek(), L"ỉ");
}

TEST_F(TelexEngineTest, Tone_Hook_Circumflex_O) {
    TypeString(*engine_, L"oor");
    EXPECT_EQ(engine_->Peek(), L"ổ");
}

TEST_F(TelexEngineTest, Tone_Hook_Horn_O) {
    TypeString(*engine_, L"owr");
    EXPECT_EQ(engine_->Peek(), L"ở");
}

TEST_F(TelexEngineTest, Tone_Hook_U) {
    TypeString(*engine_, L"ur");
    EXPECT_EQ(engine_->Peek(), L"ủ");
}

TEST_F(TelexEngineTest, Tone_Hook_Horn_U) {
    TypeString(*engine_, L"uwr");
    EXPECT_EQ(engine_->Peek(), L"ử");
}

TEST_F(TelexEngineTest, Tone_Hook_Y) {
    TypeString(*engine_, L"yr");
    EXPECT_EQ(engine_->Peek(), L"ỷ");
}

// Tilde (x) - all vowels
TEST_F(TelexEngineTest, Tone_Tilde_Circumflex_A) {
    TypeString(*engine_, L"aax");
    EXPECT_EQ(engine_->Peek(), L"ẫ");
}

TEST_F(TelexEngineTest, Tone_Tilde_Breve_A) {
    TypeString(*engine_, L"awx");
    EXPECT_EQ(engine_->Peek(), L"ẵ");
}

TEST_F(TelexEngineTest, Tone_Tilde_E) {
    TypeString(*engine_, L"ex");
    EXPECT_EQ(engine_->Peek(), L"ẽ");
}

TEST_F(TelexEngineTest, Tone_Tilde_Circumflex_E) {
    TypeString(*engine_, L"eex");
    EXPECT_EQ(engine_->Peek(), L"ễ");
}

TEST_F(TelexEngineTest, Tone_Tilde_I) {
    TypeString(*engine_, L"ix");
    EXPECT_EQ(engine_->Peek(), L"ĩ");
}

TEST_F(TelexEngineTest, Tone_Tilde_O) {
    TypeString(*engine_, L"ox");
    EXPECT_EQ(engine_->Peek(), L"õ");
}

TEST_F(TelexEngineTest, Tone_Tilde_Circumflex_O) {
    TypeString(*engine_, L"oox");
    EXPECT_EQ(engine_->Peek(), L"ỗ");
}

TEST_F(TelexEngineTest, Tone_Tilde_Horn_O) {
    TypeString(*engine_, L"owx");
    EXPECT_EQ(engine_->Peek(), L"ỡ");
}

TEST_F(TelexEngineTest, Tone_Tilde_Horn_U) {
    TypeString(*engine_, L"uwx");
    EXPECT_EQ(engine_->Peek(), L"ữ");
}

TEST_F(TelexEngineTest, Tone_Tilde_Y) {
    TypeString(*engine_, L"yx");
    EXPECT_EQ(engine_->Peek(), L"ỹ");
}

// Dot below (j) - all vowels
TEST_F(TelexEngineTest, Tone_Dot_Circumflex_A) {
    TypeString(*engine_, L"aaj");
    EXPECT_EQ(engine_->Peek(), L"ậ");
}

TEST_F(TelexEngineTest, Tone_Dot_Breve_A) {
    TypeString(*engine_, L"awj");
    EXPECT_EQ(engine_->Peek(), L"ặ");
}

TEST_F(TelexEngineTest, Tone_Dot_Circumflex_E) {
    TypeString(*engine_, L"eej");
    EXPECT_EQ(engine_->Peek(), L"ệ");
}

TEST_F(TelexEngineTest, Tone_Dot_I) {
    TypeString(*engine_, L"ij");
    EXPECT_EQ(engine_->Peek(), L"ị");
}

TEST_F(TelexEngineTest, Tone_Dot_Circumflex_O) {
    TypeString(*engine_, L"ooj");
    EXPECT_EQ(engine_->Peek(), L"ộ");
}

TEST_F(TelexEngineTest, Tone_Dot_Horn_O) {
    TypeString(*engine_, L"owj");
    EXPECT_EQ(engine_->Peek(), L"ợ");
}

TEST_F(TelexEngineTest, Tone_Dot_U) {
    TypeString(*engine_, L"uj");
    EXPECT_EQ(engine_->Peek(), L"ụ");
}

TEST_F(TelexEngineTest, Tone_Dot_Horn_U) {
    TypeString(*engine_, L"uwj");
    EXPECT_EQ(engine_->Peek(), L"ự");
}

TEST_F(TelexEngineTest, Tone_Dot_Y) {
    TypeString(*engine_, L"yj");
    EXPECT_EQ(engine_->Peek(), L"ỵ");
}

// ============================================================================
// ADDITIONAL ESCAPE TESTS
// ============================================================================

TEST_F(TelexEngineTest, Escape_Grave) {
    TypeString(*engine_, L"aff");
    EXPECT_EQ(engine_->Peek(), L"af");
}

TEST_F(TelexEngineTest, Escape_Hook) {
    TypeString(*engine_, L"arr");
    EXPECT_EQ(engine_->Peek(), L"ar");
}

TEST_F(TelexEngineTest, Escape_Tilde) {
    TypeString(*engine_, L"axx");
    EXPECT_EQ(engine_->Peek(), L"ax");
}

TEST_F(TelexEngineTest, Escape_Dot) {
    TypeString(*engine_, L"ajj");
    EXPECT_EQ(engine_->Peek(), L"aj");
}

TEST_F(TelexEngineTest, Escape_Circumflex_O) {
    TypeString(*engine_, L"ooo");
    EXPECT_EQ(engine_->Peek(), L"oo");
}

TEST_F(TelexEngineTest, Escape_Stroke_Quad) {
    TypeString(*engine_, L"dddd");
    EXPECT_EQ(engine_->Peek(), L"dđ");
}

// ============================================================================
// MIXED CASE TESTS
// ============================================================================

TEST_F(TelexEngineTest, MixedCase_Circumflex_Aa) {
    TypeString(*engine_, L"Aa");
    EXPECT_EQ(engine_->Peek(), L"Â");
}

TEST_F(TelexEngineTest, MixedCase_Circumflex_aA) {
    TypeString(*engine_, L"aA");
    EXPECT_EQ(engine_->Peek(), L"â");
}

TEST_F(TelexEngineTest, MixedCase_Breve_Aw) {
    TypeString(*engine_, L"Aw");
    EXPECT_EQ(engine_->Peek(), L"Ă");
}

TEST_F(TelexEngineTest, MixedCase_Horn_Ow) {
    TypeString(*engine_, L"Ow");
    EXPECT_EQ(engine_->Peek(), L"Ơ");
}

TEST_F(TelexEngineTest, MixedCase_Horn_Uw) {
    TypeString(*engine_, L"Uw");
    EXPECT_EQ(engine_->Peek(), L"Ư");
}

TEST_F(TelexEngineTest, MixedCase_Stroke_Dd) {
    TypeString(*engine_, L"Dd");
    EXPECT_EQ(engine_->Peek(), L"Đ");
}

// ============================================================================
// ADDITIONAL FALLING DIPHTHONG TESTS
// ============================================================================

TEST_F(TelexEngineTest, FallingDiphthong_AI_Hook) {
    TypeString(*engine_, L"air");
    EXPECT_EQ(engine_->Peek(), L"ải");
}

TEST_F(TelexEngineTest, FallingDiphthong_AO_Acute) {
    TypeString(*engine_, L"aos");
    EXPECT_EQ(engine_->Peek(), L"áo");
}

TEST_F(TelexEngineTest, FallingDiphthong_AU_Acute) {
    TypeString(*engine_, L"aus");
    EXPECT_EQ(engine_->Peek(), L"áu");
}

TEST_F(TelexEngineTest, FallingDiphthong_AY_Acute) {
    TypeString(*engine_, L"ays");
    EXPECT_EQ(engine_->Peek(), L"áy");
}

TEST_F(TelexEngineTest, FallingDiphthong_EO_Acute) {
    TypeString(*engine_, L"eos");
    EXPECT_EQ(engine_->Peek(), L"éo");
}

TEST_F(TelexEngineTest, FallingDiphthong_OI_Dot) {
    TypeString(*engine_, L"oij");
    EXPECT_EQ(engine_->Peek(), L"ọi");
}

TEST_F(TelexEngineTest, FallingDiphthong_UI_Acute) {
    TypeString(*engine_, L"uis");
    EXPECT_EQ(engine_->Peek(), L"úi");
}

TEST_F(TelexEngineTest, FallingDiphthong_UI_Dot) {
    TypeString(*engine_, L"uij");
    EXPECT_EQ(engine_->Peek(), L"ụi");
}

// ============================================================================
// ADDITIONAL RISING DIPHTHONG TESTS
// ============================================================================

TEST_F(TelexEngineTest, RisingDiphthong_OA_Hook) {
    TypeString(*engine_, L"oar");
    EXPECT_EQ(engine_->Peek(), L"ỏa");
}

TEST_F(TelexEngineTest, RisingDiphthong_UY_Acute) {
    TypeString(*engine_, L"uys");
    EXPECT_EQ(engine_->Peek(), L"uý");
}

TEST_F(TelexEngineTest, RisingDiphthong_UY_Dot) {
    TypeString(*engine_, L"uyj");
    EXPECT_EQ(engine_->Peek(), L"uỵ");
}

// ============================================================================
// ADDITIONAL REAL WORD TESTS
// ============================================================================

TEST_F(TelexEngineTest, RealWord_Quoc) {
    TypeString(*engine_, L"quoocs");  // quốc
    EXPECT_EQ(engine_->Peek(), L"quốc");
}

TEST_F(TelexEngineTest, RealWord_Gia) {
    TypeString(*engine_, L"gia");  // gia
    EXPECT_EQ(engine_->Peek(), L"gia");
}

TEST_F(TelexEngineTest, RealWord_Dinh) {
    TypeString(*engine_, L"ddinh");  // định → wait đình?
    EXPECT_EQ(engine_->Peek(), L"đinh");
}

TEST_F(TelexEngineTest, RealWord_Tra) {
    TypeString(*engine_, L"traf");  // trà
    EXPECT_EQ(engine_->Peek(), L"trà");
}

TEST_F(TelexEngineTest, RealWord_Cafe) {
    TypeString(*engine_, L"cafs");  // cà phê → cáf?
    EXPECT_EQ(engine_->Peek(), L"cá");  // just testing tone on a
}

TEST_F(TelexEngineTest, RealWord_Banh) {
    TypeString(*engine_, L"banhf");  // bành
    EXPECT_EQ(engine_->Peek(), L"bành");
}

TEST_F(TelexEngineTest, RealWord_Mi) {
    TypeString(*engine_, L"mif");  // mì
    EXPECT_EQ(engine_->Peek(), L"mì");
}

TEST_F(TelexEngineTest, RealWord_Pho_Full) {
    TypeString(*engine_, L"phor");  // phỏ
    EXPECT_EQ(engine_->Peek(), L"phỏ");
}

TEST_F(TelexEngineTest, RealWord_Bo) {
    TypeString(*engine_, L"bof");  // bò
    EXPECT_EQ(engine_->Peek(), L"bò");
}

TEST_F(TelexEngineTest, RealWord_Ga) {
    TypeString(*engine_, L"gaf");  // gà
    EXPECT_EQ(engine_->Peek(), L"gà");
}

TEST_F(TelexEngineTest, RealWord_Heo) {
    TypeString(*engine_, L"heor");  // hẻo
    EXPECT_EQ(engine_->Peek(), L"hẻo");
}

TEST_F(TelexEngineTest, RealWord_Ca) {
    TypeString(*engine_, L"cas");  // cá
    EXPECT_EQ(engine_->Peek(), L"cá");
}

TEST_F(TelexEngineTest, RealWord_Trung) {
    TypeString(*engine_, L"trungj");  // trụng
    EXPECT_EQ(engine_->Peek(), L"trụng");
}

TEST_F(TelexEngineTest, RealWord_Xoi) {
    TypeString(*engine_, L"xooi");  // xôi
    EXPECT_EQ(engine_->Peek(), L"xôi");
}

TEST_F(TelexEngineTest, RealWord_Com) {
    TypeString(*engine_, L"coom");  // cơm
    EXPECT_EQ(engine_->Peek(), L"côm");
}

TEST_F(TelexEngineTest, RealWord_Com_Horn) {
    TypeString(*engine_, L"cowm");  // cơm
    EXPECT_EQ(engine_->Peek(), L"cơm");
}

TEST_F(TelexEngineTest, RealWord_Nuong) {
    TypeString(*engine_, L"nuowng");  // nương
    EXPECT_EQ(engine_->Peek(), L"nương");
}

TEST_F(TelexEngineTest, RealWord_Ran) {
    TypeString(*engine_, L"rawn");  // răn
    EXPECT_EQ(engine_->Peek(), L"răn");
}

TEST_F(TelexEngineTest, RealWord_Chien) {
    TypeString(*engine_, L"chieens");  // chiến
    EXPECT_EQ(engine_->Peek(), L"chiến");
}

TEST_F(TelexEngineTest, RealWord_Hap) {
    TypeString(*engine_, L"haaps");  // hấp
    EXPECT_EQ(engine_->Peek(), L"hấp");
}

TEST_F(TelexEngineTest, Freestyles) {
    TypeString(*engine_, L"lefeeee");  // lefeeee
    EXPECT_EQ(engine_->Peek(), L"lèee");
}

TEST_F(TelexEngineTest, TestUych) {
    TypeString(*engine_, L"huychj");  // lefeeee
    EXPECT_EQ(engine_->Peek(), L"huỵch");
}
// ============================================================================
// SIMPLE TELEX TESTS
// Simple Telex: standalone 'w' is literal, 'w' after a/o/u vowel is modifier
// ============================================================================

class SimpleTelexTest : public ::testing::Test {
protected:
    void SetUp() override {
        config_.inputMethod = InputMethod::SimpleTelex;
        config_.spellCheckEnabled = false;
        config_.optimizeLevel = 0;
        engine_ = std::make_unique<TelexEngine>(config_);
    }

    TypingConfig config_;
    std::unique_ptr<TelexEngine> engine_;
};

TEST_F(SimpleTelexTest, W_Standalone_IsLiteral) {
    TypeString(*engine_, L"w");
    EXPECT_EQ(engine_->Peek(), L"w");
}

TEST_F(SimpleTelexTest, W_AfterConsonant_IsLiteral) {
    TypeString(*engine_, L"tw");
    EXPECT_EQ(engine_->Peek(), L"tw");
}

TEST_F(SimpleTelexTest, W_AfterU_IsModifier) {
    TypeString(*engine_, L"uw");
    EXPECT_EQ(engine_->Peek(), L"ư");
}

TEST_F(SimpleTelexTest, W_AfterO_IsModifier) {
    TypeString(*engine_, L"ow");
    EXPECT_EQ(engine_->Peek(), L"ơ");
}

TEST_F(SimpleTelexTest, W_AfterA_IsBruve) {
    TypeString(*engine_, L"aw");
    EXPECT_EQ(engine_->Peek(), L"ă");
}

TEST_F(SimpleTelexTest, W_InMua_IsModifier) {
    TypeString(*engine_, L"muaw");
    EXPECT_EQ(engine_->Peek(), L"mưa");
}

TEST_F(SimpleTelexTest, W_InDuoc_IsModifier) {
    TypeString(*engine_, L"dduowcj");
    EXPECT_EQ(engine_->Peek(), L"được");
}

TEST_F(SimpleTelexTest, W_AfterI_IsLiteral) {
    // 'i' is a vowel but not a/o/u, so 'w' should be literal
    TypeString(*engine_, L"iw");
    EXPECT_EQ(engine_->Peek(), L"iw");
}

TEST_F(SimpleTelexTest, W_AfterE_IsLiteral) {
    TypeString(*engine_, L"ew");
    EXPECT_EQ(engine_->Peek(), L"ew");
}

TEST_F(SimpleTelexTest, Circumflex_StillWorks) {
    TypeString(*engine_, L"aa");
    EXPECT_EQ(engine_->Peek(), L"â");
}

TEST_F(SimpleTelexTest, Tone_StillWorks) {
    TypeString(*engine_, L"as");
    EXPECT_EQ(engine_->Peek(), L"á");
}

TEST_F(SimpleTelexTest, DD_StillWorks) {
    TypeString(*engine_, L"dd");
    EXPECT_EQ(engine_->Peek(), L"đ");
}

TEST_F(SimpleTelexTest, RealWord_Duong) {
    TypeString(*engine_, L"dduowng");
    EXPECT_EQ(engine_->Peek(), L"đương");
}

}  // namespace
}  // namespace Telex
}  // namespace NextKey
