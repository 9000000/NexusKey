// NexusKey - SharedStateManager
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "SharedState.h"
#include <memory>

namespace NextKey {

/// Manages shared memory IPC between Core and Engine
/// Creates/opens "Local\\NexusKeySharedState" shared memory region
class SharedStateManager {
public:
    SharedStateManager();
    ~SharedStateManager();

    // Non-copyable, movable
    SharedStateManager(const SharedStateManager&) = delete;
    SharedStateManager& operator=(const SharedStateManager&) = delete;
    SharedStateManager(SharedStateManager&&) noexcept;
    SharedStateManager& operator=(SharedStateManager&&) noexcept;

    /// Create shared memory (Core side)
    [[nodiscard]] bool Create();

    /// Open existing shared memory (Engine side, read-only)
    [[nodiscard]] bool Open();

    /// Open existing shared memory with read-write access (for flag toggling from DLL)
    [[nodiscard]] bool OpenReadWrite();

    /// Read current state (validates magic before returning)
    [[nodiscard]] SharedState Read() const noexcept;

    /// Write state (Core side only)
    void Write(const SharedState& state) noexcept;

    /// Read flags directly from memory-mapped region (zero-copy, for hot path)
    [[nodiscard]] uint32_t ReadFlags() const noexcept;

    /// Toggle a flag bit atomically (safe for concurrent access from DLL/EXE)
    /// Requires OpenReadWrite() or Create()
    void ToggleFlag(uint32_t flagBit) noexcept;

    /// Check if connected to valid shared memory
    [[nodiscard]] bool IsConnected() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> pImpl_;
};

}  // namespace NextKey
