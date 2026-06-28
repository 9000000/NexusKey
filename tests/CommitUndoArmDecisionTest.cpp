// VKey - CommitUndoArmDecision unit tests
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-VKey-Commercial
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
                        VK_PRIOR_, VK_NEXT_, VK_TAB_, VK_ESCAPE_, VK_INSERT_,
                        VK_DELETE_}) {
        EXPECT_EQ(DecideCommitUndoArm(vk), CommitUndoArm::Skip) << "vk=" << vk;
    }
}

// --- Enter is NOT misclassified as navigation ------------------------------

TEST(CommitUndoArmDecisionTest, Enter_IsNot_Skip) {
    // Enter sits numerically below the navigation block; make sure it routes to
    // Clear, not Skip (Skip keeps the stack → the bug would persist).
    EXPECT_NE(DecideCommitUndoArm(VK_RETURN_), CommitUndoArm::Skip);
    EXPECT_NE(DecideCommitUndoArm(VK_RETURN_), CommitUndoArm::Arm);
}

}  // namespace
