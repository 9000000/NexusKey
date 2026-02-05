// NexusKey - EngineFactory Tests
// SPDX-License-Identifier: GPL-3.0-only

#include <gtest/gtest.h>
#include "core/engine/EngineFactory.h"
#include "core/engine/TelexEngine.h"
#include "core/engine/VniEngine.h"

namespace NextKey {
namespace {

class EngineFactoryTest : public ::testing::Test {};

// ============================================================================
// Factory Creation Tests
// ============================================================================

TEST_F(EngineFactoryTest, Create_Telex_FromConfig) {
    TypingConfig config;
    config.inputMethod = InputMethod::Telex;
    
    auto engine = EngineFactory::Create(config);
    ASSERT_NE(engine, nullptr);
    
    // Verify it's Telex by checking behavior (aa -> â)
    engine->PushChar(L'a');
    engine->PushChar(L'a');
    EXPECT_EQ(engine->Peek(), L"â");
}

TEST_F(EngineFactoryTest, Create_VNI_FromConfig) {
    TypingConfig config;
    config.inputMethod = InputMethod::VNI;
    
    auto engine = EngineFactory::Create(config);
    ASSERT_NE(engine, nullptr);
    
    // Verify it's VNI by checking behavior (a6 -> â)
    engine->PushChar(L'a');
    engine->PushChar(L'6');
    EXPECT_EQ(engine->Peek(), L"â");
}

TEST_F(EngineFactoryTest, Create_Telex_FromMethod) {
    auto engine = EngineFactory::Create(InputMethod::Telex);
    ASSERT_NE(engine, nullptr);
    
    engine->PushChar(L'e');
    engine->PushChar(L'e');
    EXPECT_EQ(engine->Peek(), L"ê");
}

TEST_F(EngineFactoryTest, Create_VNI_FromMethod) {
    auto engine = EngineFactory::Create(InputMethod::VNI);
    ASSERT_NE(engine, nullptr);
    
    engine->PushChar(L'e');
    engine->PushChar(L'6');
    EXPECT_EQ(engine->Peek(), L"ê");
}

// ============================================================================
// Runtime Switching Simulation Tests
// ============================================================================

TEST_F(EngineFactoryTest, RuntimeSwitch_TelexToVNI) {
    auto telexEngine = EngineFactory::Create(InputMethod::Telex);
    telexEngine->PushChar(L'a');
    telexEngine->PushChar(L'a');
    EXPECT_EQ(telexEngine->Peek(), L"â");
    
    // Simulate commit before switch
    std::wstring committed = telexEngine->Commit();
    EXPECT_EQ(committed, L"â");
    
    // Create VNI engine (like switching)
    auto vniEngine = EngineFactory::Create(InputMethod::VNI);
    vniEngine->PushChar(L'a');
    vniEngine->PushChar(L'6');
    EXPECT_EQ(vniEngine->Peek(), L"â");
}

TEST_F(EngineFactoryTest, RuntimeSwitch_VNIToTelex) {
    auto vniEngine = EngineFactory::Create(InputMethod::VNI);
    vniEngine->PushChar(L'd');
    vniEngine->PushChar(L'9');
    EXPECT_EQ(vniEngine->Peek(), L"đ");
    
    std::wstring committed = vniEngine->Commit();
    EXPECT_EQ(committed, L"đ");
    
    // Create Telex engine
    auto telexEngine = EngineFactory::Create(InputMethod::Telex);
    telexEngine->PushChar(L'd');
    telexEngine->PushChar(L'd');
    EXPECT_EQ(telexEngine->Peek(), L"đ");
}

// ============================================================================
// Default Behavior Tests
// ============================================================================

TEST_F(EngineFactoryTest, DefaultConfig_IsTelex) {
    TypingConfig config;  // Default constructor
    
    auto engine = EngineFactory::Create(config);
    
    // Default should be Telex (aa -> â)
    engine->PushChar(L'a');
    engine->PushChar(L'a');
    EXPECT_EQ(engine->Peek(), L"â");
}

}  // namespace
}  // namespace NextKey
