// NexusKey - TSF Precompiled Header
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

// Windows headers
#include <Windows.h>
#include <msctf.h>
#include <olectl.h>

// ATL headers (optional, for COM registration)
// #include <atlbase.h>
// #include <atlcom.h>

// Standard C++ headers used throughout TSF
#include <string>
#include <memory>
#include <cstdint>

// NextKey namespace forward declarations
namespace NextKey {
class TelexEngineAdapter;
class SharedStateManager;
struct TypingConfig;
}
