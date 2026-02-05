// NexusKey - ConfigManager Tests
// SPDX-License-Identifier: GPL-3.0-only

#include <gtest/gtest.h>
#include "core/config/ConfigManager.h"
#include <fstream>
#include <filesystem>

namespace NextKey {
namespace {

class ConfigManagerTest : public ::testing::Test {
protected:
    void SetUp() override {
        testConfigPath_ = L"test_config.toml";
    }
    
    void TearDown() override {
        // Clean up test file
        std::filesystem::remove(std::filesystem::path(testConfigPath_));
    }
    
    void WriteTestConfig(const std::string& content) {
        std::ofstream file("test_config.toml");
        file << content;
        file.close();
    }
    
    std::wstring testConfigPath_;
};

// ============================================================================
// Loading Tests
// ============================================================================

TEST_F(ConfigManagerTest, LoadFromFile_DefaultTelex) {
    WriteTestConfig(R"(
[input]
method = "telex"

[features]
spell_check = false
optimize_level = 0
)");
    
    auto config = ConfigManager::LoadFromFile(testConfigPath_);
    ASSERT_TRUE(config.has_value());
    EXPECT_EQ(config->inputMethod, InputMethod::Telex);
    EXPECT_FALSE(config->spellCheckEnabled);
    EXPECT_EQ(config->optimizeLevel, 0);
}

TEST_F(ConfigManagerTest, LoadFromFile_VNI) {
    WriteTestConfig(R"(
[input]
method = "vni"

[features]
spell_check = true
optimize_level = 2
)");
    
    auto config = ConfigManager::LoadFromFile(testConfigPath_);
    ASSERT_TRUE(config.has_value());
    EXPECT_EQ(config->inputMethod, InputMethod::VNI);
    EXPECT_TRUE(config->spellCheckEnabled);
    EXPECT_EQ(config->optimizeLevel, 2);
}

TEST_F(ConfigManagerTest, LoadFromFile_MissingFile) {
    auto config = ConfigManager::LoadFromFile(L"nonexistent_config.toml");
    EXPECT_FALSE(config.has_value());
}

TEST_F(ConfigManagerTest, LoadFromFile_InvalidToml) {
    WriteTestConfig("this is not valid toml {{{{");
    
    auto config = ConfigManager::LoadFromFile(testConfigPath_);
    EXPECT_FALSE(config.has_value());
}

TEST_F(ConfigManagerTest, LoadOrDefault_NoFile) {
    // LoadOrDefault should return compiled defaults when no file exists
    auto config = ConfigManager::LoadOrDefault();
    
    // Defaults are Telex, no spell check, optimize 0
    EXPECT_EQ(config.inputMethod, InputMethod::Telex);
    EXPECT_FALSE(config.spellCheckEnabled);
    EXPECT_EQ(config.optimizeLevel, 0);
}

// ============================================================================
// Saving Tests
// ============================================================================

TEST_F(ConfigManagerTest, SaveToFile_Telex) {
    TypingConfig config;
    config.inputMethod = InputMethod::Telex;
    config.spellCheckEnabled = false;
    config.optimizeLevel = 1;
    
    EXPECT_TRUE(ConfigManager::SaveToFile(testConfigPath_, config));
    
    // Reload and verify
    auto loaded = ConfigManager::LoadFromFile(testConfigPath_);
    ASSERT_TRUE(loaded.has_value());
    EXPECT_EQ(loaded->inputMethod, InputMethod::Telex);
    EXPECT_EQ(loaded->optimizeLevel, 1);
}

TEST_F(ConfigManagerTest, SaveToFile_VNI) {
    TypingConfig config;
    config.inputMethod = InputMethod::VNI;
    config.spellCheckEnabled = true;
    
    EXPECT_TRUE(ConfigManager::SaveToFile(testConfigPath_, config));
    
    auto loaded = ConfigManager::LoadFromFile(testConfigPath_);
    ASSERT_TRUE(loaded.has_value());
    EXPECT_EQ(loaded->inputMethod, InputMethod::VNI);
    EXPECT_TRUE(loaded->spellCheckEnabled);
}

// ============================================================================
// Path Resolution Tests
// ============================================================================

TEST_F(ConfigManagerTest, GetConfigPath_NotEmpty) {
    auto path = ConfigManager::GetConfigPath();
    EXPECT_FALSE(path.empty());
    EXPECT_TRUE(path.find(L"config.toml") != std::wstring::npos);
}

}  // namespace
}  // namespace NextKey
