// VKey browser native-messaging registration
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

namespace NextKey::BrowserHost {

/// Register the per-user native-messaging manifests for supported browsers.
/// Safe and idempotent; returns false when the companion executable is absent
/// or no browser manifest could be registered.
[[nodiscard]] bool RegisterNativeMessagingHost() noexcept;

} // namespace NextKey::BrowserHost
