// src/core/pipeline/ICommitUndoExecutor.h
//
// Commit-undo output port. Implemented by HookEngine (Wave 3) so the
// CommitUndoFeature can delegate the FSM decision without coupling to
// HookEngine's KeyOutcome enum (which lives in src/app/system/). Wave N+
// will lift the FSM body into the feature for true single-owner state.
#pragma once

#include <cstdint>

namespace NextKey::Pipeline {

// Mirror of NextKey::KeyOutcome — declared here so the feature stays
// decoupled from HookEngine's enum. HookEngine maps its KeyOutcome to this
// enum at the executor adapter boundary.
enum class CommitUndoOutcome : unsigned char {
    Eat         = 0,   // ProcessKeyDown should return true (key eaten)
    Pass        = 1,   // ProcessKeyDown should return false (pass to OS)
    Fallthrough = 2,   // ProcessKeyDown should continue to step 3+
};

class ICommitUndoExecutor {
public:
    virtual ~ICommitUndoExecutor() = default;

    // Process a keystroke through the commit-undo FSM. The implementation
    // reads vnMode internally (vietnameseMode_ atomic on HookEngine).
    //
    // Modifier state is PASSED IN, never re-read inside the FSM: on Windows
    // the FSM runs synchronously inside the low-level keyboard hook callback,
    // after a command drain that can call AttachThreadInput — documented to
    // reset what GetKeyState reports for the calling thread. A same-callback
    // GetKeyState after that drain can therefore report a held Ctrl/Alt/Win
    // as up. The caller supplies the snapshot it captured before the drain.
    [[nodiscard]] virtual CommitUndoOutcome HandleCommitUndo(
        std::uint16_t vkCode,
        bool shift, bool capsLock, bool ctrl, bool alt, bool win) = 0;
};

}  // namespace NextKey::Pipeline
