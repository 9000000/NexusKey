// ClipboardReader.h — Win32 clipboard text reader with OpenClipboard retry.
//
// OpenClipboard can transiently fail when another app holds the clipboard
// (e.g. just after Ctrl+C in the target window). We retry with a short
// delay between attempts. Returns std::nullopt on persistent failure or
// when no CF_UNICODETEXT data is present.
//
// Windows-only.

#pragma once

#include <optional>
#include <string>

namespace NextKey::TestRunner::ClipboardReader {

[[nodiscard]] std::optional<std::u16string> ReadText(
    int retries = 5,
    int retryDelayMs = 50) noexcept;

}  // namespace NextKey::TestRunner::ClipboardReader
