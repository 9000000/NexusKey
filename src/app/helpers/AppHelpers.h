// NexusKey - App-Layer Helper Functions
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "core/config/ConfigEvent.h"
#include "core/ipc/SharedStateManager.h"
#include <cwctype>
#include <string>

namespace NextKey {

/// Signal HookEngine that a config value changed.
/// Called from dialog persistence methods after ConfigManager::Save*().
/// Increments configGeneration in SharedState — HookEngine detects on next keystroke.
/// Also signals ConfigEvent for TSF DLL which still uses the Named Event path.
inline void SignalConfigChange() noexcept {
    // Bump configGeneration in SharedState (HookEngine reads this)
    SharedStateManager sm;
    if (sm.OpenReadWrite()) {
        SharedState state = sm.Read();
        if (state.IsValid()) {
            state.configGeneration++;
            sm.Write(state);
        }
    }
    // Signal ConfigEvent for TSF DLL (still uses Named Event)
    ConfigEvent event;
    if (event.Initialize()) {
        event.Signal();
    }
}

/// Convert wstring to lowercase (ASCII-safe, for app names and macro keys).
inline std::wstring ToLowerAscii(std::wstring str) noexcept {
    for (auto& c : str) c = towlower(c);
    return str;
}

}  // namespace NextKey
