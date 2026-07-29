// VKey — ComputeShouldAutoCap unit tests (Linux-portable)
// SPDX-License-Identifier: AGPL-3.0-only

#include <gtest/gtest.h>
#include <string>

#include "core/AutoCapDecision.h"

namespace NextKey {
namespace {

// Helper: invoke ComputeShouldAutoCap with a wide string literal.
[[nodiscard]] bool Decide(const wchar_t* s) {
    return ComputeShouldAutoCap(s, std::wstring(s).size());
}

TEST(AutoCapDecision, EmptyBufferIsDocStart) {
    EXPECT_TRUE(ComputeShouldAutoCap(nullptr, 0));
    EXPECT_TRUE(Decide(L""));
}

TEST(AutoCapDecision, OnlyWhitespaceTreatedAsDocStart) {
    EXPECT_TRUE(Decide(L" "));
    EXPECT_TRUE(Decide(L"   "));
    EXPECT_TRUE(Decide(L"\t"));
    EXPECT_TRUE(Decide(L" \t  "));
}

TEST(AutoCapDecision, NewlineCapsNextChar) {
    EXPECT_TRUE(Decide(L"hello\n"));
    EXPECT_TRUE(Decide(L"hello\r"));
    // Trailing whitespace after newline still caps (start of line + indent).
    EXPECT_TRUE(Decide(L"hello\n   "));
    EXPECT_TRUE(Decide(L"hello\r\n"));
}

TEST(AutoCapDecision, SentencePunctRequiresTrailingSpace) {
    // Sentence end with space → caps.
    EXPECT_TRUE(Decide(L"Hello. "));
    EXPECT_TRUE(Decide(L"Hello? "));
    EXPECT_TRUE(Decide(L"Hello! "));
    EXPECT_TRUE(Decide(L"Hello.   "));
    EXPECT_TRUE(Decide(L"Hello.\t"));

    // Sentence-end punct WITHOUT trailing whitespace (domain case) → no cap.
    EXPECT_FALSE(Decide(L"Hello."));
    EXPECT_FALSE(Decide(L"abc."));    // ".com" / ".vn" pre-caret
    EXPECT_FALSE(Decide(L"abc?"));
    EXPECT_FALSE(Decide(L"abc!"));
}

TEST(AutoCapDecision, MidWordContinuationDoesNotCap) {
    EXPECT_FALSE(Decide(L"hello"));
    EXPECT_FALSE(Decide(L"world "));   // 1 trailing space, but no sentence end
    EXPECT_FALSE(Decide(L"abc def"));
    EXPECT_FALSE(Decide(L"abc def "));
}

TEST(AutoCapDecision, TsfProbeRequiresDocumentStartOrReadableText) {
    EXPECT_TRUE(ComputeShouldAutoCapFromTsfProbe(
        /*atDocumentStart=*/true, /*textAvailable=*/false, nullptr, 0));
    EXPECT_FALSE(ComputeShouldAutoCapFromTsfProbe(
        /*atDocumentStart=*/false, /*textAvailable=*/false, nullptr, 0));
    EXPECT_FALSE(ComputeShouldAutoCapFromTsfProbe(
        /*atDocumentStart=*/false, /*textAvailable=*/true, L"middle ", 7));
    EXPECT_TRUE(ComputeShouldAutoCapFromTsfProbe(
        /*atDocumentStart=*/false, /*textAvailable=*/true, L"Done. ", 6));
}

TEST(AutoCapDecision, NonSentencePunctDoesNotCap) {
    // Comma, semicolon, colon, parens — not sentence-ending.
    EXPECT_FALSE(Decide(L"abc, "));
    EXPECT_FALSE(Decide(L"abc; "));
    EXPECT_FALSE(Decide(L"abc: "));
    EXPECT_FALSE(Decide(L"abc) "));
}

TEST(AutoCapDecision, NewlineWinsOverPunctRule) {
    // Newline + trailing whitespace → still doc-line start, no punct
    // requirement applies.
    EXPECT_TRUE(Decide(L"abc.\n   "));
    EXPECT_TRUE(Decide(L"abc\n"));
}

TEST(AutoCapDecision, VietnameseTextNoCap) {
    EXPECT_FALSE(Decide(L"Tiếng Việt "));
    EXPECT_TRUE(Decide(L"Hello.  "));
    EXPECT_TRUE(Decide(L"Xin chào.\n"));
}

TEST(AutoCapDecision, SupportedEmptyControlAtCaretZeroIsDocStart) {
    const AutoCapControlProbe probe{
        .isSupportedControl = true,
        .textLengthKnown = true,
        .caretPositionKnown = true,
        .textLength = 0,
        .caretPosition = 0,
    };
    EXPECT_TRUE(ComputeShouldAutoCapFromControlProbe(probe));
}

TEST(AutoCapDecision, UnknownOrFailedControlProbeStaysConservative) {
    AutoCapControlProbe probe{
        .isSupportedControl = false,
        .textLengthKnown = true,
        .caretPositionKnown = true,
        .textLength = 0,
        .caretPosition = 0,
    };
    EXPECT_FALSE(ComputeShouldAutoCapFromControlProbe(probe));

    probe.isSupportedControl = true;
    probe.textLengthKnown = false;
    EXPECT_FALSE(ComputeShouldAutoCapFromControlProbe(probe));

    probe.textLengthKnown = true;
    probe.caretPositionKnown = false;
    EXPECT_FALSE(ComputeShouldAutoCapFromControlProbe(probe));
}

TEST(AutoCapDecision, ExistingControlTextNeverArmsDocStart) {
    AutoCapControlProbe probe{
        .isSupportedControl = true,
        .textLengthKnown = true,
        .caretPositionKnown = true,
        .textLength = 12,
        .caretPosition = 6,
    };
    EXPECT_FALSE(ComputeShouldAutoCapFromControlProbe(probe));

    // Even a caret at position zero is not a fresh document when content exists.
    probe.caretPosition = 0;
    EXPECT_FALSE(ComputeShouldAutoCapFromControlProbe(probe));
}

TEST(AutoCapDecision, FreshEmptyDocumentEvidenceArmsForExactForeground) {
    const AutoCapFocusEvidence evidence{
        .isKnownEmptyDocument = true,
        .hwndOpaque = 0x1234,
        .focusedChildHwndOpaque = 0xABCD,
        .pid = 42,
        .probeStartedAtMs = 1'000,
    };
    const AutoCapFocusContext current{
        .hwndOpaque = 0x1234,
        .focusedChildHwndOpaque = 0xABCD,
        .pid = 42,
        .lastInputAtMs = 999,
        .nowMs = 1'049,
    };

    EXPECT_TRUE(ShouldArmAutoCapFromFocusEvidence(
        evidence, current, /*maxAgeMs=*/50));
}

TEST(AutoCapDecision, ChangedForegroundIdentityRejectsEmptyDocumentEvidence) {
    const AutoCapFocusEvidence evidence{
        .isKnownEmptyDocument = true,
        .hwndOpaque = 0x1234,
        .focusedChildHwndOpaque = 0xABCD,
        .pid = 42,
        .probeStartedAtMs = 1'000,
    };
    AutoCapFocusContext current{
        .hwndOpaque = 0x5678,
        .focusedChildHwndOpaque = 0xABCD,
        .pid = 42,
        .lastInputAtMs = 999,
        .nowMs = 1'001,
    };

    EXPECT_FALSE(ShouldArmAutoCapFromFocusEvidence(
        evidence, current, /*maxAgeMs=*/50));

    current.hwndOpaque = evidence.hwndOpaque;
    current.pid = 43;
    EXPECT_FALSE(ShouldArmAutoCapFromFocusEvidence(
        evidence, current, /*maxAgeMs=*/50));
}

TEST(AutoCapDecision, ChangedFocusedChildRejectsEmptyDocumentEvidence) {
    // Same top-level window and PID, but the worker probed a DIFFERENT
    // child control than the one now focused (WH_MOUSE_LL raced the
    // target app's own SetFocus()) — must fail closed even though the
    // top-level identity checks all pass.
    const AutoCapFocusEvidence evidence{
        .isKnownEmptyDocument = true,
        .hwndOpaque = 0x1234,
        .focusedChildHwndOpaque = 0xABCD,
        .pid = 42,
        .probeStartedAtMs = 1'000,
    };
    const AutoCapFocusContext current{
        .hwndOpaque = 0x1234,
        .focusedChildHwndOpaque = 0xBEEF,
        .pid = 42,
        .lastInputAtMs = 999,
        .nowMs = 1'001,
    };

    EXPECT_FALSE(ShouldArmAutoCapFromFocusEvidence(
        evidence, current, /*maxAgeMs=*/50));
}

TEST(AutoCapDecision, InputOvertakingProbeRejectsEmptyDocumentEvidence) {
    const AutoCapFocusEvidence evidence{
        .isKnownEmptyDocument = true,
        .hwndOpaque = 0x1234,
        .focusedChildHwndOpaque = 0xABCD,
        .pid = 42,
        .probeStartedAtMs = 1'000,
    };
    AutoCapFocusContext current{
        .hwndOpaque = 0x1234,
        .focusedChildHwndOpaque = 0xABCD,
        .pid = 42,
        .lastInputAtMs = 1'000,
        .nowMs = 1'001,
    };

    // Equality is stale too: the millisecond clock cannot order a key and the
    // probe within the same tick, so ambiguity must fail closed.
    EXPECT_FALSE(ShouldArmAutoCapFromFocusEvidence(
        evidence, current, /*maxAgeMs=*/50));

    current.lastInputAtMs = 1'001;
    EXPECT_FALSE(ShouldArmAutoCapFromFocusEvidence(
        evidence, current, /*maxAgeMs=*/50));
}

TEST(AutoCapDecision, ExpiredOrInvalidEmptyDocumentEvidenceFailsClosed) {
    AutoCapFocusEvidence evidence{
        .isKnownEmptyDocument = true,
        .hwndOpaque = 0x1234,
        .focusedChildHwndOpaque = 0xABCD,
        .pid = 42,
        .probeStartedAtMs = 1'000,
    };
    AutoCapFocusContext current{
        .hwndOpaque = 0x1234,
        .focusedChildHwndOpaque = 0xABCD,
        .pid = 42,
        .lastInputAtMs = 999,
        .nowMs = 1'050,
    };

    EXPECT_FALSE(ShouldArmAutoCapFromFocusEvidence(
        evidence, current, /*maxAgeMs=*/50));

    current.nowMs = 999;
    EXPECT_FALSE(ShouldArmAutoCapFromFocusEvidence(
        evidence, current, /*maxAgeMs=*/50));

    current.nowMs = 1'001;
    evidence.pid = 0;
    EXPECT_FALSE(ShouldArmAutoCapFromFocusEvidence(
        evidence, current, /*maxAgeMs=*/50));

    evidence.pid = current.pid;
    evidence.isKnownEmptyDocument = false;
    EXPECT_FALSE(ShouldArmAutoCapFromFocusEvidence(
        evidence, current, /*maxAgeMs=*/50));
}

TEST(AutoCapDecision, MatchingChildTrustsProbedPasswordVerdict) {
    EXPECT_TRUE(ShouldSuppressAutoCapForPasswordSafety(
        /*probedIsPassword=*/true, 0xABCD, 0xABCD));
    EXPECT_FALSE(ShouldSuppressAutoCapForPasswordSafety(
        /*probedIsPassword=*/false, 0xABCD, 0xABCD));
}

TEST(AutoCapDecision, MismatchedOrUnknownChildSuppressesForSafety) {
    // Probed a different control than the one now focused.
    EXPECT_TRUE(ShouldSuppressAutoCapForPasswordSafety(
        /*probedIsPassword=*/false, 0xABCD, 0xBEEF));
    // Either side unknown (failed query) — still conservative.
    EXPECT_TRUE(ShouldSuppressAutoCapForPasswordSafety(
        /*probedIsPassword=*/false, 0, 0xABCD));
    EXPECT_TRUE(ShouldSuppressAutoCapForPasswordSafety(
        /*probedIsPassword=*/false, 0xABCD, 0));
}

// This is a POLICY function, not a password detector: a `true` means "suppress
// auto-cap for safety", which deliberately covers both confirmed-password and
// unresolvable-evidence. Locking that here so a future reader doesn't "fix" the
// mismatch case into a tri-state — the accepted cost is documented in
// AutoCapDecision.h (auto-cap may stay off for the focus session after a race).
TEST(AutoCapDecision, SuppressionIsPolicyNotAPasswordVerdict) {
    // Confirmed password and unresolvable evidence are indistinguishable by
    // design — both return the same suppress action.
    EXPECT_EQ(ShouldSuppressAutoCapForPasswordSafety(true, 0xABCD, 0xABCD),
              ShouldSuppressAutoCapForPasswordSafety(false, 0xABCD, 0xBEEF));
    // The ONLY combination that permits auto-cap is a confirmed-normal control
    // whose probed identity still matches what is focused now.
    EXPECT_FALSE(ShouldSuppressAutoCapForPasswordSafety(false, 0xABCD, 0xABCD));
}

}  // namespace
}  // namespace NextKey
