// VKey - customKeyMap (G-4) Tests
// Copyright (c) 2024-2026 PhatMT. All rights reserved.
// SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-VKey-Commercial
//
// Tests for the per-key user override layer added in Path G G-4.
// Spec: docs/superpowers/specs/2026-05-07-path-g-g4-customkeymap-design.md

#include <gtest/gtest.h>

#include "core/config/TypingConfig.h"
#include "core/engine/TypingAction.h"
#include "core/engine/TypingEngine.h"
#include "TestHelper.h"

namespace NextKey {
namespace {

using Testing::TypeString;

class CustomKeyMapTest : public ::testing::Test {
protected:
    TypingConfig MakeTelexConfig() {
        TypingConfig cfg;
        cfg.inputMethod = InputMethod::Telex;
        cfg.spellCheckEnabled = false;
        cfg.optimizeLevel = 0;
        return cfg;
    }

    TypingConfig MakeVniConfig() {
        TypingConfig cfg;
        cfg.inputMethod = InputMethod::VNI;
        cfg.spellCheckEnabled = false;
        cfg.optimizeLevel = 0;
        return cfg;
    }

    TypingConfig MakeCombinedConfig() {
        TypingConfig cfg;
        cfg.inputMethod = InputMethod::Combined;
        cfg.spellCheckEnabled = false;
        cfg.optimizeLevel = 0;
        return cfg;
    }

