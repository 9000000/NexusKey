// VKey - Commit-undo arming decision (post commit-trigger)
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-VKey-Commercial
//
// Pure decision: after a commit trigger fires and a word was pushed onto the
// commit-undo stack, what should happen to the undo window?
//
//   Arm   - open the undo window so a Backspace right after can revive the
//           just-committed word. Printable triggers (Space, VNI digits 0-9,
//           punctuation) keep the word AT the caret, so revive is valid.
//   Skip  - leave commit-undo state untouched (don't arm; keep the stack).
//           Navigation keys (arrows / Home / End / PgUp / PgDn / Tab / Esc /
//           Delete / Insert) move the caret, so arming would replay text at
//           the wrong position — but an in-place Backspace chain may still be
//           valid, so the stack is preserved (pre-existing behavior).
//   Clear - drop the whole undo window + stack. Enter (VK_RETURN) SENDS the
//           message in chat apps (the word is gone) or breaks to a NEW LINE in
//           editors (the word sits on the prior line). Either way the committed
//           word leaves the caret, so a later Backspace must NOT replay it back
//           into the new line/message. Leaving Enter armed is exactly what
//           desynced chat apps (Messenger / Zalo): Backspace after Enter
//           re-injected the previous message's word, the hook then saw "two
//           words stuck together", which violates Vietnamese phonotactics →
//           English-bias latch → tones blocked until the user deleted the whole
//           word. See issue #210.
//
// Extracted from HookEngine.cpp for Linux GTest coverage: HookEngine.cpp is
// Win32-only and is not linked into the cross-platform VKeyTests target. Raw
// VK hex constants keep this header free of <windows.h>.

#pragma once

#include <cstdint>

namespace NextKey {

enum class CommitUndoArm : uint8_t {
    Arm,
    Skip,
    Clear,
};

/// `vkCode` is the Win32 virtual-key of the commit trigger that just fired.
[[nodiscard]] constexpr CommitUndoArm DecideCommitUndoArm(uint32_t vkCode) noexcept {
    constexpr uint32_t kVkReturn = 0x0D;
    constexpr uint32_t kVkTab    = 0x09;
    constexpr uint32_t kVkEscape = 0x1B;
    constexpr uint32_t kVkPrior  = 0x21;  // Page Up
    constexpr uint32_t kVkNext   = 0x22;  // Page Down
    constexpr uint32_t kVkEnd    = 0x23;
    constexpr uint32_t kVkHome   = 0x24;
    constexpr uint32_t kVkLeft   = 0x25;
    constexpr uint32_t kVkDown   = 0x28;  // VK_LEFT..VK_DOWN occupy 0x25..0x28
    constexpr uint32_t kVkInsert = 0x2D;
    constexpr uint32_t kVkDelete = 0x2E;

    // Enter leaves the caret (send / new line) → never replay the prior word.
    if (vkCode == kVkReturn) return CommitUndoArm::Clear;

    const bool isNavigation =
        (vkCode >= kVkLeft && vkCode <= kVkDown) ||
        vkCode == kVkHome || vkCode == kVkEnd ||
        vkCode == kVkPrior || vkCode == kVkNext ||
        vkCode == kVkTab || vkCode == kVkEscape ||
        vkCode == kVkDelete || vkCode == kVkInsert;
    if (isNavigation) return CommitUndoArm::Skip;

    // Space, VNI digits, punctuation: the word stays at the caret → arm revive.
    return CommitUndoArm::Arm;
}

}  // namespace NextKey
