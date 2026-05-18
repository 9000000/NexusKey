// VKey — HotkeyRegistry unit tests (Linux-portable, no Win32 deps)
// SPDX-License-Identifier: GPL-3.0-only

#include <gtest/gtest.h>

#include <sstream>
#include <string>

#include <toml.hpp>

#include "core/hotkey/HotkeyRegistry.h"

namespace NextKey {
namespace {

// VK literals reproduced here so tests stay Linux-portable.
constexpr uint32_t kVkShift   = 0x10;
constexpr uint32_t kVkControl = 0x11;
constexpr uint32_t kVkMenu    = 0x12;  // Alt
constexpr uint32_t kVkEscape  = 0x1B;
constexpr uint32_t kVkA       = 0x41;
constexpr uint32_t kVkF1      = 0x70;

// ───────────────────────────── Defaults ──────────────────────────────────

TEST(HotkeyRegistry, Defaults_CancelComposition_MatchesEscDown) {
    const auto cfg = HotkeyRegistry::Defaults();
    EXPECT_TRUE(cfg.Matches(Intent::CancelComposition,
                            kVkEscape, /*mods=*/0,
                            /*isDoubleTap=*/false, /*keyUp=*/false));
}

TEST(HotkeyRegistry, Defaults_SkipMacro_MatchesEscDown) {
    const auto cfg = HotkeyRegistry::Defaults();
    EXPECT_TRUE(cfg.Matches(Intent::SkipMacro,
                            kVkEscape, 0, false, false));
}

TEST(HotkeyRegistry, Defaults_ToggleEnabled_MatchesCtrlAloneOnKeyUp) {
    const auto cfg = HotkeyRegistry::Defaults();
    EXPECT_TRUE(cfg.Matches(Intent::ToggleEnabled,
                            kVkControl, 0,
                            /*isDoubleTap=*/false, /*keyUp=*/true));
}

TEST(HotkeyRegistry, Defaults_ToggleEnabled_MatchesDoubleTapAlt) {
    const auto cfg = HotkeyRegistry::Defaults();
    EXPECT_TRUE(cfg.Matches(Intent::ToggleEnabled,
                            kVkMenu, 0,
                            /*isDoubleTap=*/true, /*keyUp=*/false));
}

TEST(HotkeyRegistry, Defaults_TriggersFor_Returns4TotalBindings) {
    const auto cfg = HotkeyRegistry::Defaults();
    EXPECT_EQ(cfg.TriggersFor(Intent::CancelComposition).size(), 1u);
    EXPECT_EQ(cfg.TriggersFor(Intent::SkipMacro).size(),         1u);
    EXPECT_EQ(cfg.TriggersFor(Intent::ToggleEnabled).size(),     2u);
}

// ───────────────────────── Single-key Tap matching ───────────────────────

TEST(HotkeyRegistry, Tap_DoesNotFireOnKeyUp) {
    const auto cfg = HotkeyRegistry::Defaults();
    EXPECT_FALSE(cfg.Matches(Intent::CancelComposition,
                             kVkEscape, 0, false, /*keyUp=*/true));
}

TEST(HotkeyRegistry, Tap_WrongVk_DoesNotMatch) {
    const auto cfg = HotkeyRegistry::Defaults();
    EXPECT_FALSE(cfg.Matches(Intent::CancelComposition,
                             kVkF1, 0, false, false));
}

TEST(HotkeyRegistry, Tap_AnyModifierSet_DoesNotMatch) {
    // User rebinds CancelComposition to plain Esc (mods=0). Pressing
    // Shift+Esc must NOT fire — the chord doesn't match.
    const auto cfg = HotkeyRegistry::Defaults();
    EXPECT_FALSE(cfg.Matches(Intent::CancelComposition,
                             kVkEscape, MOD_SHIFT, false, false));
}

// ─────────────────────────────── Chord ───────────────────────────────────

TEST(HotkeyRegistry, Chord_MatchesExactModifiers) {
    HotkeyRegistry cfg;
    cfg.AddTrigger(Intent::SkipMacro, Trigger{kVkA, MOD_CTRL | MOD_SHIFT, false});
    EXPECT_TRUE(cfg.Matches(Intent::SkipMacro,
                            kVkA, MOD_CTRL | MOD_SHIFT, false, false));
}

TEST(HotkeyRegistry, Chord_MissingMod_DoesNotMatch) {
    HotkeyRegistry cfg;
    cfg.AddTrigger(Intent::SkipMacro, Trigger{kVkA, MOD_CTRL | MOD_SHIFT, false});
    EXPECT_FALSE(cfg.Matches(Intent::SkipMacro,
                             kVkA, MOD_CTRL, false, false));
}

TEST(HotkeyRegistry, Chord_ExtraMod_DoesNotMatch) {
    HotkeyRegistry cfg;
    cfg.AddTrigger(Intent::SkipMacro, Trigger{kVkA, MOD_CTRL, false});
    EXPECT_FALSE(cfg.Matches(Intent::SkipMacro,
                             kVkA, MOD_CTRL | MOD_SHIFT, false, false));
}

// ────────────────────────────── DoubleTap ────────────────────────────────

TEST(HotkeyRegistry, DoubleTap_DoesNotFireOnSingleTap) {
    const auto cfg = HotkeyRegistry::Defaults();
    EXPECT_FALSE(cfg.Matches(Intent::ToggleEnabled,
                             kVkMenu, 0,
                             /*isDoubleTap=*/false, /*keyUp=*/false));
}

TEST(HotkeyRegistry, DoubleTap_FiresOnKeyUp_WhenIsDoubleTapSet) {
    // NexusKey's 2×Alt UX fires on the 2nd Alt RELEASE. Caller signals via
    // isDoubleTap=true after detecting timing — Matches() trusts the signal
    // regardless of keyUp.
    const auto cfg = HotkeyRegistry::Defaults();
    EXPECT_TRUE(cfg.Matches(Intent::ToggleEnabled,
                            kVkMenu, 0,
                            /*isDoubleTap=*/true, /*keyUp=*/true));
}

TEST(HotkeyRegistry, DoubleTap_AlsoFiresOnKeyDown_WhenCallerSetsIsDoubleTap) {
    // Symmetric: if caller chose to fire on the 2nd DOWN (alternative UX),
    // Matches() honours that too. Caller owns the framing decision.
    const auto cfg = HotkeyRegistry::Defaults();
    EXPECT_TRUE(cfg.Matches(Intent::ToggleEnabled,
                            kVkMenu, 0,
                            /*isDoubleTap=*/true, /*keyUp=*/false));
}

// ────────────────────────── Modifier-alone ───────────────────────────────

TEST(HotkeyRegistry, ModifierAlone_DoesNotFireOnDown) {
    const auto cfg = HotkeyRegistry::Defaults();
    EXPECT_FALSE(cfg.Matches(Intent::ToggleEnabled,
                             kVkControl, 0,
                             /*isDoubleTap=*/false, /*keyUp=*/false));
}

TEST(HotkeyRegistry, ModifierAlone_DoesNotFireOnIsDoubleTap) {
    // Caller wouldn't normally set both, but defensive check: if event is
    // flagged as DoubleTap, modifier-alone path must not also fire.
    const auto cfg = HotkeyRegistry::Defaults();
    EXPECT_FALSE(cfg.Matches(Intent::ToggleEnabled,
                             kVkControl, 0,
                             /*isDoubleTap=*/true, /*keyUp=*/true));
}

TEST(HotkeyRegistry, IsModifierKey_ClassifiesCorrectly) {
    EXPECT_TRUE(IsModifierKey(kVkShift));
    EXPECT_TRUE(IsModifierKey(kVkControl));
    EXPECT_TRUE(IsModifierKey(kVkMenu));
    EXPECT_FALSE(IsModifierKey(kVkEscape));
    EXPECT_FALSE(IsModifierKey(kVkA));
}

// ─────────────────────── Empty / unknown intent ──────────────────────────

TEST(HotkeyRegistry, EmptyConfig_NeverMatches) {
    const HotkeyRegistry cfg;
    EXPECT_FALSE(cfg.Matches(Intent::CancelComposition,
                             kVkEscape, 0, false, false));
    EXPECT_FALSE(cfg.Matches(Intent::ToggleEnabled,
                             kVkControl, 0, false, true));
}

TEST(HotkeyRegistry, Clear_RemovesAllTriggers) {
    auto cfg = HotkeyRegistry::Defaults();
    cfg.Clear();
    EXPECT_FALSE(cfg.Matches(Intent::CancelComposition,
                             kVkEscape, 0, false, false));
    EXPECT_TRUE(cfg.TriggersFor(Intent::CancelComposition).empty());
}

// ───────────────────────── TOML Load / Save ──────────────────────────────

TEST(HotkeyRegistry, Load_EmptyArray_LeavesConfigEmpty) {
    toml::array arr;
    HotkeyRegistry cfg;
    cfg.Load(arr);
    EXPECT_TRUE(cfg.TriggersFor(Intent::CancelComposition).empty());
}

TEST(HotkeyRegistry, Load_KnownIntent_PopulatesTrigger) {
    toml::array arr;
    toml::table row;
    row.insert("intent", "cancel-composition");
    toml::table trig;
    trig.insert("vk",   static_cast<int64_t>(kVkF1));
    trig.insert("mods", static_cast<int64_t>(0));
    row.insert("trigger", std::move(trig));
    arr.push_back(std::move(row));

    HotkeyRegistry cfg;
    cfg.Load(arr);
    EXPECT_TRUE(cfg.Matches(Intent::CancelComposition, kVkF1, 0, false, false));
}

TEST(HotkeyRegistry, Load_UnknownIntent_Skipped) {
    toml::array arr;
    toml::table row;
    row.insert("intent", "reload-config");  // not in Intent enum
    toml::table trig;
    trig.insert("vk", static_cast<int64_t>(kVkF1));
    trig.insert("mods", static_cast<int64_t>(0));
    row.insert("trigger", std::move(trig));
    arr.push_back(std::move(row));

    HotkeyRegistry cfg;
    cfg.Load(arr);
    // F1 must not fire any known intent.
    EXPECT_FALSE(cfg.Matches(Intent::CancelComposition, kVkF1, 0, false, false));
    EXPECT_FALSE(cfg.Matches(Intent::SkipMacro,         kVkF1, 0, false, false));
    EXPECT_FALSE(cfg.Matches(Intent::ToggleEnabled,     kVkF1, 0, false, false));
}

TEST(HotkeyRegistry, Load_VkZero_Skipped) {
    toml::array arr;
    toml::table row;
    row.insert("intent", "cancel-composition");
    toml::table trig;
    trig.insert("vk", static_cast<int64_t>(0));
    row.insert("trigger", std::move(trig));
    arr.push_back(std::move(row));

    HotkeyRegistry cfg;
    cfg.Load(arr);
    EXPECT_TRUE(cfg.TriggersFor(Intent::CancelComposition).empty());
}

TEST(HotkeyRegistry, Load_MissingTrigger_Skipped) {
    toml::array arr;
    toml::table row;
    row.insert("intent", "cancel-composition");
    // no "trigger" key
    arr.push_back(std::move(row));

    HotkeyRegistry cfg;
    cfg.Load(arr);
    EXPECT_TRUE(cfg.TriggersFor(Intent::CancelComposition).empty());
}

TEST(HotkeyRegistry, Save_Defaults_ProducesParseableRoundTrip) {
    const auto src = HotkeyRegistry::Defaults();
    toml::array arr;
    src.Save(arr);

    HotkeyRegistry dst;
    dst.Load(arr);

    // Each default trigger must match again in the round-tripped config.
    EXPECT_TRUE(dst.Matches(Intent::CancelComposition, kVkEscape, 0, false, false));
    EXPECT_TRUE(dst.Matches(Intent::SkipMacro,         kVkEscape, 0, false, false));
    EXPECT_TRUE(dst.Matches(Intent::ToggleEnabled,     kVkControl, 0, false, /*keyUp=*/true));
    EXPECT_TRUE(dst.Matches(Intent::ToggleEnabled,     kVkMenu,    0, /*isDoubleTap=*/true, false));
    EXPECT_EQ(dst.TriggersFor(Intent::ToggleEnabled).size(), 2u);
}

TEST(HotkeyRegistry, Save_DoubleTapFlag_PreservedAcrossRoundTrip) {
    HotkeyRegistry src;
    src.AddTrigger(Intent::ToggleEnabled, Trigger{kVkMenu, 0, /*doubleTap=*/true});

    toml::array arr;
    src.Save(arr);

    HotkeyRegistry dst;
    dst.Load(arr);
    ASSERT_EQ(dst.TriggersFor(Intent::ToggleEnabled).size(), 1u);
    EXPECT_TRUE(dst.TriggersFor(Intent::ToggleEnabled)[0].doubleTap);
}

// ───────────────────── Legacy field migration ────────────────────────────

TEST(HotkeyRegistry, FromLegacyFields_AllOff_ProducesEmptyRegistry) {
    const auto cfg = HotkeyRegistry::FromLegacyFields(
        /*escRestoreRawEnabled=*/false,
        /*tempOffMacroByEsc=*/false,
        /*tempOffMethodValue=*/0);
    EXPECT_TRUE(cfg.TriggersFor(Intent::CancelComposition).empty());
    EXPECT_TRUE(cfg.TriggersFor(Intent::SkipMacro).empty());
    EXPECT_TRUE(cfg.TriggersFor(Intent::ToggleEnabled).empty());
}

TEST(HotkeyRegistry, FromLegacyFields_EscRestoreRaw_BindsEscToCancelComposition) {
    const auto cfg = HotkeyRegistry::FromLegacyFields(true, false, 0);
    EXPECT_TRUE(cfg.Matches(Intent::CancelComposition, kVkEscape, 0, false, false));
    EXPECT_TRUE(cfg.TriggersFor(Intent::SkipMacro).empty());
}

TEST(HotkeyRegistry, FromLegacyFields_TempOffMacro_BindsEscToSkipMacro) {
    const auto cfg = HotkeyRegistry::FromLegacyFields(false, true, 0);
    EXPECT_TRUE(cfg.Matches(Intent::SkipMacro, kVkEscape, 0, false, false));
    EXPECT_TRUE(cfg.TriggersFor(Intent::CancelComposition).empty());
}

TEST(HotkeyRegistry, FromLegacyFields_TempOffMethodCtrl_BindsCtrlAlone) {
    const auto cfg = HotkeyRegistry::FromLegacyFields(false, false, /*Ctrl=*/2);
    // Ctrl-alone fires on UP (modifier-alone implicit semantic)
    EXPECT_TRUE(cfg.Matches(Intent::ToggleEnabled, kVkControl, 0,
                            /*isDoubleTap=*/false, /*keyUp=*/true));
    EXPECT_EQ(cfg.TriggersFor(Intent::ToggleEnabled).size(), 1u);
    EXPECT_FALSE(cfg.TriggersFor(Intent::ToggleEnabled)[0].doubleTap);
}

TEST(HotkeyRegistry, FromLegacyFields_TempOffMethodDupAlt_BindsDoubleTapAlt) {
    const auto cfg = HotkeyRegistry::FromLegacyFields(false, false, /*DupAlt=*/1);
    EXPECT_TRUE(cfg.Matches(Intent::ToggleEnabled, kVkMenu, 0,
                            /*isDoubleTap=*/true, /*keyUp=*/true));
    ASSERT_EQ(cfg.TriggersFor(Intent::ToggleEnabled).size(), 1u);
    EXPECT_TRUE(cfg.TriggersFor(Intent::ToggleEnabled)[0].doubleTap);
}

TEST(HotkeyRegistry, FromLegacyFields_TempOffMethodNone_NoToggleTrigger) {
    const auto cfg = HotkeyRegistry::FromLegacyFields(false, false, /*None=*/0);
    EXPECT_TRUE(cfg.TriggersFor(Intent::ToggleEnabled).empty());
}

TEST(HotkeyRegistry, FromLegacyFields_AllOn_DupAltVariant_ProducesThreeIntents) {
    const auto cfg = HotkeyRegistry::FromLegacyFields(
        /*esc=*/true, /*macro=*/true, /*DupAlt=*/1);
    EXPECT_TRUE(cfg.Matches(Intent::CancelComposition, kVkEscape, 0, false, false));
    EXPECT_TRUE(cfg.Matches(Intent::SkipMacro,         kVkEscape, 0, false, false));
    EXPECT_TRUE(cfg.Matches(Intent::ToggleEnabled,     kVkMenu, 0, true, true));
}

TEST(HotkeyRegistry, FromLegacyFields_UnknownTempOffValue_FallsThroughToNone) {
    // Forward-compat: future enum values default to no toggle binding (safe).
    const auto cfg = HotkeyRegistry::FromLegacyFields(false, false, /*future=*/99);
    EXPECT_TRUE(cfg.TriggersFor(Intent::ToggleEnabled).empty());
}

}  // namespace
}  // namespace NextKey
