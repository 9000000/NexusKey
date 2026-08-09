// VKey - committed-text restore policy shared by Hook and TSF
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "IInputEngine.h"

#include <cstdint>
#include <utility>

namespace NextKey {

enum class CommittedTextRestoreResult : std::uint8_t {
    Failed,
    ReplayedHistory,
    SeededVisibleText,
};

enum class CommittedTextRestorePreference : std::uint8_t {
    ReplayHistory,
    VisibleText,
};

/// Restores the engine behind already-committed visible text. Exact key-history
/// replay preserves modifier provenance when it still renders the same word;
/// otherwise the visible host text wins so editing never continues from a
/// mismatched buffer. A caller handling Backspace can prefer a clean visible-
/// text seed because an equivalent render does not guarantee that the engine's
/// next undo matches deletion of the displayed glyph. The replay callback must
/// not allocate or block when this helper is reached from the keyboard hook.
template <typename Replay>
[[nodiscard]] CommittedTextRestoreResult RestoreCommittedText(
    IInputEngine& engine, const std::wstring& visibleText, Replay&& replay,
    CommittedTextRestorePreference preference =
        CommittedTextRestorePreference::ReplayHistory) {
    if (preference == CommittedTextRestorePreference::VisibleText) {
        engine.Reset();
        if (engine.SeedFromText(visibleText) && engine.Peek() == visibleText) {
            return CommittedTextRestoreResult::SeededVisibleText;
        }
    }

    engine.Reset();
    std::forward<Replay>(replay)(engine);
    if (engine.Peek() == visibleText) {
        return CommittedTextRestoreResult::ReplayedHistory;
    }

    if (preference == CommittedTextRestorePreference::ReplayHistory) {
        engine.Reset();
        if (engine.SeedFromText(visibleText) && engine.Peek() == visibleText) {
            return CommittedTextRestoreResult::SeededVisibleText;
        }
    }

    engine.Reset();
    return CommittedTextRestoreResult::Failed;
}

}  // namespace NextKey
