// VKey - Advanced engine download policy
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <string>
#include <string_view>

namespace NextKey {

inline constexpr wchar_t kAdvancedEngineAssetName[] = L"vkey_engine.dll";

[[nodiscard]] std::wstring BuildAdvancedEngineReleaseUrl();

/// The detached signature published beside the engine. Release verifies this
/// instead of a baked hash, so an engine downloaded without it is unloadable.
[[nodiscard]] std::wstring BuildAdvancedEngineSignatureUrl();
[[nodiscard]] bool IsAllowedAdvancedEngineUrl(std::wstring_view url) noexcept;

} // namespace NextKey
