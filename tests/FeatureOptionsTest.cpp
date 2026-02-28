// NexusKey - Feature Options Tests (modernOrtho, autoCaps, allowZwjf)
// SPDX-License-Identifier: GPL-3.0-only
//
// Tests for the 3 feature flags:
//   1. modernOrtho  — Modern tone placement on diphthongs (ua→uá, ue→ué)
//   2. autoCaps     — (config round-trip only; auto-capitalize logic lives in TSF DLL)
//   3. allowZwjf    — (config round-trip only; consonant validation for future use)

#include <gtest/gtest.h>
#include "core/engine/TelexEngine.h"
#include "core/engine/VniEngine.h"
#include "core/TypingConfig.h"
#include "core/SharedState.h"
#ifdef _WIN32
#include "core/config/ConfigManager.h"
#include <fstream>
#include <filesystem>
#endif

namespace NextKey {
namespace {

// ============================================================================
// Helper
// ============================================================================

void TypeString(Telex::TelexEngine& engine, const wchar_t* input) {
    for (const wchar_t* p = input; *p; ++p) {
        engine.PushChar(*p);
    }
}

void TypeString(Vni::VniEngine& engine, const wchar_t* input) {
    for (const wchar_t* p = input; *p; ++p) {
        engine.PushChar(*p);
    }
}

// ============================================================================
// TypingConfig Defaults
// ============================================================================

TEST(FeatureOptionsDefaults, NewFieldsDefaults) {
    TypingConfig config;
    EXPECT_FALSE(config.modernOrtho);
    EXPECT_FALSE(config.autoCaps);
    EXPECT_TRUE(config.allowZwjf);  // Default: tone keys enabled (normal Vietnamese)
}

// ============================================================================
// SharedState FeatureFlags Bitmask
// ============================================================================

class FeatureFlagsTest : public ::testing::Test {};

TEST_F(FeatureFlagsTest, InitDefaults_FeatureFlagsAllowZwjf) {
    SharedState state{};
    state.InitDefaults();
    EXPECT_EQ(state.featureFlags, FeatureFlags::ALLOW_ZWJF);
}

TEST_F(FeatureFlagsTest, Encode_ModernOrtho) {
    SharedState state{};
    state.InitDefaults();
    state.featureFlags |= FeatureFlags::MODERN_ORTHO;

    EXPECT_TRUE(state.featureFlags & FeatureFlags::MODERN_ORTHO);
    EXPECT_FALSE(state.featureFlags & FeatureFlags::AUTO_CAPS);
    EXPECT_TRUE(state.featureFlags & FeatureFlags::ALLOW_ZWJF);  // Default ON
}

TEST_F(FeatureFlagsTest, Encode_AutoCaps) {
    SharedState state{};
    state.InitDefaults();
    state.featureFlags |= FeatureFlags::AUTO_CAPS;

    EXPECT_FALSE(state.featureFlags & FeatureFlags::MODERN_ORTHO);
    EXPECT_TRUE(state.featureFlags & FeatureFlags::AUTO_CAPS);
    EXPECT_TRUE(state.featureFlags & FeatureFlags::ALLOW_ZWJF);  // Default ON
}

TEST_F(FeatureFlagsTest, Encode_AllFlags) {
    SharedState state{};
    state.InitDefaults();
    state.featureFlags = FeatureFlags::MODERN_ORTHO | FeatureFlags::AUTO_CAPS | FeatureFlags::ALLOW_ZWJF;

    EXPECT_TRUE(state.featureFlags & FeatureFlags::MODERN_ORTHO);
    EXPECT_TRUE(state.featureFlags & FeatureFlags::AUTO_CAPS);
    EXPECT_TRUE(state.featureFlags & FeatureFlags::ALLOW_ZWJF);
    EXPECT_EQ(state.featureFlags, 0x07);
}

TEST_F(FeatureFlagsTest, Decode_ToConfig) {
    SharedState state{};
    state.InitDefaults();
    state.featureFlags = FeatureFlags::MODERN_ORTHO | FeatureFlags::ALLOW_ZWJF;

    TypingConfig config;
    DecodeFeatureFlags(state.featureFlags, config);

    EXPECT_TRUE(config.modernOrtho);
    EXPECT_FALSE(config.autoCaps);
    EXPECT_TRUE(config.allowZwjf);
}

TEST_F(FeatureFlagsTest, RoundTrip_ConfigToStateToConfig) {
    // Encode
    TypingConfig original;
    original.modernOrtho = true;
    original.autoCaps = true;
    original.allowZwjf = false;

    SharedState state{};
    state.InitDefaults();
    state.featureFlags = EncodeFeatureFlags(original);

    // Decode
    TypingConfig decoded;
    DecodeFeatureFlags(state.featureFlags, decoded);

    EXPECT_EQ(original.modernOrtho, decoded.modernOrtho);
    EXPECT_EQ(original.autoCaps, decoded.autoCaps);
    EXPECT_EQ(original.allowZwjf, decoded.allowZwjf);
}

// ============================================================================
// ConfigManager Round-Trip Tests (Windows only — needs Windows.h)
// ============================================================================

#ifdef _WIN32

class FeatureConfigTest : public ::testing::Test {
protected:
    void SetUp() override {
        testConfigPath_ = L"test_feature_config.toml";
    }
    void TearDown() override {
        std::filesystem::remove(std::filesystem::path(testConfigPath_));
    }
    void WriteTestConfig(const std::string& content) {
        std::ofstream file("test_feature_config.toml");
        file << content;
        file.close();
    }
    std::wstring testConfigPath_;
};

TEST_F(FeatureConfigTest, Load_AllFeaturesTrue) {
    WriteTestConfig(R"(
[input]
method = "telex"

[features]
modern_ortho = true
auto_caps = true
allow_zwjf = true
)");

    auto config = ConfigManager::LoadFromFile(testConfigPath_);
    ASSERT_TRUE(config.has_value());
    EXPECT_TRUE(config->modernOrtho);
    EXPECT_TRUE(config->autoCaps);
    EXPECT_TRUE(config->allowZwjf);
}

TEST_F(FeatureConfigTest, Load_AllFeaturesFalse) {
    WriteTestConfig(R"(
[input]
method = "telex"

[features]
modern_ortho = false
auto_caps = false
allow_zwjf = false
)");

    auto config = ConfigManager::LoadFromFile(testConfigPath_);
    ASSERT_TRUE(config.has_value());
    EXPECT_FALSE(config->modernOrtho);
    EXPECT_FALSE(config->autoCaps);
    EXPECT_FALSE(config->allowZwjf);
}

TEST_F(FeatureConfigTest, Load_MissingFeatureFields_Defaults) {
    WriteTestConfig(R"(
[input]
method = "telex"

[features]
spell_check = true
)");

    auto config = ConfigManager::LoadFromFile(testConfigPath_);
    ASSERT_TRUE(config.has_value());
    EXPECT_FALSE(config->modernOrtho);
    EXPECT_FALSE(config->autoCaps);
    EXPECT_TRUE(config->allowZwjf);  // Default: tone keys enabled
}

TEST_F(FeatureConfigTest, Load_NoFeaturesSection_Defaults) {
    WriteTestConfig(R"(
[input]
method = "vni"
)");

    auto config = ConfigManager::LoadFromFile(testConfigPath_);
    ASSERT_TRUE(config.has_value());
    EXPECT_FALSE(config->modernOrtho);
    EXPECT_FALSE(config->autoCaps);
    EXPECT_TRUE(config->allowZwjf);  // Default: tone keys enabled
}

TEST_F(FeatureConfigTest, SaveAndReload_AllTrue) {
    TypingConfig config;
    config.modernOrtho = true;
    config.autoCaps = true;
    config.allowZwjf = true;

    EXPECT_TRUE(ConfigManager::SaveToFile(testConfigPath_, config));

    auto loaded = ConfigManager::LoadFromFile(testConfigPath_);
    ASSERT_TRUE(loaded.has_value());
    EXPECT_TRUE(loaded->modernOrtho);
    EXPECT_TRUE(loaded->autoCaps);
    EXPECT_TRUE(loaded->allowZwjf);
}

TEST_F(FeatureConfigTest, SaveAndReload_Mixed) {
    TypingConfig config;
    config.modernOrtho = true;
    config.autoCaps = false;
    config.allowZwjf = true;

    EXPECT_TRUE(ConfigManager::SaveToFile(testConfigPath_, config));

    auto loaded = ConfigManager::LoadFromFile(testConfigPath_);
    ASSERT_TRUE(loaded.has_value());
    EXPECT_TRUE(loaded->modernOrtho);
    EXPECT_FALSE(loaded->autoCaps);
    EXPECT_TRUE(loaded->allowZwjf);
}

TEST_F(FeatureConfigTest, SavePreservesOtherFeatures) {
    // Write initial config with existing features
    WriteTestConfig(R"(
[input]
method = "telex"

[features]
spell_check = true
optimize_level = 2
)");

    // Load, modify new fields, save
    auto config = ConfigManager::LoadFromFile(testConfigPath_);
    ASSERT_TRUE(config.has_value());
    config->modernOrtho = true;
    config->autoCaps = true;
    EXPECT_TRUE(ConfigManager::SaveToFile(testConfigPath_, *config));

    // Reload and verify old fields preserved
    auto loaded = ConfigManager::LoadFromFile(testConfigPath_);
    ASSERT_TRUE(loaded.has_value());
    EXPECT_TRUE(loaded->spellCheckEnabled);
    EXPECT_EQ(loaded->optimizeLevel, 2);
    EXPECT_TRUE(loaded->modernOrtho);
    EXPECT_TRUE(loaded->autoCaps);
    EXPECT_TRUE(loaded->allowZwjf);  // Default: tone keys enabled
}

#endif  // _WIN32

// ============================================================================
// Modern Tone Placement — Telex Engine
// ============================================================================

class TelexModernOrthoTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Classic engine (default)
        classicConfig_.inputMethod = InputMethod::Telex;
        classicConfig_.modernOrtho = false;
        classic_ = std::make_unique<Telex::TelexEngine>(classicConfig_);

