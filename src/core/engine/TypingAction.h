// NexusKey - User-mappable input actions
// Copyright (c) 2024-2026 PhatMT. All rights reserved.
// SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-NexusKey-Commercial
// Dual-licensed: GPL-3.0 for open-source use, commercial license for proprietary use.
// See LICENSE and LICENSE-COMMERCIAL in the project root.
//
// Action vocabulary for TypingEngine dispatch and (future) per-user keymap.
// Header is intentionally dependency-free so TypingConfig can hold a
// `customKeyMap: array<TypingAction, 128>` without circular includes.

#pragma once

#include <cstdint>

namespace NextKey {

/// Action that a user keystroke is classified as before dispatch.
/// `ClassifyKey` returns one of these based on (key, mode) only —
/// runtime applicability (state shape, escape flags, English protection,
/// spell-check gating) stays in the per-action handlers in TypingEngine.
///
/// G-3 introduces this enum to centralize the dispatch shape. G-4 will
/// expose it via `TypingConfig::customKeyMap` so users can rebind keys
/// without modifying engine code.
enum class TypingAction : uint8_t {
    None = 0,        // Not an IME-bound key — handled as literal char

    // -- Tone application --
    ClearTone,       // Telex z, VNI 0
    ToneAcute,       // sắc — Telex s, VNI 1
    ToneGrave,       // huyền — Telex f, VNI 2
    ToneHook,        // hỏi — Telex r, VNI 3
    ToneTilde,       // ngã — Telex x, VNI 4
    ToneDot,         // nặng — Telex j, VNI 5

    // -- Telex modifier keys (per-vowel doubling triggers) --
    CircumflexA,     // Telex a (aa→â)
    CircumflexE,     // Telex e (ee→ê)
    CircumflexO,     // Telex o (oo→ô)
    HornW,           // Telex w (u→ư, o→ơ; ă fallback when no horn target)
    HornInsertO,     // Telex [ (always inserts ơ as new state)
    HornInsertU,     // Telex ] (always inserts ư as new state)
    StrokeD,         // Telex d (dd→đ)

    // -- VNI modifier keys (single-key, picks target vowel from state) --
    VniCircumflex,   // VNI 6
    VniHorn,         // VNI 7
    VniBreve,        // VNI 8
    VniStroke,       // VNI 9
};

/// Classify a key into a TypingAction based purely on (key, mode).
/// Pure function — no state lookup, no side effects. Returns
/// `TypingAction::None` for keys with no IME meaning under the
/// supplied mode (the dispatcher then handles them as literal chars).
///
/// `lower` must be `towlower(c)` of the original key. Pass mode flags
/// directly so this header stays free of TypingConfig.
///
/// Combined mode: pass both `isTelex=true` and `isVni=true`. Telex
/// classification wins for letters (s/f/r/x/j/a/e/o/w/d/z and
/// brackets); VNI classification wins for digits 0-9. There is no
/// overlap so the order is deterministic.
[[nodiscard]] constexpr TypingAction ClassifyKey(wchar_t lower,
                                                  bool isTelex,
                                                  bool isVni) noexcept {
    if (isTelex) {
        switch (lower) {
            case L'z': return TypingAction::ClearTone;
            case L's': return TypingAction::ToneAcute;
            case L'f': return TypingAction::ToneGrave;
            case L'r': return TypingAction::ToneHook;
            case L'x': return TypingAction::ToneTilde;
            case L'j': return TypingAction::ToneDot;
            case L'a': return TypingAction::CircumflexA;
            case L'e': return TypingAction::CircumflexE;
            case L'o': return TypingAction::CircumflexO;
            case L'w': return TypingAction::HornW;
            case L'[': return TypingAction::HornInsertO;
            case L']': return TypingAction::HornInsertU;
            case L'd': return TypingAction::StrokeD;
            default: break;
        }
    }
    if (isVni) {
        switch (lower) {
            case L'0': return TypingAction::ClearTone;
            case L'1': return TypingAction::ToneAcute;
            case L'2': return TypingAction::ToneGrave;
            case L'3': return TypingAction::ToneHook;
            case L'4': return TypingAction::ToneTilde;
            case L'5': return TypingAction::ToneDot;
            case L'6': return TypingAction::VniCircumflex;
            case L'7': return TypingAction::VniHorn;
            case L'8': return TypingAction::VniBreve;
            case L'9': return TypingAction::VniStroke;
            default: break;
        }
    }
    return TypingAction::None;
}

}  // namespace NextKey
