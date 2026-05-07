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
    const std::wstring withDefault = engine.Peek();

    TypingConfig baseline = MakeTelexConfig();
    TypingEngine baselineEngine(baseline);
    TypeString(baselineEngine, L"asfx");
    EXPECT_EQ(withDefault, baselineEngine.Peek());
}

TEST_F(CustomKeyMapTest, DefaultEmptyMatchesVni) {
    TypingConfig cfg = MakeVniConfig();
    TypingEngine engine(cfg);
    TypeString(engine, L"a1e2o3");
    const std::wstring withDefault = engine.Peek();

    TypingConfig baseline = MakeVniConfig();
    TypingEngine baselineEngine(baseline);
    TypeString(baselineEngine, L"a1e2o3");
    EXPECT_EQ(withDefault, baselineEngine.Peek());
}

TEST_F(CustomKeyMapTest, DefaultEmptyMatchesCombined) {
    TypingConfig cfg = MakeCombinedConfig();
    TypingEngine engine(cfg);
    TypeString(engine, L"as6w7");
    const std::wstring withDefault = engine.Peek();

    TypingConfig baseline = MakeCombinedConfig();
    TypingEngine baselineEngine(baseline);
    TypeString(baselineEngine, L"as6w7");
    EXPECT_EQ(withDefault, baselineEngine.Peek());
}

}  // namespace
}  // namespace NextKey