        // Modern engine
        modernConfig_.inputMethod = InputMethod::Telex;
        modernConfig_.modernOrtho = true;
        modern_ = std::make_unique<Telex::TelexEngine>(modernConfig_);
    }

    void ResetEngines() {
        classic_->Reset();
        modern_->Reset();
    }

    TypingConfig classicConfig_;
    TypingConfig modernConfig_;
    std::unique_ptr<Telex::TelexEngine> classic_;
    std::unique_ptr<Telex::TelexEngine> modern_;
};

// --- Key difference: "ua" diphthong ---

TEST_F(TelexModernOrthoTest, UA_Acute_Classic_ToneOnFirst) {
    // Classic: "ua" + s → "úa" (tone on u)
    TypeString(*classic_, L"uas");
    EXPECT_EQ(classic_->Peek(), L"úa");
}

TEST_F(TelexModernOrthoTest, UA_Acute_Modern_ToneOnSecond) {
    // Modern: "ua" + s → "uá" (tone on a)
    TypeString(*modern_, L"uas");
    EXPECT_EQ(modern_->Peek(), L"uá");
}

TEST_F(TelexModernOrthoTest, UA_Grave_Classic_ToneOnFirst) {
    TypeString(*classic_, L"uaf");
    EXPECT_EQ(classic_->Peek(), L"ùa");
}

