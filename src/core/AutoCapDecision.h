// VKey - Auto-Capitalization Decision
// Copyright (c) 2024-2026 PhatMT. All rights reserved.
// SPDX-License-Identifier: AGPL-3.0-only
//
// Pure decision: given a snapshot of text up to (but not including) the
// caret, classify which "should the next char start fresh" trigger applies.
//
// Two consumers share this rule:
//   1. TSF `InspectPrecedingTextEditSession` (Win32 edit session, wchar_t
//      buffer) — collapses the trigger into a single auto-cap bool via
//      `ComputeShouldAutoCap`.
//   2. IPC anchor `DeriveAnchorFromPreceding` in `core/ipc/SharedState.h`
//      (uint16_t buffer, cross-process) — splits the trigger into the
//      `isSentenceStart` / `isLineStart` flags published to HookEngine.
//
// Linux-portable so the rule has Linux GTest coverage even though both
// consumers obtain their buffers via Windows-specific paths.

#pragma once

#include <cstddef>
#include <cstdint>

namespace NextKey {

/// Classification of the preceding-text trigger that warrants a fresh start
/// (capitalize the next letter / mark a new sentence in the IPC anchor).
enum class CapTrigger : uint8_t {
    None,         // mid-word / mid-sentence / non-sentence punct — no fresh start
    DocStart,     // empty buffer or only whitespace — both sentence + line start
    LineStart,    // last non-whitespace is `\n` or `\r` — line start, not sentence
    SentenceEnd,  // last non-whitespace is `.`, `?`, or `!` AND at least one
                  // whitespace separates it from the caret (so domains like
                  // ".com" / ".vn" do NOT trigger)
};

/// Result of a best-effort focused-control query made outside the typing hot
/// path. Unknown controls and failed queries remain conservative.
struct AutoCapControlProbe {
    bool isSupportedControl{false};
    bool textLengthKnown{false};
    bool caretPositionKnown{false};
    std::uint64_t textLength{0};
    std::uint64_t caretPosition{0};
};

/// A recognized text control is authoritative only when both independent
/// queries agree that the entire document is empty and its caret is at zero.
/// This intentionally does not infer emptiness from a zero returned by an
/// unknown/custom control.
[[nodiscard]] constexpr bool ComputeShouldAutoCapFromControlProbe(
    const AutoCapControlProbe& probe) noexcept {
    return probe.isSupportedControl
        && probe.textLengthKnown
        && probe.caretPositionKnown
        && probe.textLength == 0
        && probe.caretPosition == 0;
}

/// Maximum worker-probe age accepted by the hook-thread focus apply. It bounds
/// both 20 ms control queries plus the normally-immediate mailbox handoff; a
/// longer delay is safer to treat as unknown than to trust an old snapshot.
inline constexpr std::uint64_t kAutoCapFocusEvidenceMaxAgeMs = 50;

/// Worker-produced evidence that a specific focused document was empty.
struct AutoCapFocusEvidence {
    bool isKnownEmptyDocument{false};
    std::uintptr_t hwndOpaque{0};
    std::uintptr_t focusedChildHwndOpaque{0};
    std::uint32_t pid{0};
    std::uint64_t probeStartedAtMs{0};
};

/// Cheap hook-thread facts sampled immediately before consuming focus evidence.
struct AutoCapFocusContext {
    std::uintptr_t hwndOpaque{0};
    std::uintptr_t focusedChildHwndOpaque{0};
    std::uint32_t pid{0};
    std::uint64_t lastInputAtMs{0};
    std::uint64_t nowMs{0};
};

/// Accept worker evidence only while its exact HWND/PID/child control is
/// still focused, no real key has overtaken the probe, and the mailbox
/// handoff stayed within its bounded lifetime. Ambiguous same-millisecond
/// input fails closed.
[[nodiscard]] constexpr bool ShouldArmAutoCapFromFocusEvidence(
    const AutoCapFocusEvidence& evidence,
    const AutoCapFocusContext& current,
    std::uint64_t maxAgeMs) noexcept {
    return evidence.isKnownEmptyDocument
        && evidence.hwndOpaque != 0
        && evidence.focusedChildHwndOpaque != 0
        && evidence.pid != 0
        && evidence.probeStartedAtMs != 0
        && current.hwndOpaque == evidence.hwndOpaque
        && current.focusedChildHwndOpaque == evidence.focusedChildHwndOpaque
        && current.pid == evidence.pid
        && current.lastInputAtMs < evidence.probeStartedAtMs
        && current.nowMs >= evidence.probeStartedAtMs
        && current.nowMs - evidence.probeStartedAtMs < maxAgeMs;
}

/// POLICY, not a password verdict: "should auto-cap be suppressed for
/// password safety?" Confirmed-password and unresolvable-evidence
/// deliberately share the same action, which is why a bool is the right
/// shape here — do not read a `true` as "this IS a password field".
///
/// Evidence is only trustworthy while the exact control probed is still
/// focused: WH_MOUSE_LL / WH_KEYBOARD_LL both fire before the click or Tab
/// reaches the target app, so the worker can probe the control being LEFT
/// rather than the one being entered. A mismatch fails closed — suppressing
/// auto-cap in an ordinary field is a minor inconvenience; wrongly allowing
/// it in a real password field is not.
///
/// ACCEPTED LIMITATION (do not "fix" by probing from the hook thread): when
/// a same-window classification races the actual focus transition, auto-cap
/// can stay unavailable for the rest of that focus session, until the next
/// classification. A style-bit recheck cannot recover it because the
/// empty-document evidence was rejected by the same mismatch and cannot be
/// reconstructed without a cross-process document probe, which is
/// worker-only by design. Vietnamese composition is unaffected throughout.
[[nodiscard]] constexpr bool ShouldSuppressAutoCapForPasswordSafety(
    bool probedIsPassword,
    std::uintptr_t evidenceChildHwndOpaque,
    std::uintptr_t currentChildHwndOpaque) noexcept {
    if (evidenceChildHwndOpaque == 0 || currentChildHwndOpaque == 0
        || evidenceChildHwndOpaque != currentChildHwndOpaque) {
        return true;
    }
    return probedIsPassword;
}

/// Classify the trigger by walking back over trailing spaces/tabs and
/// inspecting the first non-whitespace char. Templated so the IPC path
/// (uint16_t / UTF-16) and the TSF path (wchar_t) share one implementation.
template <typename CharT>
[[nodiscard]] constexpr CapTrigger ClassifyCapTrigger(const CharT* buf,
                                                      std::size_t len) noexcept {
    if (buf == nullptr || len == 0) return CapTrigger::DocStart;

    std::size_t i = len;
    bool skippedWhitespace = false;
    while (i > 0) {
        const CharT c = buf[i - 1];
        if (c == CharT{' '} || c == CharT{'\t'}) {
            --i;
            skippedWhitespace = true;
        } else {
            break;
        }
    }

    if (i == 0) return CapTrigger::DocStart;

    const CharT c = buf[i - 1];
    if (c == CharT{'\n'} || c == CharT{'\r'}) return CapTrigger::LineStart;
    if ((c == CharT{'.'} || c == CharT{'?'} || c == CharT{'!'}) && skippedWhitespace) {
        return CapTrigger::SentenceEnd;
    }
    return CapTrigger::None;
}

/// Convenience wrapper for the TSF auto-cap call site: any non-`None`
/// trigger means the next typed letter should be capitalized.
[[nodiscard]] constexpr bool ComputeShouldAutoCap(const wchar_t* buf,
                                                  std::size_t len) noexcept {
    return ClassifyCapTrigger(buf, len) != CapTrigger::None;
}

}  // namespace NextKey
