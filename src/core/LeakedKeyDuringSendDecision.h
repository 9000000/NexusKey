// VKey - Pure decision: should a physical key that leaked into the injection
// window be eaten, on a multi-process renderer?
// SPDX-License-Identifier: AGPL-3.0-only
//
// Extracted from HookEngine::LowLevelKeyboardProc so the logic can be unit-
// tested on Linux (HookEngine.cpp itself is Windows-only). Mirrors the
// CjkSwitchDecision.h / DigitLedWordDecision.h pattern.
//
// ── The problem (issue #206, "nhảy loạn" / character reorder) ──
// While the active injector is mid-Replace() (`OutputDispatcher::IsSending()`),
// the low-level keyboard hook is re-entered for events the OS interleaves into
// the same window. Our own synthetic events carry VKEY_EXTRA_INFO and are
// filtered out earlier (HookEngine.cpp ~L975). What remains in the `sending_`
// branch is *physical* keys that landed in the queue during the injection —
// the in-flight transform key's auto-repeat key-downs and its key-up. The
// existing "safety backup" passed those straight through (CallNextHookEx),
// which on a multi-process renderer (Electron / WebView2 / RDP) interleaves
// them between our synthetic backspaces and VK_PACKET chars. That stream is
// then reordered by the renderer's async/cross-process input pipeline —
// "tieengs" → "tiếngs"/"tiếnsg", a stray repeat lands mid-word, etc.
//
// ── Why eating these specific events is safe (no dropped keystroke) ──
// We eat ONLY events whose vk equals the key currently being injected for
// (`sendingForVk`). Those are:
//   • the in-flight key's auto-repeat key-downs — its key-down was already
//     eaten and transformed; a repeat carries no new intent, and
//   • the in-flight key's key-up — its key-down was eaten, so passing the up
//     through would be an unbalanced up the app never saw a down for anyway.
// A *different* vk (the genuinely-typed next key, a modifier release, etc.) is
// left to pass through untouched — never silently dropped. A genuine second
// press of the *same* letter landing inside the (sub-15 ms) injection window
// is not physically reachable at human typing speed, so same-vk == no-intent
// in practice. The full defense for the rare different-vk fast-burst reorder
// is a defer-and-replay queue (tracked separately); this decision closes the
// proven, zero-data-loss part.
//
// Single-process renderers (vanilla Win32 — Notepad, Word, …) preserve
// SendInput FIFO ordering, so behavior there is intentionally unchanged.

#pragma once

namespace NextKey {

struct LeakedKeyDuringSendInputs {
    bool isSending;                // OutputDispatcher::IsSending() — injection in flight
    bool hasMultiProcessRenderer;  // active injector trait (Electron/WebView2/RDP-compat)
    unsigned int eventVk;          // vk of the physical key re-entering the hook
    unsigned int sendingForVk;     // vk whose transform is currently injecting (0 = none)
};

/// Pure: no I/O, no syscalls, safe from any thread. Returns true iff the hook
/// should EAT (suppress) the leaked physical key instead of passing it through.
[[nodiscard]] inline bool DecideEatLeakedKeyDuringSend(
        const LeakedKeyDuringSendInputs& in) noexcept {
    // Not injecting → this path is not our concern (normal pass-through).
    if (!in.isSending) return false;
    // Single-process renderer keeps SendInput FIFO ordering — leaving the
    // physical key in place cannot reorder against our synthetics.
    if (!in.hasMultiProcessRenderer) return false;
    // No identified in-flight key → stay conservative, never eat blindly.
    if (in.sendingForVk == 0) return false;
    // Eat only the in-flight key's own repeat/up events (no new user intent).
    // Any other vk (next key, modifier) passes through — no dropped keystroke.
    return in.eventVk == in.sendingForVk;
}

}  // namespace NextKey
