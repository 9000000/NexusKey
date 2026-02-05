// NexusKey - VNI Engine Tests
// SPDX-License-Identifier: GPL-3.0-only

#include <gtest/gtest.h>
#include "core/engine/VniEngine.h"

namespace NextKey {
namespace Vni {
namespace {

class VniEngineTest : public ::testing::Test {
protected:
    void SetUp() override {
        engine_ = std::make_unique<VniEngine>();
    }
    
    void TypeString(VniEngine& engine, const std::wstring& input) {
        for (wchar_t c : input) {
            engine.PushChar(c);
        }
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

}  // namespace
}  // namespace Vni
}  // namespace NextKey
