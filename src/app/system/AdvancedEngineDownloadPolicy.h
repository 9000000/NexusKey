// VKey - Advanced engine download policy
// SPDX-License-Identifier: AGPL-3.0-only

#pragma once

#include <string>
#include <string_view>

namespace NextKey {

inline constexpr wchar_t kAdvancedEngineAssetName[] = L"vkey_engine.dll";

[[nodiscard]] std::wstring BuildAdvancedEngineReleaseUrl();
[[nodiscard]] bool IsAllowedAdvancedEngineUrl(std::wstring_view url) noexcept;

} // namespace NextKey
