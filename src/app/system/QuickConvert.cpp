// NexusKey - Quick Convert Implementation
// SPDX-License-Identifier: GPL-3.0-only

#include "QuickConvert.h"
#include "ToastPopup.h"
#include "HookEngine.h"
#include "core/engine/CodeTableConverter.h"
#include <functional>

namespace NextKey {

// Conversion option indices (matching plan)
enum ConvertOption : int {
    kEncoding = 0,
    kToUpper = 1,
    kToLower = 2,
    kCapsFirst = 3,
    kCapsEach = 4,
    kRemoveDiacritics = 5
};

QuickConvert::QuickConvert(const ConvertConfig& config)
    : config_(config) {
}

void QuickConvert::UpdateConfig(const ConvertConfig& config) {
    config_ = config;
    ResetSequentialState();
}

// ═══════════════════════════════════════════════════════════
// Execute flow
// ═══════════════════════════════════════════════════════════

void QuickConvert::Execute() {
    // 1. Wait for modifier keys to be released
    WaitForModifiersRelease();

    // 2. Save current clipboard content
    std::wstring savedClipboard = ReadClipboard();

    // 3. Simulate Ctrl+C to copy selection
    SimulateCopy();
    Sleep(100);  // Wait for clipboard to update

    // 4. Read clipboard text (the selection)
    std::wstring clipText = ReadClipboard();
    if (clipText.empty()) {
        // Nothing selected — restore clipboard and bail
        if (!savedClipboard.empty()) {
            WriteClipboard(savedClipboard);
        }
        return;
    }

    // 5. Determine enabled options
    auto enabledOptions = GetEnabledOptions();
    if (enabledOptions.empty()) {
        // No conversions enabled — restore and bail
        WriteClipboard(savedClipboard);
        return;
    }

    std::wstring result;
    const wchar_t* toastMsg = nullptr;  // Which conversion was applied

    if (config_.sequential && config_.autoPaste) {
        // Sequential mode: cycle through enabled options on repeated presses
        HWND currentWindow = GetForegroundWindow();
        DWORD now = GetTickCount();

        if (IsNewSelection(clipText)) {
            // New selection: start fresh cycle
            seqState_.originText = clipText;
            seqState_.window = currentWindow;
            seqState_.currentIndex = 0;
            seqState_.contentHash = std::hash<std::wstring>{}(clipText);
        } else {
            // Same selection: advance to next option
            seqState_.currentIndex++;
            if (seqState_.currentIndex >= static_cast<int>(enabledOptions.size())) {
                // Wrap back to original text
                seqState_.currentIndex = -1;  // -1 = show original
            }
        }

        seqState_.lastConvertTime = now;

        if (seqState_.currentIndex < 0) {
            // Show original text
            result = seqState_.originText;
            toastMsg = L"\x2192 G\x1ED1" L"c";  // → Gốc
        } else {
            int optIdx = enabledOptions[seqState_.currentIndex];
            result = ApplyConversion(seqState_.originText, optIdx);
            toastMsg = GetOptionName(optIdx);
        }

        // Update hash for next comparison
        seqState_.contentHash = std::hash<std::wstring>{}(result);
    } else {
        // Non-sequential mode: apply all enabled conversions at once
        result = clipText;

        // Encoding conversion first (if source != dest)
        if (config_.sourceEncoding != config_.destEncoding) {
            auto srcTable = static_cast<CodeTable>(config_.sourceEncoding);
            auto dstTable = static_cast<CodeTable>(config_.destEncoding);
            std::wstring unicode = CodeTableConverter::DecodeString(result, srcTable);
            result = CodeTableConverter::EncodeString(unicode, dstTable);
        }

        // Text transformations (mutually exclusive case options, removeMark is independent)
        if (config_.removeMark) {
            result = CodeTableConverter::RemoveDiacritics(result);
            toastMsg = GetOptionName(kRemoveDiacritics);
        }
        if (config_.allCaps) {
            result = CodeTableConverter::ToUpper(result);
            toastMsg = GetOptionName(kToUpper);
        } else if (config_.allLower) {
            result = CodeTableConverter::ToLower(result);
            toastMsg = GetOptionName(kToLower);
        } else if (config_.capsFirst) {
            result = CodeTableConverter::CapitalizeFirstOfSentence(result);
            toastMsg = GetOptionName(kCapsFirst);
        } else if (config_.capsEach) {
            result = CodeTableConverter::CapitalizeEachWord(result);
            toastMsg = GetOptionName(kCapsEach);
        }

        // If only encoding conversion, show encoding toast
        if (!toastMsg && config_.sourceEncoding != config_.destEncoding) {
            toastMsg = GetOptionName(kEncoding);
        }
    }

    // 6. Check if anything changed
    if (result == clipText) {
        // No change — restore original clipboard
        WriteClipboard(savedClipboard);
        return;
    }

    if (config_.autoPaste) {
        // 7a. Write converted text to clipboard and paste it
        WriteClipboard(result);
        SimulatePaste();
        Sleep(50);  // Wait for paste to complete

        // 8. Re-select pasted text
        ReselectText(result.size());

        // Don't restore clipboard — user expects converted text to stay
    } else {
        // 7b. Just write to clipboard (no paste)
        WriteClipboard(result);
    }

    // 9. Toast notification — show which conversion was applied
    if (config_.alertDone && toastMsg) {
        ToastPopup::Show(toastMsg, 800);
    }
}

// ═══════════════════════════════════════════════════════════
// Clipboard operations
// ═══════════════════════════════════════════════════════════

std::wstring QuickConvert::ReadClipboard() {
    if (!OpenClipboard(nullptr)) return L"";
    HANDLE hData = GetClipboardData(CF_UNICODETEXT);
    if (!hData) {
        CloseClipboard();
        return L"";
    }
    auto* pText = static_cast<const wchar_t*>(GlobalLock(hData));
    if (!pText) {
        CloseClipboard();
        return L"";
    }
    std::wstring text(pText);
    GlobalUnlock(hData);
    CloseClipboard();
    return text;
}

bool QuickConvert::WriteClipboard(const std::wstring& text) {
    if (!OpenClipboard(nullptr)) return false;
    EmptyClipboard();
    size_t bytes = (text.size() + 1) * sizeof(wchar_t);
    HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, bytes);
    if (!hMem) {
        CloseClipboard();
        return false;
    }
    auto* pDst = static_cast<wchar_t*>(GlobalLock(hMem));
    if (!pDst) {
        GlobalFree(hMem);
        CloseClipboard();
        return false;
    }
    memcpy(pDst, text.c_str(), bytes);
    GlobalUnlock(hMem);
    SetClipboardData(CF_UNICODETEXT, hMem);
    CloseClipboard();
    return true;
}

