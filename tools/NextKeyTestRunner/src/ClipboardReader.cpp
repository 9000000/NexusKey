#include "ClipboardReader.h"

// clang-format off
#include <Windows.h>
// clang-format on

namespace NextKey::TestRunner::ClipboardReader {

std::optional<std::u16string> ReadText(int retries, int retryDelayMs) noexcept {
    for (int attempt = 0; attempt < retries; ++attempt) {
        if (!OpenClipboard(nullptr)) {
            Sleep(static_cast<DWORD>(retryDelayMs));
            continue;
        }

        HANDLE hMem = GetClipboardData(CF_UNICODETEXT);
        if (!hMem) {
            CloseClipboard();
            return std::nullopt;
        }

        LPCWSTR data = static_cast<LPCWSTR>(GlobalLock(hMem));
        if (!data) {
            CloseClipboard();
            return std::nullopt;
        }

        // wchar_t is 16-bit on Windows -- layout-compatible with char16_t.
        std::u16string out(reinterpret_cast<const char16_t*>(data));
        GlobalUnlock(hMem);
        CloseClipboard();
        return out;
    }
    return std::nullopt;
}

}  // namespace NextKey::TestRunner::ClipboardReader
