// VKey - Shared quick-convert decisions and transformations
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "core/config/TypingConfig.h"

#include <cstdint>
#include <string>
#include <vector>

namespace NextKey {

enum class QuickConvertBackend : uint8_t {
    HookClipboard,
    TsfNative,
};

enum class QuickConvertOption : uint8_t {
    Encoding = 0,
    Upper = 1,
    Lower = 2,
    SentenceCase = 3,
    TitleCase = 4,
    RemoveDiacritics = 5,
};

[[nodiscard]] constexpr QuickConvertBackend DecideQuickConvertBackend(
    bool isTsfApp, bool tsfNativeReady) noexcept {
    return isTsfApp && tsfNativeReady
        ? QuickConvertBackend::TsfNative
        : QuickConvertBackend::HookClipboard;
}

[[nodiscard]] std::wstring ApplyQuickConvertOption(
    const std::wstring& input,
    QuickConvertOption option,
    const ConvertConfig& config);

[[nodiscard]] std::wstring ApplyQuickConvertConfig(
    const std::wstring& input,
    const ConvertConfig& config);

[[nodiscard]] std::vector<QuickConvertOption> GetEnabledQuickConvertOptions(
    const ConvertConfig& config);

[[nodiscard]] const wchar_t* GetQuickConvertOptionName(
    QuickConvertOption option) noexcept;

}  // namespace NextKey
