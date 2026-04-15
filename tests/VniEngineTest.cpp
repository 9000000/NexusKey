// NexusKey - VNI Engine Tests
// SPDX-License-Identifier: GPL-3.0-only

#include <gtest/gtest.h>
#include "core/engine/VniEngine.h"
#include "TestHelper.h"

namespace NextKey {
namespace Vni {
namespace {

using Testing::TypeString;

class VniEngineTest : public ::testing::Test {
protected:
    void SetUp() override {
        engine_ = std::make_unique<VniEngine>();
    }

    std::unique_ptr<VniEngine> engine_;
};

// ============================================================================
// CIRCUMFLEX TESTS (key 6): â ê ô
// ============================================================================

TEST_F(VniEngineTest, Circumflex_A6_LowerCase) {
    TypeString(*engine_, L"a6");
    EXPECT_EQ(engine_->Peek(), L"â");
}

TEST_F(VniEngineTest, Circumflex_A6_UpperCase) {
    TypeString(*engine_, L"A6");
    EXPECT_EQ(engine_->Peek(), L"Â");
}

TEST_F(VniEngineTest, Circumflex_E6_LowerCase) {
    TypeString(*engine_, L"e6");
    EXPECT_EQ(engine_->Peek(), L"ê");
}

TEST_F(VniEngineTest, Circumflex_O6_LowerCase) {
    TypeString(*engine_, L"o6");
    EXPECT_EQ(engine_->Peek(), L"ô");
}

// ============================================================================
// HORN TESTS (key 7): ơ ư
// ============================================================================

TEST_F(VniEngineTest, Horn_O7_LowerCase) {
    TypeString(*engine_, L"o7");
    EXPECT_EQ(engine_->Peek(), L"ơ");
}

TEST_F(VniEngineTest, Horn_U7_LowerCase) {
    TypeString(*engine_, L"u7");
    EXPECT_EQ(engine_->Peek(), L"ư");
}

TEST_F(VniEngineTest, Horn_U7_UpperCase) {
    TypeString(*engine_, L"U7");
    EXPECT_EQ(engine_->Peek(), L"Ư");
}

TEST_F(VniEngineTest, Horn_UO7_DirectTransform) {
    // uo7 → ươ (first 7 horns both u and o directly)
    TypeString(*engine_, L"uo7");
    EXPECT_EQ(engine_->Peek(), L"ươ");
}

TEST_F(VniEngineTest, Horn_UO77_Escape) {
    // uo77 → uo (non h/th/kh: 2-state, second 7 escapes)
    TypeString(*engine_, L"uo77");
    EXPECT_EQ(engine_->Peek(), L"uo7");
}

TEST_F(VniEngineTest, Horn_HUO7_DirectTransform) {
    // huo7 → huơ (first 7: default uơ for h prefix)
    TypeString(*engine_, L"huo7");
    EXPECT_EQ(engine_->Peek(), L"huơ");
}

TEST_F(VniEngineTest, Horn_HUO77_Cycle) {
    // huo77 → hươ (h prefix: second 7 cycles uơ → ươ)
    TypeString(*engine_, L"huo77");
    EXPECT_EQ(engine_->Peek(), L"hươ");
}

TEST_F(VniEngineTest, Horn_HUO777_Escape) {
    // huo777 → huo (h prefix: third 7 escapes uơ → uo)
    TypeString(*engine_, L"huo777");
    EXPECT_EQ(engine_->Peek(), L"huo7");
}

TEST_F(VniEngineTest, Horn_THUO7_DirectTransform) {
    // thuo7 → thuơ (th prefix, first 7: default uơ)
    TypeString(*engine_, L"thuo7");
    EXPECT_EQ(engine_->Peek(), L"thuơ");
}

TEST_F(VniEngineTest, Horn_THUO77_Cycle) {
    // thuo77 → thươ (th prefix: uơ → ươ)
    TypeString(*engine_, L"thuo77");
    EXPECT_EQ(engine_->Peek(), L"thươ");
}

TEST_F(VniEngineTest, Horn_KHUO77_Cycle) {
    // khuo77 → khươ (kh prefix: uơ → ươ)
    TypeString(*engine_, L"khuo77");
    EXPECT_EQ(engine_->Peek(), L"khươ");
}

TEST_F(VniEngineTest, Horn_HUO7N_AutoUO_Completes) {
    // huo7n → hươn (first 7 gives uơ, n triggers AutoUO to complete ươ)
    TypeString(*engine_, L"huo7n");
    EXPECT_EQ(engine_->Peek(), L"hươn");
}

TEST_F(VniEngineTest, Horn_THUO73_Thuor) {
    // thuo73 → thuở (first 7 gives uơ, then 3 adds hỏi tone)
    TypeString(*engine_, L"thuo73");
    EXPECT_EQ(engine_->Peek(), L"thuở");
}

TEST_F(VniEngineTest, Horn_DUO7_DirectTransform) {
    // duo7 → dươ (non h/th/kh, first 7)
    TypeString(*engine_, L"duo7");
    EXPECT_EQ(engine_->Peek(), L"dươ");
}

TEST_F(VniEngineTest, Horn_DUO77_Escape) {
    // duo77 → duo (non h/th/kh: 2-state escape)
    TypeString(*engine_, L"duo77");
    EXPECT_EQ(engine_->Peek(), L"duo7");
}

TEST_F(VniEngineTest, Horn_NUO7NG_AutoPair) {
    // nuo7ng → nương (ng after pair triggers auto-horn on u)
    TypeString(*engine_, L"nuo7ng");
    EXPECT_EQ(engine_->Peek(), L"nương");
}

TEST_F(VniEngineTest, Horn_NUO71NG_Nuong_WithTone) {
    // nuo71ng → nướng (horn + acute, auto-pair when n typed)
    TypeString(*engine_, L"nuo71ng");
    EXPECT_EQ(engine_->Peek(), L"nướng");
}

TEST_F(VniEngineTest, Horn_DUO7C_Duoc) {
    // d9uo7c → đươc (c after pair triggers auto-horn on u)
    TypeString(*engine_, L"d9uo7c");
    EXPECT_EQ(engine_->Peek(), L"đươc");
}

TEST_F(VniEngineTest, Horn_SingleO7) {
    TypeString(*engine_, L"o7");
    EXPECT_EQ(engine_->Peek(), L"ơ");
}

TEST_F(VniEngineTest, Horn_QUO7C_QuCluster) {
    // u in "qu" is consonant cluster — should NOT get horned
    TypeString(*engine_, L"quo7c");
    EXPECT_EQ(engine_->Peek(), L"quơc");
}

TEST_F(VniEngineTest, Horn_HU7O7_ExplicitBoth) {
    // h-u-7-o-7 → hươ (explicit horn each vowel)
    TypeString(*engine_, L"hu7o7");
    EXPECT_EQ(engine_->Peek(), L"hươ");
}

TEST_F(VniEngineTest, Horn_HU7O7NG_Huong) {
    // hu7o71ng → hướng
    TypeString(*engine_, L"hu7o71ng");
    EXPECT_EQ(engine_->Peek(), L"hướng");
}

TEST_F(VniEngineTest, Horn_LUO7N_AutoPair) {
    // luo7n → lươn (n after pair triggers auto)
    TypeString(*engine_, L"luo7n");
    EXPECT_EQ(engine_->Peek(), L"lươn");
}

TEST_F(VniEngineTest, Horn_PHUONG_AutoPair) {
    // phuo7ng → phương
    TypeString(*engine_, L"phuo71ng");
    EXPECT_EQ(engine_->Peek(), L"phướng");
}

// ============================================================================
// BREVE TESTS (key 8): ă
// ============================================================================

TEST_F(VniEngineTest, Breve_A8_LowerCase) {
    TypeString(*engine_, L"a8");
    EXPECT_EQ(engine_->Peek(), L"ă");
}

TEST_F(VniEngineTest, Breve_A8_UpperCase) {
    TypeString(*engine_, L"A8");
    EXPECT_EQ(engine_->Peek(), L"Ă");
}

// ============================================================================
// STROKE TESTS (key 9): đ
// ============================================================================

TEST_F(VniEngineTest, Stroke_D9_LowerCase) {
    TypeString(*engine_, L"d9");
    EXPECT_EQ(engine_->Peek(), L"đ");
}

TEST_F(VniEngineTest, Stroke_D9_UpperCase) {
    TypeString(*engine_, L"D9");
    EXPECT_EQ(engine_->Peek(), L"Đ");
}

// ============================================================================
// TONE TESTS (keys 1-5)
// ============================================================================

TEST_F(VniEngineTest, Tone_Acute_1) {
    TypeString(*engine_, L"a1");
    EXPECT_EQ(engine_->Peek(), L"á");
}

TEST_F(VniEngineTest, Tone_Grave_2) {
    TypeString(*engine_, L"a2");
    EXPECT_EQ(engine_->Peek(), L"à");
}

TEST_F(VniEngineTest, Tone_Hook_3) {
    TypeString(*engine_, L"a3");
    EXPECT_EQ(engine_->Peek(), L"ả");
}

TEST_F(VniEngineTest, Tone_Tilde_4) {
    TypeString(*engine_, L"a4");
    EXPECT_EQ(engine_->Peek(), L"ã");
}

TEST_F(VniEngineTest, Tone_Dot_5) {
    TypeString(*engine_, L"a5");
    EXPECT_EQ(engine_->Peek(), L"ạ");
}

// ============================================================================
// COMBINED MODIFIER + TONE TESTS
// ============================================================================

TEST_F(VniEngineTest, Combined_A6_Acute) {
    TypeString(*engine_, L"a61");
    EXPECT_EQ(engine_->Peek(), L"ấ");
}

TEST_F(VniEngineTest, Combined_E6_Grave) {
    TypeString(*engine_, L"e62");
    EXPECT_EQ(engine_->Peek(), L"ề");
}

TEST_F(VniEngineTest, Combined_O7_Hook) {
    TypeString(*engine_, L"o73");
    EXPECT_EQ(engine_->Peek(), L"ở");
}

TEST_F(VniEngineTest, Combined_U7_Tilde) {
    TypeString(*engine_, L"u74");
    EXPECT_EQ(engine_->Peek(), L"ữ");
}

TEST_F(VniEngineTest, Combined_A8_Dot) {
    TypeString(*engine_, L"a85");
    EXPECT_EQ(engine_->Peek(), L"ặ");
}

// ============================================================================
// CODA-AWARE RISING DIPHTHONG TESTS (oa, oe with coda)
// ============================================================================

TEST_F(VniEngineTest, CodaAware_OAN_Grave_NormalOrder) {
    TypeString(*engine_, L"hoan2");  // hoàn (coda 'n' -> tone ALWAYS on second vowel 'a')
    EXPECT_EQ(engine_->Peek(), L"hoàn");
}

TEST_F(VniEngineTest, CodaAware_OAN_Grave_EarlyTone) {
    TypeString(*engine_, L"ho2an");  // h-o-2 -> hò, a -> hòa, n -> hoàn (tone relocates to 'a')
    EXPECT_EQ(engine_->Peek(), L"hoàn");
}

TEST_F(VniEngineTest, CodaAware_OET_Dot_EarlyTone) {
    TypeString(*engine_, L"xo5et");  // x-o-5 -> xọ, e -> xọe, t -> xoẹt
    EXPECT_EQ(engine_->Peek(), L"xoẹt");
}

// ============================================================================
// WORD TESTS
// ============================================================================

TEST_F(VniEngineTest, Word_Viet) {
    TypeString(*engine_, L"vie65t");  // viêt with acute = việt
    EXPECT_EQ(engine_->Peek(), L"việt");
}

TEST_F(VniEngineTest, Word_Nam) {
    TypeString(*engine_, L"nam");
    EXPECT_EQ(engine_->Peek(), L"nam");
}

TEST_F(VniEngineTest, Word_Duoc) {
    TypeString(*engine_, L"d9u7o75c");  // đươc with dot = được
    EXPECT_EQ(engine_->Peek(), L"được");
}

TEST_F(VniEngineTest, Word_Tieng) {
    TypeString(*engine_, L"tie61ng");  // tiếng
    EXPECT_EQ(engine_->Peek(), L"tiếng");
}

TEST_F(VniEngineTest, Word_Nguoi) {
    TypeString(*engine_, L"ngu7o72i");  // người
    EXPECT_EQ(engine_->Peek(), L"người");
}

// ============================================================================
// ESCAPE TESTS
// ============================================================================

TEST_F(VniEngineTest, Escape_Circumflex_A66) {
    TypeString(*engine_, L"a66");  // â + 6 = a6
    EXPECT_EQ(engine_->Peek(), L"a6");
}

TEST_F(VniEngineTest, Escape_Tone_A11) {
    TypeString(*engine_, L"a11");  // á + 1 = a1
    EXPECT_EQ(engine_->Peek(), L"a1");
}

TEST_F(VniEngineTest, Escape_Stroke_D99) {
    TypeString(*engine_, L"d99");  // đ + 9 = d9
    EXPECT_EQ(engine_->Peek(), L"d9");
}

// ============================================================================
// BACKSPACE TESTS
// ============================================================================

TEST_F(VniEngineTest, Backspace_RemovesLast) {
    TypeString(*engine_, L"vie");
    engine_->Backspace();
    EXPECT_EQ(engine_->Peek(), L"vi");
}

TEST_F(VniEngineTest, Backspace_FromEmpty) {
    engine_->Backspace();
    EXPECT_EQ(engine_->Peek(), L"");
}

// ============================================================================
// COMMIT AND RESET TESTS
// ============================================================================

TEST_F(VniEngineTest, Commit_ReturnsAndClears) {
    TypeString(*engine_, L"vie65t");
    std::wstring result = engine_->Commit();
    EXPECT_EQ(result, L"việt");
    EXPECT_EQ(engine_->Peek(), L"");
    EXPECT_EQ(engine_->Count(), 0);
}

TEST_F(VniEngineTest, Reset_ClearsState) {
    TypeString(*engine_, L"test");
    engine_->Reset();
    EXPECT_EQ(engine_->Count(), 0);
    EXPECT_EQ(engine_->Peek(), L"");
}

// ============================================================================
// QUICK CONSONANT + AUTO-RESTORE TESTS
// ============================================================================

class VniQuickConsonantTest : public ::testing::Test {
protected:
    void SetUp() override {
        config_.spellCheckEnabled = true;
        config_.autoRestoreEnabled = true;
        config_.quickConsonant = true;
        engine_ = std::make_unique<VniEngine>(config_);
    }