TEST_F(TelexModernOrthoTest, UA_Grave_Modern_ToneOnSecond) {
    TypeString(*modern_, L"uaf");
    EXPECT_EQ(modern_->Peek(), L"uà");
}

TEST_F(TelexModernOrthoTest, Mua_Classic) {
    TypeString(*classic_, L"muaf");
    EXPECT_EQ(classic_->Peek(), L"mùa");
}

TEST_F(TelexModernOrthoTest, Mua_Modern) {
    TypeString(*modern_, L"muaf");
    EXPECT_EQ(modern_->Peek(), L"muà");
}

// --- Key difference: "ue" diphthong ---

TEST_F(TelexModernOrthoTest, UE_Acute_Classic_ToneOnFirst) {
    // Classic: "ue" + s → "úe"
    TypeString(*classic_, L"ues");
    EXPECT_EQ(classic_->Peek(), L"úe");
}

TEST_F(TelexModernOrthoTest, UE_Acute_Modern_ToneOnSecond) {
    // Modern: "ue" + s → "ué"
    TypeString(*modern_, L"ues");
    EXPECT_EQ(modern_->Peek(), L"ué");
}

// --- "oa", "oe" should be SAME in both modes (tone on second) ---

TEST_F(TelexModernOrthoTest, OA_Acute_Classic_ToneOnFirst) {
    // Classic: "oa" → tone on 'o' (old-style: hóa)
    TypeString(*classic_, L"hoas");
    EXPECT_EQ(classic_->Peek(), L"hóa");
}

