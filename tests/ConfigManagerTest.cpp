// VKey - ConfigManager Tests
// SPDX-License-Identifier: GPL-3.0-only

#include <gtest/gtest.h>
#include "core/config/ConfigManager.h"
#include "core/UIConfig.h"
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

TEST_F(ConfigManagerTest, SaveToFile_PreservesUISection) {
    // First save UI config
    UIConfig uiConfig;
    uiConfig.showAdvanced = true;
    uiConfig.backgroundOpacity = 65;
    EXPECT_TRUE(ConfigManager::SaveUIConfig(testConfigPath_, uiConfig));

    // Now save typing config - should preserve [ui] section
    TypingConfig typingConfig;
    typingConfig.inputMethod = InputMethod::VNI;
    typingConfig.spellCheckEnabled = true;
    EXPECT_TRUE(ConfigManager::SaveToFile(testConfigPath_, typingConfig));

    // Verify UI config is preserved
    auto loadedUI = ConfigManager::LoadUIConfig(testConfigPath_);
    ASSERT_TRUE(loadedUI.has_value());
    EXPECT_TRUE(loadedUI->showAdvanced);
    EXPECT_EQ(loadedUI->backgroundOpacity, 65);

    // Verify typing config was saved
    auto loadedTyping = ConfigManager::LoadFromFile(testConfigPath_);
    ASSERT_TRUE(loadedTyping.has_value());
    EXPECT_EQ(loadedTyping->inputMethod, InputMethod::VNI);
}

// ============================================================================
// Path Resolution Tests
// ============================================================================

TEST_F(ConfigManagerTest, GetConfigPath_NotEmpty) {
    auto path = ConfigManager::GetConfigPath();
    EXPECT_FALSE(path.empty());
    EXPECT_TRUE(path.find(L"config.toml") != std::wstring::npos);
}

// ============================================================================
// UIConfig Loading Tests
// ============================================================================

TEST_F(ConfigManagerTest, LoadUIConfig_DefaultValues) {
    WriteTestConfig(R"(
[ui]
show_advanced = false
background_opacity = 80
dark_mode = true
pinned = false
)");

    auto config = ConfigManager::LoadUIConfig(testConfigPath_);
    ASSERT_TRUE(config.has_value());
    EXPECT_FALSE(config->showAdvanced);
    EXPECT_EQ(config->backgroundOpacity, 80);
    EXPECT_FALSE(config->pinned);
}

TEST_F(ConfigManagerTest, LoadUIConfig_CustomValues) {
    WriteTestConfig(R"(
[ui]
show_advanced = true
background_opacity = 50
pinned = true
)");

    auto config = ConfigManager::LoadUIConfig(testConfigPath_);
    ASSERT_TRUE(config.has_value());
    EXPECT_TRUE(config->showAdvanced);
    EXPECT_EQ(config->backgroundOpacity, 50);
    EXPECT_TRUE(config->pinned);
}

TEST_F(ConfigManagerTest, LoadUIConfig_MissingFile) {
    auto config = ConfigManager::LoadUIConfig(L"nonexistent_config.toml");
    EXPECT_FALSE(config.has_value());
}

TEST_F(ConfigManagerTest, LoadUIConfig_MissingUISection) {
    WriteTestConfig(R"(
[input]
method = "telex"
)");

    auto config = ConfigManager::LoadUIConfig(testConfigPath_);
    // Should return config with default values when [ui] section is missing
    ASSERT_TRUE(config.has_value());
    EXPECT_FALSE(config->showAdvanced);
    EXPECT_EQ(config->backgroundOpacity, 80);
}

TEST_F(ConfigManagerTest, LoadUIConfig_FallbackToDefaults) {
    // When LoadUIConfig returns nullopt, caller should use UIConfig defaults
    auto config = ConfigManager::LoadUIConfig(L"nonexistent_file.toml");

    // Simulate what LoadUIConfigOrDefault does
    UIConfig result = config.value_or(UIConfig{});

    EXPECT_FALSE(result.showAdvanced);
    EXPECT_EQ(result.backgroundOpacity, 80);
    EXPECT_FALSE(result.pinned);
}

// ============================================================================
// UIConfig Saving Tests
// ============================================================================

TEST_F(ConfigManagerTest, SaveUIConfig_Basic) {
    UIConfig config;
    config.showAdvanced = true;
    config.backgroundOpacity = 60;
    config.pinned = true;

    EXPECT_TRUE(ConfigManager::SaveUIConfig(testConfigPath_, config));

    // Reload and verify
    auto loaded = ConfigManager::LoadUIConfig(testConfigPath_);
    ASSERT_TRUE(loaded.has_value());
    EXPECT_TRUE(loaded->showAdvanced);
    EXPECT_EQ(loaded->backgroundOpacity, 60);
    EXPECT_TRUE(loaded->pinned);
}

TEST_F(ConfigManagerTest, SaveUIConfig_PreservesOtherSections) {
    // Write initial config with [input] section
    WriteTestConfig(R"(
[input]
method = "vni"

[features]
spell_check = true
optimize_level = 2
)");

    // Save UI config
    UIConfig uiConfig;
    uiConfig.showAdvanced = true;
    uiConfig.backgroundOpacity = 75;
    EXPECT_TRUE(ConfigManager::SaveUIConfig(testConfigPath_, uiConfig));

    // Verify [input] section is preserved
    auto typingConfig = ConfigManager::LoadFromFile(testConfigPath_);
    ASSERT_TRUE(typingConfig.has_value());
    EXPECT_EQ(typingConfig->inputMethod, InputMethod::VNI);
    EXPECT_TRUE(typingConfig->spellCheckEnabled);

    // Verify UI config was saved
    auto loadedUI = ConfigManager::LoadUIConfig(testConfigPath_);
    ASSERT_TRUE(loadedUI.has_value());
    EXPECT_TRUE(loadedUI->showAdvanced);
    EXPECT_EQ(loadedUI->backgroundOpacity, 75);
}

TEST_F(ConfigManagerTest, SaveUIConfig_OpacityBoundaries) {
    // Test minimum opacity
    UIConfig configMin;
    configMin.backgroundOpacity = 0;
    EXPECT_TRUE(ConfigManager::SaveUIConfig(testConfigPath_, configMin));
    auto loadedMin = ConfigManager::LoadUIConfig(testConfigPath_);
    ASSERT_TRUE(loadedMin.has_value());
    EXPECT_EQ(loadedMin->backgroundOpacity, 0);

    // Test maximum opacity
    UIConfig configMax;
    configMax.backgroundOpacity = 100;
    EXPECT_TRUE(ConfigManager::SaveUIConfig(testConfigPath_, configMax));
    auto loadedMax = ConfigManager::LoadUIConfig(testConfigPath_);
    ASSERT_TRUE(loadedMax.has_value());
    EXPECT_EQ(loadedMax->backgroundOpacity, 100);
}

// ============================================================================
// UIConfig Default Values Test
// ============================================================================

TEST_F(ConfigManagerTest, UIConfig_DefaultConstructor) {
    UIConfig config;

    EXPECT_FALSE(config.showAdvanced);
    EXPECT_EQ(config.backgroundOpacity, 80);
    EXPECT_FALSE(config.pinned);
}

}  // namespace
}  // namespace NextKey