    TypingConfig config_;
    std::unique_ptr<VniEngine> engine_;
};

TEST_F(VniQuickConsonantTest, GG_Alone_Restores) {
    TypeString(*engine_, L"gg");
    EXPECT_EQ(engine_->Commit(), L"gg");
}

TEST_F(VniQuickConsonantTest, CC_Alone_Restores) {
    TypeString(*engine_, L"cc");
    EXPECT_EQ(engine_->Commit(), L"cc");
}

TEST_F(VniQuickConsonantTest, UU_Alone_Restores) {
    TypeString(*engine_, L"uu");
    EXPECT_EQ(engine_->Commit(), L"uu");
}

TEST_F(VniQuickConsonantTest, GG_WithVowel_Keeps) {
    TypeString(*engine_, L"gga");
    EXPECT_EQ(engine_->Commit(), L"gia");
}

TEST_F(VniQuickConsonantTest, PP_Backspace_Escape) {
    TypeString(*engine_, L"app");
    EXPECT_EQ(engine_->Peek(), L"aph");
    engine_->Backspace();
    EXPECT_EQ(engine_->Peek(), L"app");   // restored in-place
    engine_->PushChar(L'p');               // escape consumed, literal 'p'
    EXPECT_EQ(engine_->Peek(), L"appp");
}

TEST_F(VniQuickConsonantTest, UU_Backspace_Escape) {
    TypeString(*engine_, L"uu");
    EXPECT_EQ(engine_->Peek(), L"ươ");
    engine_->Backspace();
    EXPECT_EQ(engine_->Peek(), L"uu");    // restored in-place
    engine_->PushChar(L'u');               // escape consumed, literal 'u'
    EXPECT_EQ(engine_->Peek(), L"uuu");
}

// ============================================================================
// NON-INITIAL d9 STROKE TESTS (abbreviation support)
// Keep in sync with TelexEngine dd→đ tests in TelexEngineTest.cpp
// ============================================================================

TEST_F(VniEngineTest, Stroke_NonInitial_AfterConsonant) {
    // "vd9" = v + d9→đ
    TypeString(*engine_, L"vd9");
    EXPECT_EQ(engine_->Peek(), L"vđ");
}

TEST_F(VniEngineTest, Stroke_NonInitial_BlockedAfterVowel) {
    // "ad9" = a + d → d9 should NOT become đ (preceded by vowel 'a')
    TypeString(*engine_, L"ad9");
    EXPECT_EQ(engine_->Peek(), L"ad9");
}

TEST_F(VniEngineTest, Stroke_NonInitial_EscapeAfterConsonant) {
    // "vd99" = v + d9→đ + 9→escape → "vd9"
    TypeString(*engine_, L"vd99");
    EXPECT_EQ(engine_->Peek(), L"vd9");
}

TEST_F(VniEngineTest, Stroke_Initial_StillWorks) {
    // Basic d9→đ at index 0
    TypeString(*engine_, L"d9i");
    EXPECT_EQ(engine_->Peek(), L"đi");
}

// ============================================================================
// MODIFIER SWITCHING (Circumflex ↔ Breve/Horn)
// ============================================================================

TEST_F(VniEngineTest, ModifierSwitch_CircumflexToBreve_CodaN) {
    // "chận+8" → "chặn" (â→ă via breve key across coda 'n')
    TypeString(*engine_, L"cha6n5");  // chận
    EXPECT_EQ(engine_->Peek(), L"chận");
    TypeString(*engine_, L"8");       // switch â→ă
    EXPECT_EQ(engine_->Peek(), L"chặn");
}

TEST_F(VniEngineTest, ModifierSwitch_BreveToCircumflex_CodaN) {
    // "chặn+6" → "chận" (ă→â via circumflex key across coda 'n')
    TypeString(*engine_, L"cha8n5");  // chặn
    EXPECT_EQ(engine_->Peek(), L"chặn");
    TypeString(*engine_, L"6");       // switch ă→â
    EXPECT_EQ(engine_->Peek(), L"chận");
}

TEST_F(VniEngineTest, ModifierSwitch_CircumflexToBreve_NoCoda) {
    // "â+8" → "ă"
    TypeString(*engine_, L"a68");
    EXPECT_EQ(engine_->Peek(), L"ă");
}

TEST_F(VniEngineTest, ModifierSwitch_BreveToCircumflex_NoCoda) {
    // "ă+6" → "â"
    TypeString(*engine_, L"a86");
    EXPECT_EQ(engine_->Peek(), L"â");
}

TEST_F(VniEngineTest, ModifierSwitch_CircumflexToHorn_O) {
    // "ô+7" → "ơ" (circumflex→horn on 'o')
    TypeString(*engine_, L"o67");
    EXPECT_EQ(engine_->Peek(), L"ơ");
}

TEST_F(VniEngineTest, ModifierSwitch_HornToCircumflex_O) {
    // "ơ+6" → "ô" (horn→circumflex on 'o')
    TypeString(*engine_, L"o76");
    EXPECT_EQ(engine_->Peek(), L"ô");
}

TEST_F(VniEngineTest, ModifierSwitch_CircumflexToBreve_WithTone) {
    // Tone preserved: "tấn+8" → "tắn"
    TypeString(*engine_, L"ta6n1");  // tấn
    EXPECT_EQ(engine_->Peek(), L"tấn");
    TypeString(*engine_, L"8");       // switch â→ă
    EXPECT_EQ(engine_->Peek(), L"tắn");
}

// ============================================================================
// TONE RELOCATION AFTER HORN MODIFIER
// ============================================================================

TEST_F(VniEngineTest, ToneReloc_HornOnUO_MovesToneToO) {
    // "tu2o7ng" → "tường" (tone relocates from ư to ơ)
    TypeString(*engine_, L"tu2o7ng");
    EXPECT_EQ(engine_->Peek(), L"tường");
}

TEST_F(VniEngineTest, ToneReloc_HornOnU_ToneStaysOnHorn) {
    // "cu1a7" → "cứa" (tone stays on ư — horn vowel P1 priority)
    TypeString(*engine_, L"cu1a7");
    EXPECT_EQ(engine_->Peek(), L"cứa");
}

// ============================================================================
// ENGLISH PROTECTION — INVALID ADJACENT VOWEL PAIRS
// ============================================================================

TEST_F(VniEngineTest, EnglishProt_InvalidVowelPair_EA_BlocksTone) {
    // "sea1" → "sea1" (ea is not a Vietnamese diphthong)
    TypeString(*engine_, L"sea1");
    EXPECT_EQ(engine_->Peek(), L"sea1");
}

TEST_F(VniEngineTest, EnglishProt_InvalidVowelPair_OY_BlocksTone) {
    // "boy1" → "boy1" (oy is not a Vietnamese diphthong)
    TypeString(*engine_, L"boy1");
    EXPECT_EQ(engine_->Peek(), L"boy1");
}

TEST_F(VniEngineTest, EnglishProt_ValidVowelPair_OA_AllowsTone) {
    // "hoa1" → "hóa" (oa is coda-aware: no coda → tone on first vowel 'o')
    TypeString(*engine_, L"hoa1");
    EXPECT_EQ(engine_->Peek(), L"hóa");
}


// ============================================================================
// UU + HORN: horn should go on FIRST 'u' (lưu, cưu, hưu)
// ============================================================================

TEST_F(VniEngineTest, Horn_UU_HornOnFirstU) {
    // uu7 → ưu (horn on first 'u', like lưu pattern)
    TypeString(*engine_, L"uu7");
    EXPECT_EQ(engine_->Peek(), L"ưu");
}

TEST_F(VniEngineTest, Horn_LUU_Luu) {
    // luu7 → lưu
    TypeString(*engine_, L"luu7");
    EXPECT_EQ(engine_->Peek(), L"lưu");
}

// ============================================================================
// TONE STABILITY — repeated vowels must NOT slide the tone (Bug #1)
// ============================================================================

TEST_F(VniEngineTest, ToneStable_RepeatedA_AfterI) {
    // "Ki2aaaa" → "Kìaaaa" (tone stays on 'i', not last 'a')
    TypeString(*engine_, L"Ki2aaaa");
    EXPECT_EQ(engine_->Peek(), L"Kìaaaa");
}

TEST_F(VniEngineTest, ToneStable_RepeatedO_AfterE) {
    // "me2oooo" → "mèoooo" (tone stays on 'e', not last 'o')
    TypeString(*engine_, L"me2oooo");
    EXPECT_EQ(engine_->Peek(), L"mèoooo");
}

TEST_F(VniEngineTest, ToneStable_RepeatedA_Single) {
    // "a2aaa" → "àaaa" (tone stays on first 'a')
    TypeString(*engine_, L"a2aaa");
    EXPECT_EQ(engine_->Peek(), L"àaaa");
}

TEST_F(VniEngineTest, ToneStable_RepeatedO_AfterO) {
    // "o1ooo" → "óooo"
    TypeString(*engine_, L"o1ooo");
    EXPECT_EQ(engine_->Peek(), L"óooo");
}

TEST_F(VniEngineTest, ToneStable_RepeatedI_AfterOA) {
    // "Hoai2iiiii" → "Hoàiiiiii" (tone stays on 'a', not sliding to 'i')
    // Tone key '2' doesn't add a char → 6 i's in output (1 original + 5 typed)
    TypeString(*engine_, L"Hoai2iiiii");
    EXPECT_EQ(engine_->Peek(), L"Hoàiiiiii");
}

TEST_F(VniEngineTest, ToneStable_RepeatedI_AfterUA) {
    // "Quai1iiiii" → "Quáiiiiii" (tone stays on 'a')
    TypeString(*engine_, L"Quai1iiiii");
    EXPECT_EQ(engine_->Peek(), L"Quáiiiiii");
}

TEST_F(VniEngineTest, ToneStable_RepeatedU_Single) {
    // "Tu1uuuuu" → "Túuuuuu" (tone stays on first 'u')
    TypeString(*engine_, L"Tu1uuuuu");
    EXPECT_EQ(engine_->Peek(), L"Túuuuuu");
}

TEST_F(VniEngineTest, ToneStable_RepeatedI_AfterOI) {
    // "d9oi2iiiii" → "đòiiiiii" (tone stays on 'o')
    TypeString(*engine_, L"d9oi2iiiii");
    EXPECT_EQ(engine_->Peek(), L"đòiiiiii");
}

TEST_F(VniEngineTest, ToneStable_RepeatedI_AfterAI) {
    // "Vai4iiiii" → "Vãiiiiii" (tone stays on 'a')
    TypeString(*engine_, L"Vai4iiiii");
    EXPECT_EQ(engine_->Peek(), L"Vãiiiiii");
}

TEST_F(VniEngineTest, ToneStable_CodaRelocStillWorks) {
    // "ho2an" → "hoàn" (coda 'n' changes rule-3 target — must still relocate)
    TypeString(*engine_, L"ho2an");
    EXPECT_EQ(engine_->Peek(), L"hoàn");
}

TEST_F(VniEngineTest, ToneStable_DiphthongRelocStillWorks) {
    // "hu3y" → "hủy" (classic uy: no coda → tone on first)
    TypeString(*engine_, L"hu3y");
    EXPECT_EQ(engine_->Peek(), L"hủy");
}

// ============================================================================
// Spell exclusion tone bypass (Issue #75)
// ============================================================================

class VniSpellExclusionTest : public ::testing::Test {
protected:
    void SetUp() override {
        config_.inputMethod = InputMethod::VNI;
        config_.spellCheckEnabled = true;
        config_.autoRestoreEnabled = true;
        config_.spellExclusions = {L"kà"};
        engine_ = std::make_unique<VniEngine>(config_);
    }