TEST_F(TelexModernOrthoTest, OA_Acute_Modern_ToneOnSecond) {
    // Modern: "oa" → tone on 'a' (new-style: hoá)
    TypeString(*modern_, L"hoas");
    EXPECT_EQ(modern_->Peek(), L"hoá");
}

TEST_F(TelexModernOrthoTest, OA_Grave_Classic) {
    // Classic: hòa (tone on o)
    TypeString(*classic_, L"hoaf");
    EXPECT_EQ(classic_->Peek(), L"hòa");
}

TEST_F(TelexModernOrthoTest, OA_Grave_Modern) {
    // Modern: hoà (tone on a)
    TypeString(*modern_, L"hoaf");
    EXPECT_EQ(modern_->Peek(), L"hoà");
}

TEST_F(TelexModernOrthoTest, OE_Classic_ToneOnFirst) {
    // Classic: "oe" → tone on 'o' (old-style: hóe)
    TypeString(*classic_, L"hoes");
    EXPECT_EQ(classic_->Peek(), L"hóe");
}

TEST_F(TelexModernOrthoTest, OE_Modern_ToneOnSecond) {
    // Modern: "oe" → tone on 'e' (new-style: hoé)
    TypeString(*modern_, L"hoes");
    EXPECT_EQ(modern_->Peek(), L"hoé");
}

// --- "uy" should be SAME in both modes (tone on second) ---

TEST_F(TelexModernOrthoTest, UY_Grave_Both) {
    TypeString(*classic_, L"uyf");
    std::wstring classicResult = classic_->Peek();

    TypeString(*modern_, L"uyf");
    std::wstring modernResult = modern_->Peek();

    EXPECT_EQ(classicResult, L"uỳ");
    EXPECT_EQ(modernResult, L"uỳ");
}

// --- Falling diphthongs: SAME in both modes (tone on first) ---

TEST_F(TelexModernOrthoTest, AI_ToneOnFirst_Both) {
    TypeString(*classic_, L"ais");
    EXPECT_EQ(classic_->Peek(), L"ái");

    TypeString(*modern_, L"ais");
    EXPECT_EQ(modern_->Peek(), L"ái");
}

TEST_F(TelexModernOrthoTest, OI_ToneOnFirst_Both) {
    TypeString(*classic_, L"oif");
    EXPECT_EQ(classic_->Peek(), L"òi");

    TypeString(*modern_, L"oif");
    EXPECT_EQ(modern_->Peek(), L"òi");
}

TEST_F(TelexModernOrthoTest, AU_ToneOnFirst_Both) {
    TypeString(*classic_, L"aur");
    EXPECT_EQ(classic_->Peek(), L"ảu");

    TypeString(*modern_, L"aur");
    EXPECT_EQ(modern_->Peek(), L"ảu");
}

// --- Modified vowels still take priority in both modes ---

