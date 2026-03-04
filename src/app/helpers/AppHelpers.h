// NexusKey - App-Layer Helper Functions
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "core/config/ConfigEvent.h"
#include <cwctype>
#include <string>

namespace NextKey {

/// Signal HookEngine that a config value changed.
/// Called from dialog persistence methods after ConfigManager::Save*().
inline void SignalConfigChange() noexcept {
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
