// Tests for DecideEatLeakedKeyDuringSend — pure decision for the "nhảy loạn"
// reorder fix (issue #206). HookEngine.cpp is Windows-only, so we test the
// extracted decision in isolation (same pattern as CjkSwitchDecisionTest).
// SPDX-License-Identifier: AGPL-3.0-only

#include <gtest/gtest.h>

#include "core/LeakedKeyDuringSendDecision.h"

using NextKey::DecideEatLeakedKeyDuringSend;
using NextKey::LeakedKeyDuringSendInputs;

namespace {

constexpr unsigned int kVkS = 0x53;       // 'S' — the tone key in the log
constexpr unsigned int kVkD = 0x44;       // 'D' — the leaked next key in the log
constexpr unsigned int kVkShift = 0x10;   // VK_SHIFT

// Default: a transform for 'S' is injecting on a multi-process renderer, and
// an 'S' event has leaked into the window. Individual tests override.
LeakedKeyDuringSendInputs MakeInputs() {
    LeakedKeyDuringSendInputs in{};
    in.isSending = true;
    in.hasMultiProcessRenderer = true;
    in.eventVk = kVkS;
    in.sendingForVk = kVkS;
    return in;
}

}  // namespace

// ── Gating: not injecting ─────────────────────────────────────────────
TEST(LeakedKeyDuringSendDecision, NotSending_NeverEats) {
    auto in = MakeInputs();
    in.isSending = false;
    EXPECT_FALSE(DecideEatLeakedKeyDuringSend(in));
}

// ── Gating: single-process renderer keeps FIFO ordering ───────────────
TEST(LeakedKeyDuringSendDecision, SingleProcessRenderer_NeverEats) {
    // Vanilla Win32 (Notepad/Word): SendInput FIFO holds, behavior unchanged.
    auto in = MakeInputs();
    in.hasMultiProcessRenderer = false;
    EXPECT_FALSE(DecideEatLeakedKeyDuringSend(in));
}

// ── Core: in-flight key's own leaked events are eaten ─────────────────
TEST(LeakedKeyDuringSendDecision, MultiProcess_SameVk_Eats) {
    // The 's' auto-repeat key-down / key-up that interleaved between our
    // synthetic backspaces in the log (lines 61/62/65) → eat, no reorder.
    auto in = MakeInputs();  // eventVk == sendingForVk == 'S'
    EXPECT_TRUE(DecideEatLeakedKeyDuringSend(in));
}

// ── Safety: a genuinely-typed different next key is NEVER dropped ─────
TEST(LeakedKeyDuringSendDecision, MultiProcess_DifferentVk_PassesThrough) {
    // The physical 'd' of "dashboard" that leaked mid-injection (log line 227).
    // Eating it would silently drop the user's keystroke — must pass through.
    auto in = MakeInputs();
    in.eventVk = kVkD;        // next key, different from in-flight 'S'
    EXPECT_FALSE(DecideEatLeakedKeyDuringSend(in));
}

// ── Safety: modifier release is never stranded ────────────────────────
TEST(LeakedKeyDuringSendDecision, MultiProcess_ModifierVk_PassesThrough) {
    // A Shift up leaking during send: vk differs from the alpha in-flight key,
    // so it passes through — no stuck modifier in the target app.
    auto in = MakeInputs();
    in.eventVk = kVkShift;
    EXPECT_FALSE(DecideEatLeakedKeyDuringSend(in));
}

// ── Conservative: unknown in-flight key → never eat ───────────────────
TEST(LeakedKeyDuringSendDecision, MultiProcess_NoInFlightVk_PassesThrough) {
    auto in = MakeInputs();
    in.sendingForVk = 0;      // nothing identified as in flight
    in.eventVk = kVkS;
    EXPECT_FALSE(DecideEatLeakedKeyDuringSend(in));
}

// ── Combined gate: every condition must hold to eat ───────────────────
TEST(LeakedKeyDuringSendDecision, AllGatesRequired) {
    // Sanity matrix: flipping any single gate off flips the verdict to pass.
    EXPECT_TRUE(DecideEatLeakedKeyDuringSend(MakeInputs()));

    auto noSend = MakeInputs();          noSend.isSending = false;
    auto single = MakeInputs();          single.hasMultiProcessRenderer = false;
    auto diff   = MakeInputs();          diff.eventVk = kVkD;
    auto none   = MakeInputs();          none.sendingForVk = 0;

    EXPECT_FALSE(DecideEatLeakedKeyDuringSend(noSend));
    EXPECT_FALSE(DecideEatLeakedKeyDuringSend(single));
    EXPECT_FALSE(DecideEatLeakedKeyDuringSend(diff));
    EXPECT_FALSE(DecideEatLeakedKeyDuringSend(none));
}