TEST_F(TelexModernOrthoTest, CircumflexTakesPriority_Classic) {
    TypeString(*classic_, L"aas");  // â + s → ấ
    EXPECT_EQ(classic_->Peek(), L"ấ");
}

TEST_F(TelexModernOrthoTest, CircumflexTakesPriority_Modern) {
    TypeString(*modern_, L"aas");
    EXPECT_EQ(modern_->Peek(), L"ấ");
}

TEST_F(TelexModernOrthoTest, HornTakesPriority_Classic) {
    TypeString(*classic_, L"uwf");  // ư + f → ừ
    EXPECT_EQ(classic_->Peek(), L"ừ");
}

TEST_F(TelexModernOrthoTest, HornTakesPriority_Modern) {
    TypeString(*modern_, L"uwf");
    EXPECT_EQ(modern_->Peek(), L"ừ");
}

// --- Single vowel: SAME in both modes ---

TEST_F(TelexModernOrthoTest, SingleVowel_A_Both) {
    TypeString(*classic_, L"as");
    EXPECT_EQ(classic_->Peek(), L"á");

    TypeString(*modern_, L"as");
    EXPECT_EQ(modern_->Peek(), L"á");
}

// --- Triphthongs: modern puts tone on middle ---

TEST_F(TelexModernOrthoTest, Triphthong_OAI_Modern_ToneOnMiddle) {
    TypeString(*modern_, L"oais");
    EXPECT_EQ(modern_->Peek(), L"oái");
}

TEST_F(TelexModernOrthoTest, Triphthong_UYA_Modern_ToneOnMiddle) {
    // Middle of u-y-a is 'y', so tone goes on 'y'
    TypeString(*modern_, L"uyas");
    EXPECT_EQ(modern_->Peek(), L"uýa");
}

// --- Real words ---

TEST_F(TelexModernOrthoTest, Word_Hoa_Modern) {
    TypeString(*modern_, L"hoaf");
    EXPECT_EQ(modern_->Peek(), L"hoà");
}

TEST_F(TelexModernOrthoTest, Word_Qua_Modern) {
    // "qua" — q+u is consonant cluster, 'a' is the only vowel
    TypeString(*modern_, L"quas");
    EXPECT_EQ(modern_->Peek(), L"quá");
}

TEST_F(TelexModernOrthoTest, Word_Cua_Modern) {
    // Modern: "cua" + f → "cuà" (tone on a)
    TypeString(*modern_, L"cuaf");
    EXPECT_EQ(modern_->Peek(), L"cuà");
}

TEST_F(TelexModernOrthoTest, Word_Cua_Classic) {
    // Classic: "cua" + f → "cùa" (tone on u)
    TypeString(*classic_, L"cuaf");
    EXPECT_EQ(classic_->Peek(), L"cùa");
}

TEST_F(TelexModernOrthoTest, Word_Thua_Modern) {
    TypeString(*modern_, L"thuas");
    EXPECT_EQ(modern_->Peek(), L"thuá");
}

TEST_F(TelexModernOrthoTest, Word_Thua_Classic) {
    TypeString(*classic_, L"thuas");
    EXPECT_EQ(classic_->Peek(), L"thúa");
}

// --- "uo" in modern → tone on second ---

TEST_F(TelexModernOrthoTest, UO_Modern_ToneOnSecond) {
    TypeString(*modern_, L"uos");
    EXPECT_EQ(modern_->Peek(), L"uó");
}

// ============================================================================
// Modern Tone Placement — VNI Engine
// ============================================================================

class VniModernOrthoTest : public ::testing::Test {
protected:
    void SetUp() override {
        classicConfig_.inputMethod = InputMethod::VNI;
        classicConfig_.modernOrtho = false;
        classic_ = std::make_unique<Vni::VniEngine>(classicConfig_);

        modernConfig_.inputMethod = InputMethod::VNI;
        modernConfig_.modernOrtho = true;
        modern_ = std::make_unique<Vni::VniEngine>(modernConfig_);
    }

