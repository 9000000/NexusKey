// VKey - portable legacy RegisterHotKey planning tests
// SPDX-License-Identifier: GPL-3.0-only

#include <gtest/gtest.h>

#include "core/hotkey/RegisterHotKeyPlan.h"

namespace NextKey {
namespace {

constexpr std::uint32_t kVkControl = 0x11;
constexpr std::uint32_t kVkShift = 0x10;
constexpr std::uint32_t kVkMenu = 0x12;
constexpr std::uint32_t kVkLwin = 0x5B;
constexpr std::uint32_t kVkK = 0x4B;

void ExpectChord(const HotkeyConfig& chord,
                 bool ctrl,
                 bool shift,
                 bool alt,
                 bool win,
                 std::uint32_t vk) {
    EXPECT_EQ(chord.ctrl, ctrl);
    EXPECT_EQ(chord.shift, shift);
    EXPECT_EQ(chord.alt, alt);
    EXPECT_EQ(chord.win, win);
    EXPECT_EQ(chord.vk, vk);
}

TEST(RegisterHotKeyPlanTest, ExplicitVirtualKeyProducesOneUnchangedChord) {
    const auto plan = BuildRegisterHotKeyPlan(
        {.ctrl = true, .alt = true, .vk = kVkK});

    ASSERT_EQ(plan.size(), 1u);
    ExpectChord(plan[0], true, false, true, false, kVkK);
}

TEST(RegisterHotKeyPlanTest, CtrlShiftModifierOnlySupportsEitherPressOrder) {
    const auto plan = BuildRegisterHotKeyPlan(
        {.ctrl = true, .shift = true});

    ASSERT_EQ(plan.size(), 2u);
    ExpectChord(plan[0], false, true, false, false, kVkControl);
    ExpectChord(plan[1], true, false, false, false, kVkShift);
}

TEST(RegisterHotKeyPlanTest, ModifierOnlyAlternativesAreDeterministicAndRemoveMainFlag) {
    const auto plan = BuildRegisterHotKeyPlan(
        {.ctrl = true, .shift = true, .alt = true, .win = true});

    ASSERT_EQ(plan.size(), 4u);
    ExpectChord(plan[0], false, true, true, true, kVkControl);
    ExpectChord(plan[1], true, false, true, true, kVkShift);
    ExpectChord(plan[2], true, true, false, true, kVkMenu);
    ExpectChord(plan[3], true, true, true, false, kVkLwin);
}

TEST(RegisterHotKeyPlanTest, UnassignedConfigProducesNoRegistrations) {
    EXPECT_TRUE(BuildRegisterHotKeyPlan({}).empty());
}

}  // namespace
}  // namespace NextKey