// ═══════════════════════════════════════════════════════════
// Key simulation
// ═══════════════════════════════════════════════════════════

void QuickConvert::SimulateCopy() {
    INPUT inputs[4] = {};

    // Ctrl down
    inputs[0].type = INPUT_KEYBOARD;
    inputs[0].ki.wVk = VK_CONTROL;
    inputs[0].ki.dwExtraInfo = HookEngine::NEXUSKEY_EXTRA_INFO;

    // C down
    inputs[1].type = INPUT_KEYBOARD;
    inputs[1].ki.wVk = 'C';
    inputs[1].ki.dwExtraInfo = HookEngine::NEXUSKEY_EXTRA_INFO;

    // C up
    inputs[2].type = INPUT_KEYBOARD;
    inputs[2].ki.wVk = 'C';
    inputs[2].ki.dwFlags = KEYEVENTF_KEYUP;
    inputs[2].ki.dwExtraInfo = HookEngine::NEXUSKEY_EXTRA_INFO;

    // Ctrl up
    inputs[3].type = INPUT_KEYBOARD;
    inputs[3].ki.wVk = VK_CONTROL;
    inputs[3].ki.dwFlags = KEYEVENTF_KEYUP;
    inputs[3].ki.dwExtraInfo = HookEngine::NEXUSKEY_EXTRA_INFO;

    SendInput(4, inputs, sizeof(INPUT));
}