    TypingConfig classicConfig_;
    TypingConfig modernConfig_;
    std::unique_ptr<Vni::VniEngine> classic_;
    std::unique_ptr<Vni::VniEngine> modern_;
};

TEST_F(VniModernOrthoTest, UA_Classic_ToneOnFirst) {
    // VNI classic: diphthong-aware, "ua" → tone on 'u' (old-style)
    TypeString(*classic_, L"ua1");
    EXPECT_EQ(classic_->Peek(), L"úa");
}

TEST_F(VniModernOrthoTest, UA_Modern_ToneOnSecond) {
    // VNI modern: diphthong-aware → tone on 'a'
    TypeString(*modern_, L"ua1");
    EXPECT_EQ(modern_->Peek(), L"uá");
}

TEST_F(VniModernOrthoTest, OA_Classic_ToneOnFirst) {
    // VNI classic: "oa" → tone on 'o' (old-style: hóa)
    TypeString(*classic_, L"hoa1");
    EXPECT_EQ(classic_->Peek(), L"hóa");
}

TEST_F(VniModernOrthoTest, OA_Modern_ToneOnSecond) {
    TypeString(*modern_, L"hoa1");
    EXPECT_EQ(modern_->Peek(), L"hoá");
}

TEST_F(VniModernOrthoTest, AI_Modern_ToneOnFirst) {
    TypeString(*modern_, L"ai1");
    EXPECT_EQ(modern_->Peek(), L"ái");
}

TEST_F(VniModernOrthoTest, OI_Modern_ToneOnFirst) {
    TypeString(*modern_, L"oi2");
    EXPECT_EQ(modern_->Peek(), L"òi");
}

TEST_F(VniModernOrthoTest, AU_Modern_ToneOnFirst) {
    TypeString(*modern_, L"au3");
    EXPECT_EQ(modern_->Peek(), L"ảu");
}

TEST_F(VniModernOrthoTest, UY_Modern_ToneOnSecond) {
    TypeString(*modern_, L"uy2");
    EXPECT_EQ(modern_->Peek(), L"uỳ");
}

TEST_F(VniModernOrthoTest, ModifiedVowel_TakesPriority) {
    // â (a+6) + tone → tone on â
    TypeString(*modern_, L"a61");
    EXPECT_EQ(modern_->Peek(), L"ấ");
}

TEST_F(VniModernOrthoTest, HornVowel_TakesPriority) {
    // ơ (o+7) + tone → tone on ơ
    TypeString(*modern_, L"o72");
    EXPECT_EQ(modern_->Peek(), L"ờ");
}

TEST_F(VniModernOrthoTest, SingleVowel_Modern) {
    TypeString(*modern_, L"a1");
    EXPECT_EQ(modern_->Peek(), L"á");
}

TEST_F(VniModernOrthoTest, Triphthong_OAI_Modern_ToneOnMiddle) {
    TypeString(*modern_, L"oai1");
    EXPECT_EQ(modern_->Peek(), L"oái");
}

TEST_F(VniModernOrthoTest, Word_Mua_Modern) {
    TypeString(*modern_, L"mua2");
    EXPECT_EQ(modern_->Peek(), L"muà");
}

TEST_F(VniModernOrthoTest, Word_Cua_Modern) {
    TypeString(*modern_, L"cua2");
    EXPECT_EQ(modern_->Peek(), L"cuà");
}

// ============================================================================
// ZWJF Behavior — Telex (allowZwjf ON = default)
// ============================================================================
// With allowZwjf ON (default), z/w/j/f have their normal Telex behavior:
// f/j are tone keys after vowels, w is modifier key, z is regular.
// At word start (before vowels), they naturally act as regular consonants.

class TelexZwjfTest : public ::testing::Test {
protected:
    void SetUp() override {
        config_.inputMethod = InputMethod::Telex;
        // allowZwjf defaults to true (normal Vietnamese)
        engine_ = std::make_unique<Telex::TelexEngine>(config_);
    }

    void ResetEngine() {
        engine_->Reset();
    }

    TypingConfig config_;
    std::unique_ptr<Telex::TelexEngine> engine_;
};

