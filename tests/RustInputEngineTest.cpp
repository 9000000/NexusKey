// VKey - RustInputEngine adapter tests
// SPDX-License-Identifier: AGPL-3.0-only
//
// Covers the FFI plumbing in the adapter (UTF-16 widening, peek/commit/backspace,
// stub contract) — NOT Vietnamese typing correctness, which the engine repo owns.
//
// The adapter only does real work when the prebuilt vkey_engine library is on
// the load path, so these skip by default (keeping the rest of the suite on the
// in-tree C++ engine). To run them against the real engine, point the env var
// at the vendored artifact, e.g.:
//   VKEY_ENGINE_LIB=extern/vkey_engine/lib/linux-x64/libvkey_engine.so ./VKeyTests

#ifdef VKEY_USE_RUST_ENGINE

#include <gtest/gtest.h>

#include "core/engine/RustInputEngine.h"

namespace NextKey {
namespace {

class RustInputEngineTest : public ::testing::Test {
protected:
    void SetUp() override {
        if (!RustInputEngine::LibraryAvailable()) {
            GTEST_SKIP() << "vkey_engine library not loaded; set VKEY_ENGINE_LIB to run";
        }
    }
};

TEST_F(RustInputEngineTest, TelexComposesAndCommits) {
    TypingConfig config;
    config.inputMethod = InputMethod::Telex;
    RustInputEngine engine(config);

    engine.PushChar(L'a');
    engine.PushChar(L'a');
    EXPECT_EQ(engine.Peek(), L"â");  // â
    EXPECT_EQ(engine.Count(), 1u);

    EXPECT_EQ(engine.Commit(), L"â");
    EXPECT_TRUE(engine.Peek().empty());
    EXPECT_EQ(engine.Count(), 0u);
}

TEST_F(RustInputEngineTest, ToneEscape_UppercaseR_Issue209Comment) {
    // #209 comment (Shzr0): "TeR" → "Tẻ", second R must escape → "TeR".
    // C++ TypingEngine passes this (TelexEngineTest.Escape_ToneHoi_UppercaseR).
    TypingConfig config;
    config.inputMethod = InputMethod::Telex;
    config.spellCheckEnabled = true;
    config.spellSuggestEnabled = true;
    RustInputEngine engine(config);

    engine.PushChar(L'T');
    engine.PushChar(L'e');
    engine.PushChar(L'R');
    EXPECT_EQ(engine.Peek(), L"Tẻ");
    engine.PushChar(L'R');
    EXPECT_EQ(engine.Peek(), L"TeR");
}

TEST_F(RustInputEngineTest, ToneEscape_LowercaseR_Parity) {
    TypingConfig config;
    config.inputMethod = InputMethod::Telex;
    config.spellCheckEnabled = true;
    config.spellSuggestEnabled = true;
    RustInputEngine engine(config);

    engine.PushChar(L't');
    engine.PushChar(L'e');
    engine.PushChar(L'r');
    EXPECT_EQ(engine.Peek(), L"tẻ");
    engine.PushChar(L'r');
    EXPECT_EQ(engine.Peek(), L"ter");
}

TEST_F(RustInputEngineTest, BackspaceShrinksComposition) {
    TypingConfig config;
    config.inputMethod = InputMethod::Telex;
    RustInputEngine engine(config);

    engine.PushChar(L'x');
    engine.PushChar(L'i');
    engine.PushChar(L'n');
    EXPECT_EQ(engine.Peek(), L"xin");

    engine.Backspace();
    EXPECT_EQ(engine.Peek(), L"xi");
    EXPECT_EQ(engine.Count(), 2u);
}

TEST_F(RustInputEngineTest, ResetClearsComposition) {
    TypingConfig config;
    config.inputMethod = InputMethod::Telex;
    RustInputEngine engine(config);

    engine.PushChar(L'a');
    engine.PushChar(L'a');
    ASSERT_FALSE(engine.Peek().empty());

    engine.Reset();
    EXPECT_TRUE(engine.Peek().empty());
    EXPECT_EQ(engine.Count(), 0u);
}

TEST_F(RustInputEngineTest, RawKeystrokesPreserved) {
    TypingConfig config;
    config.inputMethod = InputMethod::Telex;
    RustInputEngine engine(config);

    engine.PushChar(L'a');
    engine.PushChar(L's');  // sắc tone on 'a'
    EXPECT_EQ(engine.Peek(), L"á");
    EXPECT_EQ(engine.PeekRaw(), L"as");
    EXPECT_EQ(engine.PeekRawView(), std::wstring_view(L"as"));
}

TEST_F(RustInputEngineTest, ToneEscapeReported) {
    TypingConfig config;
    config.inputMethod = InputMethod::Telex;
    RustInputEngine engine(config);

    engine.PushChar(L'a');
    engine.PushChar(L's');
    engine.PushChar(L's');  // repeated tone key escapes -> literal "as"
    EXPECT_EQ(engine.Peek(), L"as");
    EXPECT_TRUE(engine.IsToneEscaped());
}

TEST_F(RustInputEngineTest, RepeatedWModifierEscapeSurvivesEnglishWordTail) {
    TypingConfig config;
    config.spellCheckEnabled = true;
    config.spellSuggestEnabled = true;
    config.autoRestoreEnabled = true;

    for (const InputMethod method : {
             InputMethod::Telex,
             InputMethod::SimpleTelex,
             InputMethod::Combined,
         }) {
        config.inputMethod = method;
        for (const auto& [raw, expected] : {
                 std::pair{std::wstring_view(L"dowwnload"), std::wstring_view(L"download")},
                 std::pair{std::wstring_view(L"powwershell"), std::wstring_view(L"powershell")},
             }) {
            RustInputEngine engine(config);
            for (const wchar_t c : raw) {
                engine.PushChar(c);
            }

            EXPECT_EQ(engine.Peek(), expected);
            EXPECT_TRUE(engine.IsToneEscaped());
            EXPECT_EQ(engine.PeekRaw(), raw);  // ESC restore still needs physical keys.
            EXPECT_EQ(engine.Commit(), expected);
        }
    }
}

TEST_F(RustInputEngineTest, EnglishWordFlag) {
    TypingConfig config;
    config.inputMethod = InputMethod::Telex;
    config.spellCheckEnabled = true;
    RustInputEngine engine(config);

    ASSERT_TRUE(engine.SeedFromText(L"hỏc"));  // hook tone + stop coda -> not Vietnamese
    EXPECT_TRUE(engine.IsEnglishWord());

    ASSERT_TRUE(engine.SeedFromText(L"việt"));
    EXPECT_FALSE(engine.IsEnglishWord());
}

TEST_F(RustInputEngineTest, SeedFromTextLiteralRestore) {
    TypingConfig config;
    config.inputMethod = InputMethod::Telex;
    RustInputEngine engine(config);

    ASSERT_TRUE(engine.SeedFromText(L"việt"));
    EXPECT_EQ(engine.Peek(), L"việt");
    EXPECT_EQ(engine.PeekRaw(), L"việt");
    EXPECT_EQ(engine.Count(), 4u);
    EXPECT_FALSE(engine.IsToneEscaped());  // seeding is not a tone escape
}

TEST_F(RustInputEngineTest, QuickConsonantReported) {
    TypingConfig config;
    config.inputMethod = InputMethod::Telex;
    config.quickConsonant = true;
    RustInputEngine engine(config);

    engine.PushChar(L'c');
    engine.PushChar(L'c');  // cc -> ch
    EXPECT_EQ(engine.Peek(), L"ch");
    EXPECT_TRUE(engine.HasActiveQuickConsonant());
}

TEST_F(RustInputEngineTest, LastCommitWasCorrectedReported) {
    TypingConfig config;
    config.inputMethod = InputMethod::Telex;
    config.spellCheckEnabled = true;
    config.spellSuggestEnabled = true;
    RustInputEngine engine(config);

    for (const wchar_t c : std::wstring(L"gnuwowif")) {
        engine.PushChar(c);
    }
    const std::wstring committed = engine.Commit();
    EXPECT_FALSE(committed.empty());
    EXPECT_TRUE(engine.LastCommitWasCorrected());
}

TEST_F(RustInputEngineTest, LastCommitWasCorrectedFalseWhenSpellSuggestDisabled) {
    TypingConfig config;
    config.inputMethod = InputMethod::Telex;
    config.spellCheckEnabled = true;
    config.spellSuggestEnabled = false;
    RustInputEngine engine(config);

    for (const wchar_t c : std::wstring(L"gnuwowif")) {
        engine.PushChar(c);
    }
    (void)engine.Commit();
    EXPECT_FALSE(engine.LastCommitWasCorrected());
}

// Regression: vkey_engine_set_spell_exclusions_utf16 is a process-global 2-arg
// (buf, len) FFI call with no engine handle. The adapter previously called it
// through a bogus 3-arg (engine, buf, len) function pointer, which shifts every
// argument register under the Win64 ABI -- Rust read the engine handle as `buf`
// and the real buffer's address as `len`, then read far out of bounds. That
// crashed instantly whenever config.spellExclusions was non-empty.
TEST_F(RustInputEngineTest, SpellExclusionsDoNotCrashConstruction) {
    TypingConfig config;
    config.inputMethod = InputMethod::Telex;
    config.spellCheckEnabled = true;
    config.spellSuggestEnabled = true;
    config.spellExclusions = {L"đcđt", L"hđ"};
    RustInputEngine engine(config);

    engine.PushChar(L'a');
    engine.PushChar(L'a');
    EXPECT_EQ(engine.Peek(), L"â");
}

// Proves the ABI v4 custom-keymap wiring end-to-end: a physical key remapped
// to a non-Telex/VNI action must actually apply that action through the Rust
// engine, not silently fall back to Telex (VKey-rs ADR-0007).
TEST_F(RustInputEngineTest, UserDefinedCustomKeymapAppliesRemappedAction) {
    TypingConfig config;
    config.inputMethod = InputMethod::UserDefined;
    config.customKeyMap[static_cast<uint8_t>(L'p')] = TypingAction::StrokeD;  // 'p' -> đ
    config.customKeyMap[static_cast<uint8_t>(L'q')] = TypingAction::ToneAcute;  // 'q' -> sắc
    RustInputEngine engine(config);

    for (const wchar_t c : std::wstring(L"dpaq")) {  // d, stroke(p), a, acute(q)
        engine.PushChar(c);
    }

    EXPECT_EQ(engine.Peek(), L"đá");
}

TEST_F(RustInputEngineTest, UserDefinedUnboundKeyStaysLiteral) {
    TypingConfig config;
    config.inputMethod = InputMethod::UserDefined;
    // No bindings installed: every key must type as itself.
    RustInputEngine engine(config);

    for (const wchar_t c : std::wstring(L"das")) {
        engine.PushChar(c);
    }

    EXPECT_EQ(engine.Peek(), L"das");
}

// Tier 2 (byte codes 18-34): Unikey-compatibility actions. Proves the newly
// vendored engine actually decodes these, not just the tier-1 Telex/VNI remap.
TEST_F(RustInputEngineTest, UserDefinedTier2DirectInsertAndHornOrInsertU) {
    TypingConfig config;
    config.inputMethod = InputMethod::UserDefined;
    config.customKeyMap[static_cast<uint8_t>(L'p')] = TypingAction::InsertDStroke;    // 'p' -> đ
    config.customKeyMap[static_cast<uint8_t>(L'y')] = TypingAction::HornOrInsertU;    // 'y' -> ư (or horn on u/o)
    RustInputEngine engine(config);

    engine.PushChar(L'p');
    EXPECT_EQ(engine.Peek(), L"đ");
    engine.PushChar(L'p');  // repeat escapes back to literal 'p'
    EXPECT_EQ(engine.Peek(), L"p");

    engine.Reset();
    engine.PushChar(L'y');  // no target -> standalone insert fallback
    EXPECT_EQ(engine.Peek(), L"ư");
}

// #221 parity: vowel-less abbreviation chain (PLHĐ) must compose despite the
// pl- hard-English onset; a vowel in the buffer keeps the block (pladd literal).
// C++ twin: TelexEngineTest.StrokeD_AbbrevChain_*.
TEST_F(RustInputEngineTest, StrokeD_AbbrevChain_PLHD_Parity) {
    TypingConfig config;
    config.inputMethod = InputMethod::Telex;
    config.spellCheckEnabled = true;
    config.spellSuggestEnabled = true;
    RustInputEngine engine(config);

    for (wchar_t c : std::wstring(L"plhdd")) engine.PushChar(c);
    EXPECT_EQ(engine.Peek(), L"plhđ");

    engine.Reset();
    for (wchar_t c : std::wstring(L"pladd")) engine.PushChar(c);
    EXPECT_EQ(engine.Peek(), L"pladd");
}

}  // namespace
}  // namespace NextKey

#endif  // VKEY_USE_RUST_ENGINE
