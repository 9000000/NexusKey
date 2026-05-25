// src/core/engine/rule/IModifierSubExecutor.h
//
// Modifier SUB-handler output port. Implemented by TypingEngine so each
// ModifierProposal can delegate to the engine's existing per-action body
// without lifting state mutation out of TypingEngine.
//
// Wave 8.1 introduces this port with a single method (HandleAdjacentCircumflex).
// Future waves (W8.2 HornW, W8.3 Breve P7, W8.4 StrokeD/Bracket, W8.5 VNI
// 6/7/8) extend this port with additional sub-handler methods.
//
// Distinct from IModifierExecutor (W7.3): that port owns the OUTER dispatch
// (HandleModifierAction → ProcessModifier switch); this port owns INNER
// sub-handlers reached by ProcessModifier's per-action cases.
#pragma once

#include "core/engine/TypingAction.h"

namespace NextKey::EngineRule {

class IModifierSubExecutor {
public:
    virtual ~IModifierSubExecutor() = default;

    // CircumflexA/E/O routed from ProcessModifier. Body lives on TypingEngine
    // (private virtual override) — wrap-don't-lift per W7 retro AD-1.
    [[nodiscard]] virtual bool HandleAdjacentCircumflex(TypingAction action,
                                                        wchar_t keyChar) = 0;

    // HornW (Telex `w` modifier, P1-P8). Body lives on TypingEngine; this
    // port lets HornModifierProposal route dispatch through the proposal
    // layer (W8.2).
    [[nodiscard]] virtual bool HandleHornW(TypingAction action,
                                            wchar_t keyChar) = 0;
};

}  // namespace NextKey::EngineRule