// --- F as initial consonant, still tones after vowel ---

TEST_F(TelexZwjfTest, F_AsInitialConsonant) {
    // 'f' first → just the letter 'f'
    TypeString(*engine_, L"f");
    EXPECT_EQ(engine_->Peek(), L"f");
}

TEST_F(TelexZwjfTest, F_InitialThenVowel_NoTone) {
    // "fa" → f is consonant, a is vowel, no tone applied
    TypeString(*engine_, L"fa");
    EXPECT_EQ(engine_->Peek(), L"fa");
}

TEST_F(TelexZwjfTest, F_InitialWord_Fas) {
    // "fas" → f(consonant) + a(vowel) + s(tone acute on a) → "fá"
    TypeString(*engine_, L"fas");
    EXPECT_EQ(engine_->Peek(), L"fá");
}

TEST_F(TelexZwjfTest, F_InitialWord_Faj) {
    // "faj" → f(consonant) + a(vowel) + j(tone dot on a) → "fạ"
    TypeString(*engine_, L"faj");
    EXPECT_EQ(engine_->Peek(), L"fạ");
}

TEST_F(TelexZwjfTest, F_StillActsAsToneAfterVowel) {
    // "af" → a(vowel) + f(grave tone on a) → "à"
    TypeString(*engine_, L"af");
    EXPECT_EQ(engine_->Peek(), L"à");
}

TEST_F(TelexZwjfTest, F_StillActsAsTone_InWord) {
    // "hoaf" → hoa + f(grave) → "hòa" (classic: tone on first vowel of oa)
    TypeString(*engine_, L"hoaf");
    EXPECT_EQ(engine_->Peek(), L"hòa");
}

// --- J as initial consonant, still tones after vowel ---

TEST_F(TelexZwjfTest, J_AsInitialConsonant) {
    TypeString(*engine_, L"j");
    EXPECT_EQ(engine_->Peek(), L"j");
}

TEST_F(TelexZwjfTest, J_InitialWord_Jas) {
    // "jas" → j(consonant) + a(vowel) + s(acute) → "já"
    TypeString(*engine_, L"jas");
    EXPECT_EQ(engine_->Peek(), L"já");
}

TEST_F(TelexZwjfTest, J_InitialWord_Jaf) {
    // "jaf" → j(consonant) + a(vowel) + f(grave) → "jà"
    TypeString(*engine_, L"jaf");
    EXPECT_EQ(engine_->Peek(), L"jà");
}

TEST_F(TelexZwjfTest, J_StillActsAsToneAfterVowel) {
    // "aj" → a(vowel) + j(dot tone) → "ạ"
    TypeString(*engine_, L"aj");
    EXPECT_EQ(engine_->Peek(), L"ạ");
}

TEST_F(TelexZwjfTest, J_StillActsAsTone_InWord) {
    // "vieetj" → "việt"
    TypeString(*engine_, L"vieetj");
    EXPECT_EQ(engine_->Peek(), L"việt");
}

// --- W as initial consonant, still acts as modifier after vowel ---

TEST_F(TelexZwjfTest, W_AsInitialConsonant) {
    TypeString(*engine_, L"w");
    EXPECT_EQ(engine_->Peek(), L"w");
}

TEST_F(TelexZwjfTest, W_InitialThenVowel) {
    // "wa" → w is regular char, a is vowel
    TypeString(*engine_, L"wa");
    EXPECT_EQ(engine_->Peek(), L"wa");
}

TEST_F(TelexZwjfTest, W_InitialWord_Was) {
    // "was" → w(consonant) + a(vowel) + s(acute) → "wá"
    TypeString(*engine_, L"was");
    EXPECT_EQ(engine_->Peek(), L"wá");
}

TEST_F(TelexZwjfTest, W_StillActsAsModifier_Horn) {
    // "uw" → u + w(horn) → "ư"
    TypeString(*engine_, L"uw");
    EXPECT_EQ(engine_->Peek(), L"ư");
}

