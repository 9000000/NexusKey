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
    // Non-movable on purpose: every accessor dereferences impl_ unchecked, so
    // a moved-from instance would be a null-deref waiting to happen.
    BrowserContextManager(BrowserContextManager&&) = delete;
    BrowserContextManager& operator=(BrowserContextManager&&) = delete;

    /// Create or reopen the per-session mapping. Both VKey and the native host
    /// call this: whichever starts first owns creation.
    [[nodiscard]] bool Create();
    [[nodiscard]] std::uint32_t ReadGeneration() const noexcept;
    [[nodiscard]] bool Read(BrowserContextState& out) const noexcept;

    /// Publish one already-resolved domain route. Strings must be lowercase
    /// ASCII executable/hostname values and are rejected if they do not fit.
    [[nodiscard]] bool Publish(std::uint32_t ownerProcessId,
                               std::uint64_t ownerNonce,
                               std::uint64_t updatedTickMs,
                               bool focused,
                               BrowserRoute route,
                               BrowserMode mode,
                               std::string_view browserExe,
                               std::string_view hostname) noexcept;
    /// Record an accepted V/E toggle for the focused browser context. This is
    /// called on the hook thread and therefore uses a zero-timeout mutex try.
    [[nodiscard]] bool PublishModeEvent(bool vietnamese) noexcept;
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
