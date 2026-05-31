// VKey - Pure decision function for spreadsheet-formula segment tracking
// SPDX-License-Identifier: AGPL-3.0-only
//
// A "segment" is the run of keystrokes since the last cell/line boundary
// (Enter / Tab / Esc / navigation / context reset). In a spreadsheet host
// (Excel), a segment whose FIRST content key is '=' is a *formula*. While
// inside a formula segment the output layer must suppress its U+202F
// autocomplete-dismiss bait char: Excel's formula autocomplete is a separate
// dropdown, not a Chromium-style inline selection, so the bait's extra
// backspace over-deletes (eats the leading '=') and strands a U+202F glyph.
//
// Extracted from HookEngine (Win-only: GetKeyState + VK_* constants) so the
// state machine is unit-testable on Linux — same pattern as
// DigitLedWordDecision.h / AutoCapStateTransition.h / CommitUndoExemption.h.
// The caller owns the VK→kind classification (kept out of this header so it
// stays Win32-free); this function only advances the FSM given the kind.

#pragma once

namespace NextKey {

// State carried across keystrokes (hook-thread-owned in HookEngine).
struct FormulaSegmentState {
    bool atSegmentStart = true;  // next content key is the segment's first
    bool inFormula      = false; // segment's first content key was '='
};

// Per-keystroke classification, computed by the caller from VK + modifiers.
enum class FormulaKeyKind {
    Boundary,      // Enter/Tab/Esc/arrows/Home/End/PgUp/PgDn — ends the segment
    Passive,       // modifiers, Backspace, Delete — don't open a content segment
    EqualsStart,   // an unshifted '=' (formula trigger; only acts at segment start)
    OtherContent   // any other content key
};

// Advance the segment FSM. Pure / branch-only — no allocation, no I/O.
[[nodiscard]] constexpr FormulaSegmentState
NextFormulaSegmentState(FormulaSegmentState s, FormulaKeyKind kind) noexcept {
    switch (kind) {
        case FormulaKeyKind::Boundary:
            // New cell/line — forget formula-ness, re-arm segment-start.
            return FormulaSegmentState{/*atSegmentStart=*/true, /*inFormula=*/false};
        case FormulaKeyKind::Passive:
            // Doesn't open content; leaves both flags untouched.
            return s;
        case FormulaKeyKind::EqualsStart:
            // '=' as the first content key opens a formula. A '=' later in the
            // segment (e.g. "=a=b") is just a comparison — keep current state.
            if (s.atSegmentStart)
                return FormulaSegmentState{/*atSegmentStart=*/false, /*inFormula=*/true};
            return FormulaSegmentState{/*atSegmentStart=*/false, s.inFormula};
        case FormulaKeyKind::OtherContent:
            // First content key that isn't '=' → segment is plain text, not a
            // formula. Mid-segment content leaves formula-ness unchanged.
            if (s.atSegmentStart)
                return FormulaSegmentState{/*atSegmentStart=*/false, /*inFormula=*/false};
            return FormulaSegmentState{/*atSegmentStart=*/false, s.inFormula};
    }
    return s;  // unreachable; satisfies non-void constexpr control paths
}

}  // namespace NextKey
