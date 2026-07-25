// VKey - CommitUndoArmDecision unit tests
// SPDX-License-Identifier: AGPL-3.0-only
//
// Locks in the issue #210 fix: Enter must CLEAR commit-undo (never arm), so a
// Backspace after Enter cannot replay a word that the host already sent (chat)
// or pushed to a previous line (editor). HookEngine.cpp is Win32-only; this
// covers the pure decision it now delegates to.

#include "core/CommitUndoArmDecision.h"
#include <gtest/gtest.h>

using NextKey::CommitUndoArm;
using NextKey::DecideCommitUndoArm;

namespace {

// Win32 VK codes (kept local so the test documents the exact inputs).
constexpr uint32_t VK_RETURN_ = 0x0D;
constexpr uint32_t VK_SPACE_  = 0x20;
constexpr uint32_t VK_TAB_    = 0x09;
constexpr uint32_t VK_ESCAPE_ = 0x1B;
constexpr uint32_t VK_LEFT_   = 0x25;
constexpr uint32_t VK_UP_     = 0x26;
constexpr uint32_t VK_RIGHT_  = 0x27;
constexpr uint32_t VK_DOWN_   = 0x28;
constexpr uint32_t VK_HOME_   = 0x24;
constexpr uint32_t VK_END_    = 0x23;
constexpr uint32_t VK_PRIOR_  = 0x21;
constexpr uint32_t VK_NEXT_   = 0x22;
constexpr uint32_t VK_INSERT_ = 0x2D;
constexpr uint32_t VK_DELETE_ = 0x2E;

// --- The fix: Enter clears, never arms -------------------------------------

TEST(CommitUndoArmDecisionTest, Enter_Clears_issue210) {
    // Regression: leaving Enter armed let a post-Enter Backspace replay the
    // previous message's word in chat apps → "2 words stuck" → tones blocked.
    EXPECT_EQ(DecideCommitUndoArm(VK_RETURN_), CommitUndoArm::Clear);
}

// --- Printable triggers arm (word stays at caret) --------------------------

TEST(CommitUndoArmDecisionTest, Space_Arms) {
    EXPECT_EQ(DecideCommitUndoArm(VK_SPACE_), CommitUndoArm::Arm);
}

TEST(CommitUndoArmDecisionTest, VniDigitsAndPunctuation_Arm) {
    for (uint32_t vk = '0'; vk <= '9'; ++vk)
        EXPECT_EQ(DecideCommitUndoArm(vk), CommitUndoArm::Arm) << "vk=" << vk;
    EXPECT_EQ(DecideCommitUndoArm(0xBC), CommitUndoArm::Arm);  // VK_OEM_COMMA ','
    EXPECT_EQ(DecideCommitUndoArm(0xBE), CommitUndoArm::Arm);  // VK_OEM_PERIOD '.'
}

// --- Navigation keys skip (caret moved, but keep stack) --------------------

TEST(CommitUndoArmDecisionTest, NavigationKeys_Skip) {
    for (uint32_t vk : {VK_LEFT_, VK_UP_, VK_RIGHT_, VK_DOWN_, VK_HOME_, VK_END_,
                        VK_PRIOR_, VK_NEXT_, VK_ESCAPE_, VK_INSERT_,
                        VK_DELETE_}) {
        EXPECT_EQ(DecideCommitUndoArm(vk), CommitUndoArm::Skip) << "vk=" << vk;
    }
}

// --- Replay-context boundary policy ----------------------------------------
// Single source of truth for "this key LEAVES the editing context, so a replay
// window must never survive it". Enforced at one choke point in ProcessKeyDown
// (CommitState::DiscardReplayContext) because the post-commit switch below is
// unreachable when the engine is empty, when the commit is quick-consonant, or
// when no stack entry is pushed — and HandleBackspace re-arms replay from any
// stack that survives.

TEST(CommitUndoArmDecisionTest, ReplayContextBoundary_IsEnterAndTabOnly) {
    EXPECT_TRUE(NextKey::IsReplayContextBoundary(VK_RETURN_));
    EXPECT_TRUE(NextKey::IsReplayContextBoundary(VK_TAB_));

    // Same-field navigation is NOT a boundary — multi-word backward replay
    // legitimately depends on the stack surviving these.
    for (uint32_t vk : {VK_LEFT_, VK_UP_, VK_RIGHT_, VK_DOWN_, VK_HOME_, VK_END_,
                        VK_PRIOR_, VK_NEXT_, VK_ESCAPE_, VK_INSERT_, VK_DELETE_,
                        VK_SPACE_}) {
        EXPECT_FALSE(NextKey::IsReplayContextBoundary(vk)) << "vk=" << vk;
    }
}

TEST(CommitUndoArmDecisionTest, BoundaryKeysAgreeWithArmDecision) {
    // The two must not drift: every boundary key must also map to Clear, so the
    // post-commit switch stays consistent defense-in-depth for the choke point.
    for (uint32_t vk = 0; vk < 0x100; ++vk) {
        if (NextKey::IsReplayContextBoundary(vk)) {
            EXPECT_EQ(DecideCommitUndoArm(vk), CommitUndoArm::Clear)
                << "boundary key must also Clear, vk=" << vk;
        }
    }
}

// --- Tab clears (unlike same-field navigation, Tab commonly moves focus to
// a DIFFERENT control) -------------------------------------------------------

TEST(CommitUndoArmDecisionTest, Tab_Clears) {
    // Regression: Skip previously kept commitStack_ alive across a Tab-driven
    // focus change, so backspacing-to-empty in the NEW field could re-arm
    // Ready from the OLD field's stack entry and replay its text there.
    EXPECT_EQ(DecideCommitUndoArm(VK_TAB_), CommitUndoArm::Clear);
}

// --- Enter is NOT misclassified as navigation ------------------------------

TEST(CommitUndoArmDecisionTest, Enter_IsNot_Skip) {
    // Enter sits numerically below the navigation block; make sure it routes to
    // Clear, not Skip (Skip keeps the stack → the bug would persist).
    EXPECT_NE(DecideCommitUndoArm(VK_RETURN_), CommitUndoArm::Skip);
    EXPECT_NE(DecideCommitUndoArm(VK_RETURN_), CommitUndoArm::Arm);
}

}  // namespace