void QuickConvert::SimulatePaste() {
    INPUT inputs[4] = {};

    // Ctrl down
    inputs[0].type = INPUT_KEYBOARD;
    inputs[0].ki.wVk = VK_CONTROL;
    inputs[0].ki.dwExtraInfo = HookEngine::NEXUSKEY_EXTRA_INFO;

    // V down
    inputs[1].type = INPUT_KEYBOARD;
    inputs[1].ki.wVk = 'V';
    inputs[1].ki.dwExtraInfo = HookEngine::NEXUSKEY_EXTRA_INFO;

    // V up
    inputs[2].type = INPUT_KEYBOARD;
    inputs[2].ki.wVk = 'V';
    inputs[2].ki.dwFlags = KEYEVENTF_KEYUP;
    inputs[2].ki.dwExtraInfo = HookEngine::NEXUSKEY_EXTRA_INFO;

    // Ctrl up
    inputs[3].type = INPUT_KEYBOARD;
    inputs[3].ki.wVk = VK_CONTROL;
    inputs[3].ki.dwFlags = KEYEVENTF_KEYUP;
    inputs[3].ki.dwExtraInfo = HookEngine::NEXUSKEY_EXTRA_INFO;

    SendInput(4, inputs, sizeof(INPUT));
}

void QuickConvert::WaitForModifiersRelease() {
    // Spin until all modifier keys are released (max ~500ms)
    for (int i = 0; i < 100; ++i) {
        bool anyDown = (GetAsyncKeyState(VK_CONTROL) & 0x8000) ||
                       (GetAsyncKeyState(VK_SHIFT) & 0x8000) ||
                       (GetAsyncKeyState(VK_MENU) & 0x8000) ||
                       (GetAsyncKeyState(VK_LWIN) & 0x8000) ||
                       (GetAsyncKeyState(VK_RWIN) & 0x8000);
        if (!anyDown) break;
        Sleep(5);
    }
}

void QuickConvert::ReselectText(size_t charCount) {
    if (charCount == 0) return;

    // Send Shift+Left × charCount to re-select the pasted text
    // Batch into groups to avoid SendInput limits
    constexpr size_t BATCH = 32;

    for (size_t sent = 0; sent < charCount; ) {
        size_t batchSize = (charCount - sent > BATCH) ? BATCH : (charCount - sent);
        size_t inputCount = 2 + batchSize * 2;  // Shift down + (Left down + Left up) * N + Shift up
        std::vector<INPUT> inputs(inputCount, INPUT{});

        // Shift down
        inputs[0].type = INPUT_KEYBOARD;
        inputs[0].ki.wVk = VK_SHIFT;
        inputs[0].ki.dwExtraInfo = HookEngine::NEXUSKEY_EXTRA_INFO;

        for (size_t i = 0; i < batchSize; ++i) {
            size_t base = 1 + i * 2;
            // Left down
            inputs[base].type = INPUT_KEYBOARD;
            inputs[base].ki.wVk = VK_LEFT;
            inputs[base].ki.dwExtraInfo = HookEngine::NEXUSKEY_EXTRA_INFO;
            // Left up
            inputs[base + 1].type = INPUT_KEYBOARD;
            inputs[base + 1].ki.wVk = VK_LEFT;
            inputs[base + 1].ki.dwFlags = KEYEVENTF_KEYUP;
            inputs[base + 1].ki.dwExtraInfo = HookEngine::NEXUSKEY_EXTRA_INFO;
        }

        // Shift up
        inputs[inputCount - 1].type = INPUT_KEYBOARD;
        inputs[inputCount - 1].ki.wVk = VK_SHIFT;
        inputs[inputCount - 1].ki.dwFlags = KEYEVENTF_KEYUP;
        inputs[inputCount - 1].ki.dwExtraInfo = HookEngine::NEXUSKEY_EXTRA_INFO;

        SendInput(static_cast<UINT>(inputCount), inputs.data(), sizeof(INPUT));
        sent += batchSize;
    }
}

