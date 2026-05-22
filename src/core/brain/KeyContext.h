// src/core/brain/KeyContext.h
//
// Per-keystroke context handed to every feature. Built by Brain at the top
// of HandleKey, then passed by const ref through the dispatch loop. POD-like
// — no allocation, no virtual calls in construction.
#pragma once

#include <cstdint>
#include "core/brain/ICompositionSession.h"

namespace NextKey::Brain {

struct KeyContext {
    std::uint16_t            vk;            // Win32 VK code, e.g. 'A'=0x41
    wchar_t                  keyChar;       // resolved char (after CapsLock/Shift), 0 if non-alpha
    bool                     shift;
    bool                     capsLock;
    bool                     ctrl;
    bool                     alt;
    bool                     win;

    const ICompositionSession& session;     // const view, lifetime tied to HookEngine

    // Optional: re-inject vk after Handled — set by PostEngine features (e.g.
    // BackwardEdit) when they need to deliver the original keystroke alongside
    // their BS+text intents. 0 = no reinject. PreEngine features must leave 0.
    std::uint16_t            reinjectVk = 0;
};

}  // namespace NextKey::Brain