    TypingConfig MakeUserDefinedConfig() {
        TypingConfig cfg;
        cfg.inputMethod = InputMethod::UserDefined;
        cfg.spellCheckEnabled = false;
        cfg.optimizeLevel = 0;
        return cfg;
    }
};

// =====================================================================
// U1 — UserDefined Mode: pure hybrid (Telex + VNI actions coexist)
// =====================================================================

TEST_F(CustomKeyMapTest, UserDefinedHybridTelexAndVni) {
    TypingConfig cfg = MakeUserDefinedConfig();
    // 's' (Telex sharp) and '2' (VNI grave)
    cfg.customKeyMap[static_cast<size_t>(L's')] = TypingAction::ToneAcute;
    cfg.customKeyMap[static_cast<size_t>(L'2')] = TypingAction::ToneGrave;
    
    TypingEngine engine(cfg);
    TypeString(engine, L"as");
    EXPECT_EQ(engine.Peek(), L"á");
    
    TypeString(engine, L"2"); // Switch to grave
    EXPECT_EQ(engine.Peek(), L"à");
}

TEST_F(CustomKeyMapTest, UserDefinedUndoAllMarks) {
    TypingConfig cfg = MakeUserDefinedConfig();
    cfg.customKeyMap[static_cast<size_t>(L's')] = TypingAction::ToneAcute;
    cfg.customKeyMap[static_cast<size_t>(L'a')] = TypingAction::CircumflexA;
    cfg.customKeyMap[static_cast<size_t>(L'z')] = TypingAction::UndoAllMarks;
    
    TypingEngine engine(cfg);
    TypeString(engine, L"aas");
    // 'a' + remapped 'a' -> â, then 's' -> ấ
    EXPECT_EQ(engine.Peek(), L"ấ");
    
    TypeString(engine, L"z");
    // 'z' is remapped to UndoAllMarks -> clears both circumflex and sharp tone.
    // Base char 'a' remains.
    EXPECT_EQ(engine.Peek(), L"a");
}

TEST_F(CustomKeyMapTest, UserDefinedDirectInsert) {
    TypingConfig cfg = MakeUserDefinedConfig();
    cfg.customKeyMap[static_cast<size_t>(L'1')] = TypingAction::InsertABreve;
    cfg.customKeyMap[static_cast<size_t>(L'2')] = TypingAction::InsertDStroke;
    
    TypingEngine engine(cfg);
    TypeString(engine, L"12");
    EXPECT_EQ(engine.Peek(), L"ăđ");
}

TEST_F(CustomKeyMapTest, UserDefinedCircumflexEscape) {
    TypingConfig cfg = MakeUserDefinedConfig();
    cfg.customKeyMap[static_cast<size_t>(L'q')] = TypingAction::CircumflexA;
    
    TypingEngine engine(cfg);
    TypeString(engine, L"aq");
    EXPECT_EQ(engine.Peek(), L"â");
    
    // Problem 6: Gõ tiếp 'q' phải escape â -> aq
    TypeString(engine, L"q");
    EXPECT_EQ(engine.Peek(), L"aq");
}

// U2 — UserDefined OEM punctuation keys (HookEngine step 6d, bug 2026-05-16).
// `;` bound to ToneDot không ăn vì HookEngine::IsCommitTrigger nuốt OEM trước.
// Engine layer đã đúng — test này pin behaviour để routing fix không drift.
TEST_F(CustomKeyMapTest, UserDefinedOemKeysSpanAllTones) {
    TypingConfig cfg = MakeUserDefinedConfig();
    cfg.customKeyMap[static_cast<size_t>(L';')] = TypingAction::ToneDot;
    cfg.customKeyMap[static_cast<size_t>(L'\'')] = TypingAction::ToneAcute;
    cfg.customKeyMap[static_cast<size_t>(L',')] = TypingAction::ToneGrave;
    cfg.customKeyMap[static_cast<size_t>(L'.')] = TypingAction::ToneHook;
    cfg.customKeyMap[static_cast<size_t>(L'/')] = TypingAction::ToneTilde;
    TypingEngine engine(cfg);
    TypeString(engine, L"a;");
    EXPECT_EQ(engine.Peek(), L"ạ");  // U+1EA1
    TypeString(engine, L"\'");
    EXPECT_EQ(engine.Peek(), L"á");
    TypeString(engine, L",");
    EXPECT_EQ(engine.Peek(), L"à");
    TypeString(engine, L".");
    EXPECT_EQ(engine.Peek(), L"ả");
    TypeString(engine, L"/");
    EXPECT_EQ(engine.Peek(), L"ã");
}

// U3 — User feedback 2026-05-17: phím W remapped to HornOrInsertUNoStart,
// `[` `]` mapped to HornInsertO/U.
// Reported: `[` `]` không hoạt động, W đầu từ vẫn ra Ư.
// Engine-layer pin: verify literal/insert behaviour so we can isolate Hook
// routing vs engine bugs.
TEST_F(CustomKeyMapTest, UserDefinedWNoStartAtWordStartIsLiteral) {
    TypingConfig cfg = MakeUserDefinedConfig();
    cfg.customKeyMap[static_cast<size_t>(L'w')] = TypingAction::HornOrInsertUNoStart;
    TypingEngine engine(cfg);
    TypeString(engine, L"w");
    EXPECT_EQ(engine.Peek(), L"w");  // start-of-word: literal, NOT ư
}

TEST_F(CustomKeyMapTest, UserDefinedBracketInsertsHornAtWordStart) {
    TypingConfig cfg = MakeUserDefinedConfig();
    cfg.customKeyMap[static_cast<size_t>(L'[')] = TypingAction::HornInsertO;
    cfg.customKeyMap[static_cast<size_t>(L']')] = TypingAction::HornInsertU;
    TypingEngine engine(cfg);
    TypeString(engine, L"[");
    EXPECT_EQ(engine.Peek(), L"ơ");
    TypingEngine engine2(cfg);
    TypeString(engine2, L"]");
    EXPECT_EQ(engine2.Peek(), L"ư");
}

TEST_F(CustomKeyMapTest, UserDefinedWNoStartStillAppliesHornMidWord) {
    TypingConfig cfg = MakeUserDefinedConfig();
    cfg.customKeyMap[static_cast<size_t>(L'w')] = TypingAction::HornOrInsertUNoStart;
    TypingEngine engine(cfg);
    TypeString(engine, L"tuw");  // u + w → ư mid-word (P5)
    EXPECT_EQ(engine.Peek(), L"tư");
    TypingEngine engine2(cfg);
    TypeString(engine2, L"tow");  // o + w → ơ mid-word (P6)
    EXPECT_EQ(engine2.Peek(), L"tơ");
}

// Helper: mirror the full default UserDefined keymap that the Settings UI
// writes when the user picks the Telex-style preset (see config.toml shipped
// with the app). English-protection regression tests below depend on this
// shape because bias arming relies on e/a/o being mapped to Circumflex
// actions (free-mark fails set bias via HandleAdjacentCircumflex).
static void FillTelexPresetCustomKeyMap(TypingConfig& cfg) {
    cfg.customKeyMap[static_cast<size_t>(L'a')] = TypingAction::CircumflexA;
    cfg.customKeyMap[static_cast<size_t>(L'd')] = TypingAction::StrokeD;
    cfg.customKeyMap[static_cast<size_t>(L'e')] = TypingAction::CircumflexE;
    cfg.customKeyMap[static_cast<size_t>(L'o')] = TypingAction::CircumflexO;
    cfg.customKeyMap[static_cast<size_t>(L's')] = TypingAction::ToneAcute;
    cfg.customKeyMap[static_cast<size_t>(L'r')] = TypingAction::ToneHook;
    cfg.customKeyMap[static_cast<size_t>(L'f')] = TypingAction::ToneGrave;
    cfg.customKeyMap[static_cast<size_t>(L'x')] = TypingAction::ToneTilde;
    cfg.customKeyMap[static_cast<size_t>(L'j')] = TypingAction::ToneDot;
    cfg.customKeyMap[static_cast<size_t>(L'z')] = TypingAction::ClearTone;
    cfg.customKeyMap[static_cast<size_t>(L'w')] = TypingAction::HornOrInsertUNoStart;
    cfg.customKeyMap[static_cast<size_t>(L'[')] = TypingAction::HornInsertO;
    cfg.customKeyMap[static_cast<size_t>(L']')] = TypingAction::HornInsertU;
}

TEST_F(CustomKeyMapTest, UserDefinedRespectsEnglishBias_Review) {
    // Regression 2026-05-18: UserDefined user-only actions must honor the same
    // English-protection guards as Telex modifier path. Without bias check in
    // section 2d, typing "review" with w=HornOrInsertUNoStart produces
    // `revieư` (fallback insert fires) while equivalent Telex (HornW) stays
    // `review` (P8 skips because vowels exist + bias HardEnglish from free-mark).
    TypingConfig cfg = MakeUserDefinedConfig();
    FillTelexPresetCustomKeyMap(cfg);
    TypingEngine engine(cfg);
    TypeString(engine, L"review");
    EXPECT_EQ(engine.Peek(), L"review");
}

TEST_F(CustomKeyMapTest, UserDefinedRespectsEnglishBias_Where) {
    // `wh` raw start-cluster impossible in Vietnamese → IsHardEnglishStart sets
    // bias HardEnglish on the second char. Subsequent w/e/r/e must pass
    // through literally even in UserDefined mode.
    TypingConfig cfg = MakeUserDefinedConfig();
    FillTelexPresetCustomKeyMap(cfg);
    TypingEngine engine(cfg);
    TypeString(engine, L"where");
    EXPECT_EQ(engine.Peek(), L"where");
}

TEST_F(CustomKeyMapTest, UserDefinedValidVietnameseStillComposes_Thuw) {
    // Sanity guard: English-bias check must not over-block legitimate
    // Vietnamese sequences. `thuw` is valid start cluster (`thu` + horn → `thư`).
    TypingConfig cfg = MakeUserDefinedConfig();
    FillTelexPresetCustomKeyMap(cfg);
    TypingEngine engine(cfg);
    TypeString(engine, L"thuw");
    EXPECT_EQ(engine.Peek(), L"thư");
    TypeString(engine, L"s");
    EXPECT_EQ(engine.Peek(), L"thứ");
}

TEST_F(CustomKeyMapTest, UserDefinedHornOrInsertUFallback_WwFullyReverts) {
    // Regression 2026-05-18: w + w after a no-target insertion (e.g. typing
    // "revie" then `w` — no a/o/u to apply horn → fallback inserts ư as
    // "revieư") must FULLY revert on the second `w`, not split ư into u+w.
    // Mark the fallback-inserted ư as synthetic so HandleHornW P4 sees it
    // as the ww-escape signature (synthetic + last-state) → erase + literal.
    TypingConfig cfg = MakeUserDefinedConfig();
    cfg.customKeyMap[static_cast<size_t>(L'w')] = TypingAction::HornOrInsertUNoStart;
    TypingEngine engine(cfg);
    TypeString(engine, L"review");
    EXPECT_EQ(engine.Peek(), L"revieư");  // fallback inserted ư (synthetic)
    TypeString(engine, L"w");             // second `w` — full ww escape
    EXPECT_EQ(engine.Peek(), L"review");  // ư replaced by literal w, no `u`
}

TEST_F(CustomKeyMapTest, UserDefinedBracketInsertsHornMidWord) {
    TypingConfig cfg = MakeUserDefinedConfig();
    cfg.customKeyMap[static_cast<size_t>(L'[')] = TypingAction::HornInsertO;
    cfg.customKeyMap[static_cast<size_t>(L']')] = TypingAction::HornInsertU;
    TypingEngine engine(cfg);
    TypeString(engine, L"th[");
    EXPECT_EQ(engine.Peek(), L"thơ");
    TypingEngine engine2(cfg);
    TypeString(engine2, L"th]");
    EXPECT_EQ(engine2.Peek(), L"thư");
}

// =====================================================================
// G1 — Default-empty parity: customKeyMap{} → behavior unchanged
// =====================================================================

TEST_F(CustomKeyMapTest, DefaultEmptyMatchesTelex) {
    TypingConfig cfg = MakeTelexConfig();
    TypingEngine engine(cfg);
    TypeString(engine, L"asfx");
    // G-3.6 baseline: telex 'a'+s+f+x — s=â modifier, f=tone huyền, x=tone ngã
    // → ã (U+00E3). Captured 2026-05-07.
    EXPECT_EQ(engine.Peek(), L"ã");
}

TEST_F(CustomKeyMapTest, DefaultEmptyMatchesVni) {
    TypingConfig cfg = MakeVniConfig();
    TypingEngine engine(cfg);
    TypeString(engine, L"a1e2o3");
    // G-3.6 baseline: 'a'+1 → á, then 'e'+2 starts new syllable → é, then
    // 'o'+3 would continue — actual engine output captured 2026-05-07: aé2o3.
    EXPECT_EQ(engine.Peek(), L"aé2o3");
}

TEST_F(CustomKeyMapTest, DefaultEmptyMatchesCombined) {
    TypingConfig cfg = MakeCombinedConfig();
    TypingEngine engine(cfg);
    TypeString(engine, L"as6w7");
    // G-3.6 baseline: 'a'+s → â (telex vowel modifier), '6' → tone huyền → ầ,
    // 'w' → modifier, '7' appended — actual engine output captured 2026-05-07: ắ7.
    EXPECT_EQ(engine.Peek(), L"ắ7");
}


// =====================================================================
// G2 — Replace built-in: user override applies in UserDefined mode
// =====================================================================
// Invariant: customKeyMap is the user-defined input method's mapping table.
// It applies ONLY when `inputMethod == UserDefined`. Other modes use their
// own base mapping (ClassifyKey) and ignore customKeyMap entirely — see
// `*_IgnoredInNonUserDefinedMode` tests below for the regression guard.

TEST_F(CustomKeyMapTest, RemapSToToneHook) {
    TypingConfig cfg = MakeUserDefinedConfig();
    // UserDefined needs its full base mapping copied — only `s` differs.
    cfg.customKeyMap[static_cast<size_t>(L'a')] = TypingAction::CircumflexA;
    cfg.customKeyMap[static_cast<size_t>(L's')] = TypingAction::ToneHook;
    TypingEngine engine(cfg);
    TypeString(engine, L"as");
    // With remap 's'→ToneHook: 'a' + 's' → 'ả' (hỏi) instead of 'á' (sắc).
    EXPECT_EQ(engine.Peek(), L"ả");
}

TEST_F(CustomKeyMapTest, RemapDigit1ToClearTone) {
    TypingConfig cfg = MakeUserDefinedConfig();
    cfg.customKeyMap[static_cast<size_t>(L'1')] = TypingAction::ClearTone;
    cfg.customKeyMap[static_cast<size_t>(L'2')] = TypingAction::ToneGrave;
    TypingEngine engine(cfg);
    TypeString(engine, L"a2");  // 'a' + grave → 'à'
    EXPECT_EQ(engine.Peek(), L"à");
    TypeString(engine, L"1");   // remapped: clear tone
    EXPECT_EQ(engine.Peek(), L"a");
}


// =====================================================================
// G3 — Gap-fill: map a key in UserDefined mode (no base to fall back on)
// =====================================================================

TEST_F(CustomKeyMapTest, MapQToClearTone) {
    TypingConfig cfg = MakeUserDefinedConfig();
    cfg.customKeyMap[static_cast<size_t>(L'a')] = TypingAction::CircumflexA;
    cfg.customKeyMap[static_cast<size_t>(L's')] = TypingAction::ToneAcute;
    cfg.customKeyMap[static_cast<size_t>(L'q')] = TypingAction::ClearTone;
    TypingEngine engine(cfg);
    TypeString(engine, L"asq");  // 'a' + sắc → 'á', then 'q' clears tone → 'a'
    EXPECT_EQ(engine.Peek(), L"a");
}

TEST_F(CustomKeyMapTest, MapQToToneAcute) {
    TypingConfig cfg = MakeUserDefinedConfig();
    cfg.customKeyMap[static_cast<size_t>(L'q')] = TypingAction::ToneAcute;
    TypingEngine engine(cfg);
    TypeString(engine, L"aq");  // 'a' + remapped 'q' → ToneAcute → 'á'
    EXPECT_EQ(engine.Peek(), L"á");
}

// =====================================================================
// G2/G3 regression guard: customKeyMap MUST be ignored in non-UserDefined
// modes. Stale `[UserDefinedKeyMap]` entries left in config.toml after a
// mode switch must not silently affect Telex/VNI/Combined base behavior.
// =====================================================================

TEST_F(CustomKeyMapTest, CustomMapIgnoredInTelexMode) {
    TypingConfig cfg = MakeTelexConfig();
    // Stale entries from a previous UserDefined session.
    cfg.customKeyMap[static_cast<size_t>(L's')] = TypingAction::ToneHook;
    cfg.customKeyMap[static_cast<size_t>(L'w')] = TypingAction::HornOrInsertUNoStart;
    TypingEngine engine(cfg);
    TypeString(engine, L"as");
    // Telex base: 'a' + 's' → 'á' (sắc). customKeyMap ignored.
    EXPECT_EQ(engine.Peek(), L"á");

    TypingEngine engine2(cfg);
    TypeString(engine2, L"thuw");
    // Telex base 'w' → HornW (P5: standalone u → horn). customKeyMap ignored.
    // Regression for user-reported bug 2026-05-18: stale customKeyMap['w']=
    // HornOrInsertUNoStart caused 'w' to silent-drop, producing literal "thuw"
    // → tone 's' then applied to bare 'u' producing "thúw".
    EXPECT_EQ(engine2.Peek(), L"thư");
}

TEST_F(CustomKeyMapTest, CustomMapIgnoredInVniMode) {
    TypingConfig cfg = MakeVniConfig();
    cfg.customKeyMap[static_cast<size_t>(L'1')] = TypingAction::ClearTone;
    TypingEngine engine(cfg);
    TypeString(engine, L"a1");
    // VNI base: 'a' + '1' → 'á' (ToneAcute). customKeyMap ignored.
    EXPECT_EQ(engine.Peek(), L"á");
}

TEST_F(CustomKeyMapTest, CustomMapIgnoredInCombinedMode) {
    TypingConfig cfg = MakeCombinedConfig();
    cfg.customKeyMap[static_cast<size_t>(L's')] = TypingAction::ToneHook;
    TypingEngine engine(cfg);
    TypeString(engine, L"as");
    // Combined base: Telex 's' → ToneAcute → 'á'. customKeyMap ignored.
    EXPECT_EQ(engine.Peek(), L"á");
}

// =====================================================================
// G4 — ASCII boundary: non-ASCII keys bypass the override branch
// =====================================================================

TEST_F(CustomKeyMapTest, NonAsciiKeyFallsBackToClassifyKey) {
    TypingConfig cfg = MakeTelexConfig();
    // The override array has only 128 slots; non-ASCII keys must skip
    // the override check entirely (defensive: `lower < 128` guard).
    // We verify by typing a Vietnamese char directly — the engine treats
    // it as a literal char (ClassifyKey returns None for it), and no
    // override applies because `lower >= 128`.
    TypingEngine engine(cfg);
    TypeString(engine, L"á");  // U+00E1, definitely >= 128
    EXPECT_EQ(engine.Peek(), L"á");  // unchanged literal pass-through
}

// =====================================================================
// G5 — isVniDigitSequence post-applies even when override fired
// =====================================================================

TEST_F(CustomKeyMapTest, OverrideOnDigitYieldsLiteralInDigitSequence) {
    TypingConfig cfg = MakeVniConfig();
    // Remap '7' → ToneHook (instead of default VniHorn).
    cfg.customKeyMap[static_cast<size_t>(L'7')] = TypingAction::ToneHook;
    TypingEngine engine(cfg);
    // Type a literal digit first to enter "VNI digit sequence" mode.
    TypeString(engine, L"4");
    // Now '7' should be treated as literal (digit-sequence guard wins
    // over both the override and the default VniHorn) → composed "47".
    TypeString(engine, L"7");
    EXPECT_EQ(engine.Peek(), L"47");
}

// =====================================================================
// G6 — Sentinel: explicit None == not set
// =====================================================================

TEST_F(CustomKeyMapTest, CustomMapNoneFallsThroughToClassifyKey) {
    TypingConfig cfg = MakeTelexConfig();
    // Explicitly set 's' to None — must be indistinguishable from default.
    cfg.customKeyMap[static_cast<size_t>(L's')] = TypingAction::None;
    TypingEngine engine(cfg);
    TypeString(engine, L"as");
    // Default Telex: 'a' + 's' → 'á' (sắc).
    EXPECT_EQ(engine.Peek(), L"á");
}

// =====================================================================
// G7 — Parametric smoke: every non-None TypingAction reachable via remap
// =====================================================================

class CustomKeyMapAllActions
    : public CustomKeyMapTest,
      public ::testing::WithParamInterface<TypingAction> {};

TEST_P(CustomKeyMapAllActions, EveryTypingActionNoThrow) {
    const TypingAction action = GetParam();
    ASSERT_NE(action, TypingAction::None) << "G7 only iterates non-None actions";

    // Configure: 'q' (which ClassifyKey returns None for) remapped to the
    // parameter action under UserDefined mode (customKeyMap is only honored
    // there per the gating invariant). We do not assert specific Vietnamese
    // strings — only that dispatch reaches the correct action handler
    // (no crash, action resolves). Engine state observation is via Peek().
    TypingConfig cfg = MakeUserDefinedConfig();
    cfg.customKeyMap[static_cast<size_t>(L'q')] = action;
    TypingEngine engine(cfg);
    // Seed a vowel so modifier/tone actions have something to operate on.
    TypeString(engine, L"a");
    EXPECT_NO_THROW(TypeString(engine, L"q"));
    // Final Peek should be a non-empty wstring (engine must not be in a
    // broken state after dispatch).
    EXPECT_FALSE(engine.Peek().empty());
}

INSTANTIATE_TEST_SUITE_P(
    AllActions,
    CustomKeyMapAllActions,
    ::testing::Values(
        TypingAction::ClearTone,
        TypingAction::ToneAcute,
        TypingAction::ToneGrave,
        TypingAction::ToneHook,
        TypingAction::ToneTilde,
        TypingAction::ToneDot,
        TypingAction::CircumflexA,
        TypingAction::CircumflexE,
        TypingAction::CircumflexO,
        TypingAction::HornW,
        TypingAction::HornInsertO,
        TypingAction::HornInsertU,
        TypingAction::StrokeD,
        TypingAction::VniCircumflex,
        TypingAction::VniHorn,
        TypingAction::VniBreve,
        TypingAction::VniStroke,
        TypingAction::HornOrInsertU,
        TypingAction::HornOrInsertUNoStart,
        TypingAction::UndoAllMarks,
        TypingAction::InsertABreve,
        TypingAction::InsertABreveUpper,
        TypingAction::InsertACircumflex,
        TypingAction::InsertACircumflexUpper,
        TypingAction::InsertDStroke,
        TypingAction::InsertDStrokeUpper,
        TypingAction::InsertECircumflex,
        TypingAction::InsertECircumflexUpper,
        TypingAction::InsertOCircumflex,
        TypingAction::InsertOCircumflexUpper,
        TypingAction::InsertOHorn,
        TypingAction::InsertOHornUpper,
        TypingAction::InsertUHorn,
        TypingAction::InsertUHornUpper
    )
);

}  // namespace
}  // namespace NextKey
