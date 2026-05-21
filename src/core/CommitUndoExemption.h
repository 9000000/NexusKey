// VKey - Commit-undo cancellation exemption rule
// SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-VKey-Commercial
//
// Pure predicate: given a keystroke, returns true if the key should be
// EXEMPT from the cancel-Primed branches in
// `HookEngine::HandleCommitUndo`. Two cancel sites share this rule:
//
//   1. Synth-guard (line ~1061): full `CancelCommitUndo()` when injector
//      synth events are still in flight (`synthEventsPending_ > 0 &&
//      elapsed < settleMs`). Non-exempt keys wipe `commitStack_`.
//
//   2. Catch-all else (line ~1124): plain demotion to Idle state
//      (commitStack_ preserved) for any key not matching alpha / VNI
//      digit / VK_BACK.
//
// Both sites must exempt the same key classes so the post-BS recovery
// paths (tone-modifier replay, ESC restore-raw) can fire.
//
// Extracted from HookEngine.cpp for Linux GTest coverage: HookEngine.cpp
// is Win32-only and not linked into the cross-platform VKeyTests target.
// See `docs/plans/2026-05-17-esc-restore-raw-post-bs-design.md` §9 and
// memory `project_commit_undo_synth_guard_exemption.md`.

#pragma once

#include "core/config/TypingConfig.h"  // InputMethod
#include <cstdint>

namespace NextKey {

/// Returns true when `vkCode` should bypass the cancel-Primed branches.
///
/// Exempt classes:
///   - Telex tone modifiers `s/f/r/x/j` (active in Telex / SimpleTelex /
///     Combined). SimpleTelex is identical to Telex for tone keys — it
///     only differs in bracket / `w` handling (TypingConfig.h:19), and
///     the engine's `IsTelexMode()` (TypingEngine.h:118) returns true
///     for all three methods.
///   - VNI tone modifiers `1-5` without Shift (active in VNI / Combined).
///     Shift is checked because Shift+digit produces punctuation on most
///     layouts (Shift+1 = '!', etc.), which is not a tone keystroke.
///   - UserDefined tone keys: caller passes `isCustomToneKey=true` when
///     `vkCode` resolves (via lowercase ASCII) to a customKeyMap entry
///     whose `TypingAction` is one of `ToneAcute/Grave/Hook/Tilde/Dot`.
///     ClearTone is intentionally excluded — matches existing Telex
///     behaviour where `z` is not exempt. Any non-UserDefined method
///     ignores this flag.
///   - VK_ESCAPE when `escIsCancelTrigger` is true — same semantic class:
///     the key only modifies the just-committed word (replaces composed
///     Vietnamese with the user's raw keys). v3 source of truth: hot-path
///     snapshot of `HotkeyRegistry::Matches(Intent::CancelComposition,
///     VK_ESCAPE, mods=0, doubleTap=false, keyUp=false)`. Disabled intent
///     or removed Esc trigger ⇒ caller passes `false` and Esc behaves
///     like any other non-exempt key.
///
/// All other keys (alpha letters, punctuation, navigation, F-keys, etc.)
/// return false — caller must demote / cancel commit-undo state.
[[nodiscard]] constexpr bool IsCommitUndoExemptKey(
    uint32_t vkCode,
    InputMethod method,
    bool shiftHeld,
    bool escIsCancelTrigger,
    bool isCustomToneKey = false) noexcept {
    constexpr uint32_t kVkEscape = 0x1B;

    const bool isTelexTone =
        (method == InputMethod::Telex ||
         method == InputMethod::SimpleTelex ||
         method == InputMethod::Combined) &&
        (vkCode == 'S' || vkCode == 'F' || vkCode == 'R' ||
         vkCode == 'X' || vkCode == 'J');

    const bool isVniTone =
        (method == InputMethod::VNI || method == InputMethod::Combined) &&
        vkCode >= '1' && vkCode <= '5' &&
        !shiftHeld;

    const bool isUserDefinedTone =
        (method == InputMethod::UserDefined) && isCustomToneKey;

    const bool isEscRestoreRawKey =
        (vkCode == kVkEscape) && escIsCancelTrigger;

    return isTelexTone || isVniTone || isUserDefinedTone || isEscRestoreRawKey;
}

}  // namespace NextKey