    TypingConfig config_;
    std::unique_ptr<VniEngine> engine_;
};

TEST_F(VniSpellExclusionTest, ToneBypass_KaGrave_ExactMatch) {
    // VNI: "ka2" → "kà" (2 = grave). Exclusion "kà" allows it.
    TypeString(*engine_, L"ka2");
    EXPECT_EQ(engine_->Peek(), L"kà");
    EXPECT_EQ(engine_->Commit(), L"kà");
}

TEST_F(VniSpellExclusionTest, ToneBypass_KaAcute_NotBypassed) {
    // VNI: "ka1" → "kas" (1 = acute). Exclusion "kà" does NOT allow "ká".
    TypeString(*engine_, L"ka1");
    EXPECT_EQ(engine_->Peek(), L"ka1");  // '1' treated as literal
}

TEST_F(VniSpellExclusionTest, ToneBypass_NoExclusion_Blocked) {
    // Without exclusion, "ka2" → literal "ka2"
    TypingConfig cfg;
    cfg.inputMethod = InputMethod::VNI;
    cfg.spellCheckEnabled = true;
    cfg.autoRestoreEnabled = true;
    VniEngine eng(cfg);
    TypeString(eng, L"ka2");
    EXPECT_EQ(eng.Peek(), L"ka2");
}

// Issue #78: Modifier bypass when allowZwjf=false (VNI keys 6/7/8)

TEST_F(VniSpellExclusionTest, ZwjfOff_CircumflexBypass) {
    // VNI: "zo6" → "zô" with exclusion "zô", allowZwjf=false
    TypingConfig cfg;
    cfg.inputMethod = InputMethod::VNI;
    cfg.spellCheckEnabled = true;
    cfg.autoRestoreEnabled = true;
    cfg.allowZwjf = false;
    cfg.spellExclusions = {L"zô"};
    VniEngine eng(cfg);
    TypeString(eng, L"zo6");
    EXPECT_EQ(eng.Peek(), L"zô");
    EXPECT_EQ(eng.Commit(), L"zô");
}

TEST_F(VniSpellExclusionTest, ZwjfOff_BreveBypass_FullWord) {
    // VNI: "za81c" → "zắc" with exclusion "zắc", allowZwjf=false
    TypingConfig cfg;
    cfg.inputMethod = InputMethod::VNI;
    cfg.spellCheckEnabled = true;
    cfg.autoRestoreEnabled = true;
    cfg.allowZwjf = false;
    cfg.spellExclusions = {L"zắc"};
    VniEngine eng(cfg);
    TypeString(eng, L"za81c");
    EXPECT_EQ(eng.Peek(), L"zắc");
    EXPECT_EQ(eng.Commit(), L"zắc");
}

TEST_F(VniSpellExclusionTest, ZwjfOff_NoExclusion_StillBlocked) {
    // No matching exclusion → modifier blocked
    TypingConfig cfg;
    cfg.inputMethod = InputMethod::VNI;
    cfg.spellCheckEnabled = true;
    cfg.autoRestoreEnabled = true;
    cfg.allowZwjf = false;
    VniEngine eng(cfg);
    TypeString(eng, L"zo6");
    EXPECT_EQ(eng.Peek(), L"zo6");
}

}  // namespace
}  // namespace Vni
}  // namespace NextKey
