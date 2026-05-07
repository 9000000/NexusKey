// NexusKey - customKeyMap (G-4) Tests
// Copyright (c) 2024-2026 PhatMT. All rights reserved.
// SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-NexusKey-Commercial
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
};

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

}  // namespace
}  // namespace NextKey
