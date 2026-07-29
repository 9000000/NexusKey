// VKey - Advanced engine installer
// SPDX-License-Identifier: AGPL-3.0-only

#pragma once

#include <windows.h>

namespace NextKey {

class AdvancedEngineInstaller final {
public:
    [[nodiscard]] static bool EnsureInstalledWithUi(HWND parent);
};

} // namespace NextKey