// ═══════════════════════════════════════════════════════════
// Conversion logic
// ═══════════════════════════════════════════════════════════

std::wstring QuickConvert::ApplyConversion(const std::wstring& input, int optionIndex) const {
    switch (optionIndex) {
        case kEncoding: {
            auto srcTable = static_cast<CodeTable>(config_.sourceEncoding);
            auto dstTable = static_cast<CodeTable>(config_.destEncoding);
            std::wstring unicode = CodeTableConverter::DecodeString(input, srcTable);
            return CodeTableConverter::EncodeString(unicode, dstTable);
        }
        case kToUpper:
            return CodeTableConverter::ToUpper(input);
        case kToLower:
            return CodeTableConverter::ToLower(input);
        case kCapsFirst:
            return CodeTableConverter::CapitalizeFirstOfSentence(input);
        case kCapsEach:
            return CodeTableConverter::CapitalizeEachWord(input);
        case kRemoveDiacritics:
            return CodeTableConverter::RemoveDiacritics(input);
        default:
            return input;
    }
}

const wchar_t* QuickConvert::GetOptionName(int optionIndex) {
    switch (optionIndex) {
        case kEncoding:        return L"\x2192 Chuy\x1EC3n m\x00E3";              // → Chuyển mã
        case kToUpper:         return L"\x2192 ch\x1EEF HOA";                     // → chữ HOA
        case kToLower:         return L"\x2192 ch\x1EEF th\x01B0\x1EDD" L"ng";   // → chữ thường
        case kCapsFirst:       return L"\x2192 Hoa \x0111\x1EA7u c\x00E2u";      // → Hoa đầu câu
        case kCapsEach:        return L"\x2192 Hoa T\x1EEB" L"ng Ch\x1EEF";      // → Hoa Từng Chữ
        case kRemoveDiacritics:return L"\x2192 B\x1ECF d\x1EA5u";                // → Bỏ dấu
        default:               return L"\x2192 Chuy\x1EC3" L"n m\x00E3 xong";    // → Chuyển mã xong
    }
}

std::vector<int> QuickConvert::GetEnabledOptions() const {
    std::vector<int> options;

    if (config_.sourceEncoding != config_.destEncoding) {
        options.push_back(kEncoding);
    }
    if (config_.allCaps) {
        options.push_back(kToUpper);
    }
    if (config_.allLower) {
        options.push_back(kToLower);
    }
    if (config_.capsFirst) {
        options.push_back(kCapsFirst);
    }
    if (config_.capsEach) {
        options.push_back(kCapsEach);
    }
    if (config_.removeMark) {
        options.push_back(kRemoveDiacritics);
    }

    return options;
}

// ═══════════════════════════════════════════════════════════
// Sequential state management
// ═══════════════════════════════════════════════════════════

bool QuickConvert::IsNewSelection(const std::wstring& clipText) const {
    HWND currentWindow = GetForegroundWindow();
    DWORD now = GetTickCount();

    // New if: different window, timed out, or content doesn't match expected
    if (currentWindow != seqState_.window) return true;
    if ((now - seqState_.lastConvertTime) > SEQUENTIAL_TIMEOUT_MS) return true;
    if (seqState_.originText.empty()) return true;

    // Check if clipText matches what we last wrote (continuing cycle)
    // or matches the original (user re-selected)
    size_t clipHash = std::hash<std::wstring>{}(clipText);
    if (clipHash == seqState_.contentHash) return false;  // Matches our last output
    if (clipText == seqState_.originText) return false;    // Matches original

    return true;  // Different text = new selection
}

void QuickConvert::ResetSequentialState() {
    seqState_ = SequentialState{};
}

}  // namespace NextKey
