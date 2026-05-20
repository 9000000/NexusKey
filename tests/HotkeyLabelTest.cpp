// VKey — HotkeyLabel formatter unit tests (Linux-portable, no Win32 deps).
// SPDX-License-Identifier: GPL-3.0-only

#include <gtest/gtest.h>

#include <string>

#include "core/hotkey/HotkeyLabel.h"
#include "core/hotkey/HotkeyRegistry.h"  // kModCtrl / kModShift / kModAlt / kModWin


namespace NextKey {
namespace {

// VK literals (Linux-portable copies of Win32 constants).
constexpr uint32_t kVkZ     = 0x5A;
constexpr uint32_t kVkF1    = 0x70;
constexpr uint32_t kVkF5    = 0x74;
constexpr uint32_t kVkF12   = 0x7B;
constexpr uint32_t kVk0     = 0x30;
constexpr uint32_t kVk9     = 0x39;
constexpr uint32_t kVkEsc   = 0x1B;
constexpr uint32_t kVkSpace = 0x20;

// ───────────────────────────── Empty / unset ─────────────────────────────

TEST(HotkeyLabel, EmptyWhenNothingAssigned) {
    EXPECT_EQ(FormatHotkeyLabel(0, 0), L"");
}

TEST(HotkeyLabel, ModifiersOnlyWhenVkZero) {
    EXPECT_EQ(FormatHotkeyLabel(0, kModCtrl), L"Ctrl");
    EXPECT_EQ(FormatHotkeyLabel(0, kModCtrl | kModShift), L"Ctrl+Shift");
    EXPECT_EQ(FormatHotkeyLabel(0, kModCtrl | kModShift | kModAlt | kModWin),
              L"Ctrl+Shift+Alt+Win");
}

// ───────────────────────────── Letters / digits ──────────────────────────

TEST(HotkeyLabel, AltZ) {
    EXPECT_EQ(FormatHotkeyLabel(kVkZ, kModAlt), L"Alt+Z");
}

TEST(HotkeyLabel, JustZ) {
    EXPECT_EQ(FormatHotkeyLabel(kVkZ, 0), L"Z");
}

TEST(HotkeyLabel, CtrlShift0) {
    EXPECT_EQ(FormatHotkeyLabel(kVk0, kModCtrl | kModShift), L"Ctrl+Shift+0");
}

TEST(HotkeyLabel, CtrlShift9) {
    EXPECT_EQ(FormatHotkeyLabel(kVk9, kModCtrl | kModShift), L"Ctrl+Shift+9");
}

// ───────────────────────────── F-row ─────────────────────────────────────

TEST(HotkeyLabel, CtrlShiftF1) {
    EXPECT_EQ(FormatHotkeyLabel(kVkF1, kModCtrl | kModShift),
              L"Ctrl+Shift+F1");
}

TEST(HotkeyLabel, F5Alone) {
    EXPECT_EQ(FormatHotkeyLabel(kVkF5, 0), L"F5");
}

TEST(HotkeyLabel, F12) {
    EXPECT_EQ(FormatHotkeyLabel(kVkF12, kModAlt), L"Alt+F12");
}

// ───────────────────────────── Named keys ────────────────────────────────

TEST(HotkeyLabel, EscNamed) {
    EXPECT_EQ(FormatHotkeyLabel(kVkEsc, 0), L"Esc");
}

TEST(HotkeyLabel, SpaceNamed) {
    EXPECT_EQ(FormatHotkeyLabel(kVkSpace, kModCtrl), L"Ctrl+Space");
}

// ───────────────────────────── Modifier order is stable ──────────────────

TEST(HotkeyLabel, ModifierOrderIsCtrlShiftAltWin) {
    // Regardless of bit order in the mask, output is Ctrl→Shift→Alt→Win.
    EXPECT_EQ(FormatHotkeyLabel(kVkZ, kModWin | kModAlt | kModShift | kModCtrl),
              L"Ctrl+Shift+Alt+Win+Z");
}

}  // namespace
}  // namespace NextKey