TEST_F(TelexZwjfTest, W_StillActsAsModifier_InWord) {
    // "muaw" → m + u + a + w → "mưa" (horn on u, via UA pattern)
    TypeString(*engine_, L"muaw");
    EXPECT_EQ(engine_->Peek(), L"mưa");
}

TEST_F(TelexZwjfTest, W_InitialWord_WithTone) {
    // "waf" → w(consonant) + a(vowel) + f(grave) → "wà"
    TypeString(*engine_, L"waf");
    EXPECT_EQ(engine_->Peek(), L"wà");
}

// --- Z as initial consonant ---

TEST_F(TelexZwjfTest, Z_AsInitialConsonant) {
    TypeString(*engine_, L"z");
    EXPECT_EQ(engine_->Peek(), L"z");
}

TEST_F(TelexZwjfTest, Z_InitialWord_Zas) {
    // "zas" → z(consonant) + a(vowel) + s(acute) → "zá"
    TypeString(*engine_, L"zas");
    EXPECT_EQ(engine_->Peek(), L"zá");
}

TEST_F(TelexZwjfTest, Z_InitialWord_WithCircumflex) {
    // "zaas" → z + â + s(acute) → "zấ"
    TypeString(*engine_, L"zaas");
    EXPECT_EQ(engine_->Peek(), L"zấ");
}

// --- Full words with ZWJF as initial consonants + Vietnamese tones ---

TEST_F(TelexZwjfTest, Word_Fan_NoTone) {
    TypeString(*engine_, L"fan");
    EXPECT_EQ(engine_->Peek(), L"fan");
}

TEST_F(TelexZwjfTest, Word_Wifi_FActsAsTone) {
    // "wifi": w(char) + i(vowel) + f(grave tone on i) + i(char) → "wìi"
    // This is expected Telex behavior: 'f' is ALWAYS a tone key after a vowel.
    // English words containing f/j after vowels cannot be typed literally in Telex.
    TypeString(*engine_, L"wifi");
    EXPECT_EQ(engine_->Peek(), L"wìi");
}

TEST_F(TelexZwjfTest, Word_Jazz_NoTone) {
    TypeString(*engine_, L"jazz");
    EXPECT_EQ(engine_->Peek(), L"jazz");
}

TEST_F(TelexZwjfTest, Word_Zone_NoTone) {
    TypeString(*engine_, L"zone");
    EXPECT_EQ(engine_->Peek(), L"zone");
}

// ============================================================================
// NOTE: allowZwjf is for spell-check validation only (accept "ja" as valid
// Vietnamese). It does NOT change z/w/j/f tone/modifier behavior in the engine.
// TelexZwjfTest above already covers the standard Telex behavior.
// ============================================================================

// ============================================================================
// Engine Created via Factory Respects modernOrtho
// ============================================================================

TEST(FeatureOptionsFactory, TelexEngine_ModernOrtho_ViaConfig) {
    TypingConfig config;
    config.inputMethod = InputMethod::Telex;
    config.modernOrtho = true;

    Telex::TelexEngine engine(config);
    TypeString(engine, L"uas");
    // Modern: tone on 'a'
    EXPECT_EQ(engine.Peek(), L"uá");
}

TEST(FeatureOptionsFactory, TelexEngine_ClassicOrtho_ViaConfig) {
    TypingConfig config;
    config.inputMethod = InputMethod::Telex;
    config.modernOrtho = false;

    Telex::TelexEngine engine(config);
    TypeString(engine, L"uas");
    // Classic: tone on 'u'
    EXPECT_EQ(engine.Peek(), L"úa");
}

TEST(FeatureOptionsFactory, VniEngine_ModernOrtho_ViaConfig) {
    TypingConfig config;
    config.inputMethod = InputMethod::VNI;
    config.modernOrtho = true;

    Vni::VniEngine engine(config);
    TypeString(engine, L"ai1");
    // Modern diphthong-aware: tone on 'a'
    EXPECT_EQ(engine.Peek(), L"ái");
}

}  // namespace
}  // namespace NextKey
