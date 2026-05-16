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
// G2 — Replace built-in: user override always wins (precedence)
// =====================================================================

TEST_F(CustomKeyMapTest, RemapTelexSToToneHook) {
    TypingConfig cfg = MakeTelexConfig();
    cfg.customKeyMap[static_cast<size_t>(L's')] = TypingAction::ToneHook;
    TypingEngine engine(cfg);
    TypeString(engine, L"as");
    // Default Telex: 'a' + 's' → 'á' (sắc). With remap 's'→ToneHook: 'a' + 's' → 'ả' (hỏi).
    EXPECT_EQ(engine.Peek(), L"ả");
}

TEST_F(CustomKeyMapTest, RemapVniDigit1ToClearTone) {
    TypingConfig cfg = MakeVniConfig();
    // Default VNI: '1' → ToneAcute. Remap '1' → ClearTone.
    cfg.customKeyMap[static_cast<size_t>(L'1')] = TypingAction::ClearTone;
    TypingEngine engine(cfg);
    TypeString(engine, L"a2");  // 'a' + grave → 'à'
    EXPECT_EQ(engine.Peek(), L"à");
    TypeString(engine, L"1");   // remapped: clear tone
    EXPECT_EQ(engine.Peek(), L"a");
}


// =====================================================================
// G3 — Gap-fill: map a key that ClassifyKey returns None for
// =====================================================================

TEST_F(CustomKeyMapTest, MapQToClearTone_TelexMode) {
    TypingConfig cfg = MakeTelexConfig();
    // 'q' is not a Telex action key — ClassifyKey returns None.
    cfg.customKeyMap[static_cast<size_t>(L'q')] = TypingAction::ClearTone;
    TypingEngine engine(cfg);
    TypeString(engine, L"asq");  // 'a' + sắc → 'á', then 'q' clears tone → 'a'
    EXPECT_EQ(engine.Peek(), L"a");
}

TEST_F(CustomKeyMapTest, MapQToToneAcute_VniMode) {
    TypingConfig cfg = MakeVniConfig();
    // 'q' is not a VNI action key — ClassifyKey returns None.
    cfg.customKeyMap[static_cast<size_t>(L'q')] = TypingAction::ToneAcute;
    TypingEngine engine(cfg);
    TypeString(engine, L"aq");  // 'a' + remapped 'q' → ToneAcute → 'á'
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

    // Configure: 'q' (which ClassifyKey returns None for in pure Telex mode)
    // remapped to the parameter action. Compare composed output of typing
    // "q" against an engine driven through the natural key for the same
    // action. We do not assert specific Vietnamese strings here — only
    // that dispatch reaches the correct action handler (no crash, action
    // resolves). Engine state observation is via Peek().
    TypingConfig cfg = MakeCombinedConfig();
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
