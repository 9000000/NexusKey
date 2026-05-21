// VKey — IsCommitUndoExemptKey unit tests (Linux-portable)
// SPDX-License-Identifier: GPL-3.0-only
//
// Pin contract for the exemption rule shared by HookEngine's
// commit-undo Primed-branch cancel sites:
//   1. Synth-guard cancel (HookEngine.cpp ~line 1061) — wipes commitStack_.
//   2. Catch-all else (HookEngine.cpp ~line 1124) — demotes state to Idle.
//
// History:
//   - Original rule (pre-2026-05-17): only Telex `s/f/r/x/j` + VNI `1-5`
//     were exempt (Sprint 2 D1, chaos 5.3 fix).
//   - 2026-05-17 (this work): ESC restore-raw added. Without exemption ESC
//     post-BS hit either cancel site (depending on synth-pending state),
//     defeating the post-BS rawInput restore path. Bug observed
//     on Notepad++ (log session 22:56 → 22:59 after fix).

#include <gtest/gtest.h>

#include "core/CommitUndoExemption.h"

namespace NextKey {
namespace {

constexpr uint32_t kVkEscape = 0x1B;
constexpr uint32_t kVkA      = 0x41;
constexpr uint32_t kVkS      = 0x53;
constexpr uint32_t kVkF      = 0x46;
constexpr uint32_t kVkR      = 0x52;
constexpr uint32_t kVkX      = 0x58;
constexpr uint32_t kVkJ      = 0x4A;
constexpr uint32_t kVkB      = 0x42;
constexpr uint32_t kVk1      = 0x31;
constexpr uint32_t kVk5      = 0x35;
constexpr uint32_t kVk6      = 0x36;
constexpr uint32_t kVk0      = 0x30;
constexpr uint32_t kVkSpace  = 0x20;
constexpr uint32_t kVkReturn = 0x0D;

// ============================================================
// Telex tone modifiers — exempt in Telex / Combined modes
// ============================================================

TEST(CommitUndoExemption, Telex_ToneKeys_ExemptInTelex) {
    for (uint32_t vk : {kVkS, kVkF, kVkR, kVkX, kVkJ}) {
        EXPECT_TRUE(IsCommitUndoExemptKey(vk, InputMethod::Telex, false, false))
            << "vk=0x" << std::hex << vk;
    }
}

TEST(CommitUndoExemption, Telex_ToneKeys_ExemptInCombined) {
    for (uint32_t vk : {kVkS, kVkF, kVkR, kVkX, kVkJ}) {
        EXPECT_TRUE(IsCommitUndoExemptKey(vk, InputMethod::Combined, false, false));
    }
}

TEST(CommitUndoExemption, Telex_ToneKeys_NotExemptInVni) {
    for (uint32_t vk : {kVkS, kVkF, kVkR, kVkX, kVkJ}) {
        EXPECT_FALSE(IsCommitUndoExemptKey(vk, InputMethod::VNI, false, false))
            << "vk=0x" << std::hex << vk << " — in VNI 's' is just a letter";
    }
}

TEST(CommitUndoExemption, Telex_ToneKeys_NotExemptInUserDefined) {
    // UserDefined exemption requires caller to set `isCustomToneKey=true`
    // after looking up customKeyMap. Without that flag (default false),
    // s/f/r/x/j are NOT exempt in UserDefined even though they are
    // hardcoded tones in Telex/SimpleTelex/Combined.
    for (uint32_t vk : {kVkS, kVkF, kVkR, kVkX, kVkJ}) {
        EXPECT_FALSE(IsCommitUndoExemptKey(vk, InputMethod::UserDefined, false, false));
    }
}

// ============================================================
// SimpleTelex parity — Telex tone keys must behave identically
// (regression: 2026-05-21 `khoong+space+BS+s` produced `khôngs`
// because exemption list omitted SimpleTelex method)
// ============================================================

TEST(CommitUndoExemption, Telex_ToneKeys_ExemptInSimpleTelex) {
    // SimpleTelex differs from Telex only in bracket/`w` handling
    // (TypingConfig.h:19). Tone keys s/f/r/x/j route through the same
    // engine path (IsTelexMode() in TypingEngine.h includes SimpleTelex),
    // so the post-BS replay path must also treat them as exempt.
    for (uint32_t vk : {kVkS, kVkF, kVkR, kVkX, kVkJ}) {
        EXPECT_TRUE(IsCommitUndoExemptKey(vk, InputMethod::SimpleTelex, false, false))
            << "vk=0x" << std::hex << vk
            << " — SimpleTelex tone modifier must be exempt to allow commit-undo replay";
    }
}

TEST(CommitUndoExemption, Vni_Digits_NotExemptInSimpleTelex) {
    // SimpleTelex inherits Telex tone semantics — digits are NOT tone keys.
    for (uint32_t vk = kVk1; vk <= kVk5; ++vk) {
        EXPECT_FALSE(IsCommitUndoExemptKey(vk, InputMethod::SimpleTelex, false, false));
    }
}

// ============================================================
// UserDefined customKeyMap tone lookup — caller computes
// `isCustomToneKey` via customKeyMap[lowercase_ascii] ∈
// {ToneAcute..ToneDot}; predicate trusts that flag in UserDefined.
// ============================================================

TEST(CommitUndoExemption, UserDefined_CustomToneKey_Exempt) {
    // Any vk can be exempt in UserDefined when isCustomToneKey=true.
    // The predicate doesn't validate WHICH vk — that's the caller's job
    // via the customKeyMap lookup.
    for (uint32_t vk : {kVkS, kVkF, kVkR, kVkX, kVkJ, kVkA, kVkB,
                        kVk1, kVk5, kVk6, kVk0}) {
        EXPECT_TRUE(IsCommitUndoExemptKey(vk, InputMethod::UserDefined,
                                          /*shift=*/false, /*esc=*/false,
                                          /*isCustomToneKey=*/true))
            << "vk=0x" << std::hex << vk;
    }
}

TEST(CommitUndoExemption, UserDefined_CustomToneKey_FlagIgnoredOutsideUserDefined) {
    // isCustomToneKey only applies in UserDefined mode. Setting it true
    // in Telex/VNI/Combined must not change exemption (those use their
    // hardcoded tone-key sets).
    EXPECT_FALSE(IsCommitUndoExemptKey(kVkB, InputMethod::Telex, false, false,
                                       /*isCustomToneKey=*/true));
    EXPECT_FALSE(IsCommitUndoExemptKey(kVkB, InputMethod::VNI, false, false,
                                       /*isCustomToneKey=*/true));
    EXPECT_FALSE(IsCommitUndoExemptKey(kVkB, InputMethod::SimpleTelex, false, false,
                                       /*isCustomToneKey=*/true));
    EXPECT_FALSE(IsCommitUndoExemptKey(kVkB, InputMethod::Combined, false, false,
                                       /*isCustomToneKey=*/true));
}

TEST(CommitUndoExemption, UserDefined_NoCustomTone_NotExempt) {
    // When caller's customKeyMap lookup returns a non-tone action,
    // isCustomToneKey=false → no exemption.
    for (uint32_t vk : {kVkS, kVkA, kVk1, kVkB}) {
        EXPECT_FALSE(IsCommitUndoExemptKey(vk, InputMethod::UserDefined,
                                           false, false,
                                           /*isCustomToneKey=*/false));
    }
}

// ============================================================
// VNI tone modifiers — exempt in VNI / Combined when Shift NOT held
// ============================================================

TEST(CommitUndoExemption, Vni_Digits1To5_ExemptInVni) {
    for (uint32_t vk = kVk1; vk <= kVk5; ++vk) {
        EXPECT_TRUE(IsCommitUndoExemptKey(vk, InputMethod::VNI, false, false))
            << "vk=0x" << std::hex << vk;
    }
}

TEST(CommitUndoExemption, Vni_Digits1To5_ExemptInCombined) {
    for (uint32_t vk = kVk1; vk <= kVk5; ++vk) {
        EXPECT_TRUE(IsCommitUndoExemptKey(vk, InputMethod::Combined, false, false));
    }
}

TEST(CommitUndoExemption, Vni_DigitsWithShift_NotExempt) {
    // Shift+digit yields punctuation on US layout — not a tone keystroke.
    for (uint32_t vk = kVk1; vk <= kVk5; ++vk) {
        EXPECT_FALSE(IsCommitUndoExemptKey(vk, InputMethod::VNI, /*shift=*/true, false))
            << "Shift+vk=0x" << std::hex << vk << " is punctuation, must not be exempt";
    }
}

TEST(CommitUndoExemption, Vni_Digit0_NotExempt) {
    // VNI '0' is clear-tone, not a tone modifier — different code path in engine.
    EXPECT_FALSE(IsCommitUndoExemptKey(kVk0, InputMethod::VNI, false, false));
}

TEST(CommitUndoExemption, Vni_Digit6_NotExempt) {
    // VNI '6/7/8/9' are modifier keys (circumflex, horn, breve), not tone —
    // these reshape the vowel, not just add a diacritic, so they fall outside
    // the "only modifies previous word" exemption.
    EXPECT_FALSE(IsCommitUndoExemptKey(kVk6, InputMethod::VNI, false, false));
}

TEST(CommitUndoExemption, Vni_Digits_NotExemptInTelex) {
    for (uint32_t vk = kVk1; vk <= kVk5; ++vk) {
        EXPECT_FALSE(IsCommitUndoExemptKey(vk, InputMethod::Telex, false, false))
            << "In Telex, digits are not tone keys";
    }
}

// ============================================================
// ESC restore-raw — exempt when escIsCancelTrigger is on
// (snapshot of HotkeyRegistry::Matches(Intent::CancelComposition, VK_ESCAPE...))
// ============================================================

TEST(CommitUndoExemption, Esc_ExemptWhenToggleOn_AllMethods) {
    for (auto m : {InputMethod::Telex, InputMethod::VNI,
                   InputMethod::Combined, InputMethod::UserDefined}) {
        EXPECT_TRUE(IsCommitUndoExemptKey(kVkEscape, m, false, /*escEnabled=*/true))
            << "method=" << static_cast<int>(m);
    }
}

TEST(CommitUndoExemption, Esc_NotExemptWhenToggleOff) {
    for (auto m : {InputMethod::Telex, InputMethod::VNI,
                   InputMethod::Combined, InputMethod::UserDefined}) {
        EXPECT_FALSE(IsCommitUndoExemptKey(kVkEscape, m, false, /*escEnabled=*/false));
    }
}

TEST(CommitUndoExemption, Esc_ShiftDoesNotChangeExemption) {
    // Shift is consulted only for VNI digits. ESC ignores shift.
    EXPECT_TRUE(IsCommitUndoExemptKey(kVkEscape, InputMethod::Telex,
                                       /*shift=*/true, /*escEnabled=*/true));
    EXPECT_TRUE(IsCommitUndoExemptKey(kVkEscape, InputMethod::Telex,
                                       /*shift=*/false, /*escEnabled=*/true));
}

// ============================================================
// Non-exempt keys — must always return false
// ============================================================

TEST(CommitUndoExemption, AlphaLetters_NotExempt) {
    // Alpha keys have their own replay branch in HandleCommitUndo. The
    // exemption rule is about the *cancel* path; alphas neither cancel
    // nor are exempt — they replay.
    for (uint32_t vk = kVkA; vk <= kVkA + 25u; ++vk) {
        if (vk == kVkS || vk == kVkF || vk == kVkR || vk == kVkX || vk == kVkJ) continue;
        EXPECT_FALSE(IsCommitUndoExemptKey(vk, InputMethod::Telex, false, true))
            << "vk=0x" << std::hex << vk;
    }
}

TEST(CommitUndoExemption, NonAlphaKeys_NotExempt) {
    // Space, Enter, ordinary letter B — none should match exemption.
    EXPECT_FALSE(IsCommitUndoExemptKey(kVkSpace, InputMethod::Telex, false, true));
    EXPECT_FALSE(IsCommitUndoExemptKey(kVkReturn, InputMethod::Telex, false, true));
    EXPECT_FALSE(IsCommitUndoExemptKey(kVkB, InputMethod::Telex, false, true));
}

// ============================================================
// Cross-cut regression: 2026-05-17 bug reproduction
// ============================================================

TEST(CommitUndoExemption, Regression_2026_05_21_SimpleTelexPostBSTone) {
    // Repro: user typed `khoong + space + BS + s` on SimpleTelex method
    // in Notepad (Win32, 30ms settle). Expected `khống` (sắc on ô).
    // Got `khôngs`. Hook log showed:
    //   "commit-undo: drop stack-top 'không' for non-tone alpha 'S'
    //    → fresh composition"
    // Root cause: IsCommitUndoExemptKey only checked Telex + Combined
    // for tone modifiers, omitting SimpleTelex. Engine treats SimpleTelex
    // tone keys identically (IsTelexMode()), so the cancel branch
    // mismatched the engine's classification and dropped the stack.
    EXPECT_TRUE(IsCommitUndoExemptKey(kVkS, InputMethod::SimpleTelex,
                                       /*shift=*/false, /*esc=*/false))
        << "After SimpleTelex bug fix, 's' must be exempt so the "
           "alpha-branch replay path fires.";
}

TEST(CommitUndoExemption, Regression_2026_05_21_UserDefinedCustomTone) {
    // Same flow as above but on UserDefined with customKeyMap bound
    // (e.g. 's' → ToneAcute mimicking Telex). The caller computes
    // isCustomToneKey via customKeyMap lookup; predicate honours it.
    EXPECT_TRUE(IsCommitUndoExemptKey(kVkS, InputMethod::UserDefined,
                                       /*shift=*/false, /*esc=*/false,
                                       /*isCustomToneKey=*/true))
        << "UserDefined with customKeyMap['s']=ToneAcute must allow "
           "post-BS replay just like Telex.";
}

TEST(CommitUndoExemption, Regression_2026_05_17_EscPostBS) {
    // Scenario: user typed `virus → space → BS → ESC`. Without ESC exemption,
    // the catch-all else demotes state from Primed to Idle, and
    // HandlePreDispatch's ESC branch (`hasPrimedCommit` condition) fails.
    // Log signature: BS log line shows `→ Primed`, then no `EscRestoreRaw[post-BS]`
    // line on ESC. Fix commits: 41ba120 + 351defa.
    EXPECT_TRUE(IsCommitUndoExemptKey(kVkEscape, InputMethod::Telex,
                                       /*shift=*/false, /*escEnabled=*/true))
        << "ESC must be exempt when bound to CancelComposition — otherwise the "
           "catch-all demote-to-Idle wipes Primed state before TryEscRestoreRaw runs.";
}

}  // namespace
}  // namespace NextKey
