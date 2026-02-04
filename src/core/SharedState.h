// NexusKey - SharedState Header
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <cstdint>

namespace NextKey {

/// SharedState struct for IPC between Core and Engine (16 bytes total)
/// Magic: 0x59454B4E ('NKEY')
struct SharedState {
    uint32_t magic;      // Magic identifier: 'NKEY' = 0x59454B4E
    uint32_t epoch;      // Config epoch counter (incremented on config change)
    uint32_t flags;      // Runtime flags (Vietnamese mode, etc.)
    uint32_t reserved;   // Future use (padding to 16 bytes)

    static constexpr uint32_t MAGIC_VALUE = 0x59454B4E;  // 'NKEY'

    bool IsValid() const { return magic == MAGIC_VALUE; }
};

// Flag bit definitions
namespace SharedFlags {
    constexpr uint32_t VIETNAMESE_MODE = 0x0001;
    constexpr uint32_t ENGINE_ENABLED = 0x0002;
}

}  // namespace NextKey
