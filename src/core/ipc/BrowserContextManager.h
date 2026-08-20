// VKey - Browser extension routing shared-memory manager
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "BrowserContextState.h"

#include <memory>
#include <string_view>

namespace NextKey {

class BrowserContextManager {
public:
    BrowserContextManager();
    ~BrowserContextManager();
    BrowserContextManager(const BrowserContextManager&) = delete;
    BrowserContextManager& operator=(const BrowserContextManager&) = delete;
    BrowserContextManager(BrowserContextManager&&) noexcept;
    BrowserContextManager& operator=(BrowserContextManager&&) noexcept;

    /// VKey owner side: create or reopen the per-session mapping.
    [[nodiscard]] bool Create();
    /// Native-host side: open the mapping created by the running VKey process.
    [[nodiscard]] bool OpenReadWrite();
    [[nodiscard]] std::uint32_t ReadGeneration() const noexcept;
    [[nodiscard]] bool Read(BrowserContextState& out) const noexcept;

    /// Publish one already-resolved domain route. Strings must be lowercase
    /// ASCII executable/hostname values and are rejected if they do not fit.
    [[nodiscard]] bool Publish(std::uint32_t ownerProcessId,
                               std::uint64_t ownerNonce,
                               std::uint64_t updatedTickMs,
                               bool focused,
                               BrowserRoute route,
                               std::string_view browserExe,
                               std::string_view hostname) noexcept;
    /// Clear only this connection's state, so a stale native host cannot erase
    /// a newer browser profile/window that published after it.
    void ClearIfOwned(std::uint32_t ownerProcessId,
                      std::uint64_t ownerNonce,
                      std::uint64_t updatedTickMs) noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace NextKey
