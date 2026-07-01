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

}  // namespace
}  // namespace NextKey

#endif  // VKEY_USE_RUST_ENGINE
