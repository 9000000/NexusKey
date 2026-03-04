// NexusKey - Quick Convert (Hotkey-driven text conversion)
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "core/config/TypingConfig.h"
#include <string>
#include <vector>
#include <Windows.h>

namespace NextKey {

/// Quick-convert engine: copy → convert → paste → re-select → toast.
/// Runs headless in the main process, triggered by HookEngine hotkey callback.
class QuickConvert {
public:
    explicit QuickConvert(const ConvertConfig& config);
    void UpdateConfig(const ConvertConfig& config);

    /// Execute quick-convert (called from HookEngine on hotkey)
    void Execute();

private:
    // Clipboard operations
    [[nodiscard]] static std::wstring ReadClipboard();
    static bool WriteClipboard(const std::wstring& text);

    // Simulate Ctrl+C / Ctrl+V via SendInput
    static void SimulateCopy();
    static void SimulatePaste();

    // Wait for modifier keys to be released (prevent interference)
    static void WaitForModifiersRelease();

    // Re-select pasted text (Shift+Left × N)
    static void ReselectText(size_t charCount);

    // Apply a single conversion option to text
    [[nodiscard]] std::wstring ApplyConversion(const std::wstring& input, int optionIndex) const;

    // Get display name for a conversion option (for toast)
    [[nodiscard]] static const wchar_t* GetOptionName(int optionIndex);

    // Get list of enabled option indices
    [[nodiscard]] std::vector<int> GetEnabledOptions() const;

    // Sequential state
    struct SequentialState {
        std::wstring originText;       // Original text before any conversion
        HWND window = nullptr;         // Window where selection was made
        int currentIndex = 0;          // Current position in enabled options cycle
        DWORD lastConvertTime = 0;     // For timeout-based reset
        size_t contentHash = 0;        // Hash of converted text for change detection
    };
    static constexpr DWORD SEQUENTIAL_TIMEOUT_MS = 5000;

    [[nodiscard]] bool IsNewSelection(const std::wstring& clipText) const;
    void ResetSequentialState();

    ConvertConfig config_;
    SequentialState seqState_;
};

}  // namespace NextKey
