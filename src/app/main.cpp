// NexusKey - Core Application Entry Point
// SPDX-License-Identifier: GPL-3.0-only

#include "core/SharedStateManager.h"
#include "core/TypingConfig.h"
#include <Windows.h>
#include <iostream>

using namespace NextKey;

int main() {
    std::wcout << L"NexusKey Core v1.0.0" << std::endl;

    // Initialize shared state for Engine IPC
    SharedStateManager sharedState;
    if (!sharedState.Create()) {
        std::wcerr << L"Failed to create shared memory" << std::endl;
        // Continue - engine will use defaults (FR8)
    } else {
        std::wcout << L"SharedState created successfully" << std::endl;
    }

    // For now, just keep running (placeholder for message loop, tray, etc.)
    std::wcout << L"Press Enter to exit..." << std::endl;
    std::cin.get();

    return 0;
}
