// NexusKey Classic — Convert Tool Dialog
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#ifdef _WIN32
#include "ClassicTheme.h"
#include "core/config/TypingConfig.h"
#include <Windows.h>
#include <commctrl.h>

namespace NextKey::Classic {

/// Win32 native dialog for text conversion tool settings.
/// Configures: case conversion toggles, encoding dropdowns, hotkey, and execute.
class ClassicConvertToolDialog {
public:
    static bool Show(HINSTANCE hInstance, HWND parent);

private:
    ClassicConvertToolDialog() = default;

    bool Init(HINSTANCE hInstance, HWND parent);
    void CreateControls();
    void PopulateFromConfig();
    void ReadToConfig();
    void SaveConfig();
    void DoConvert();

    static LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
    int Dpi(int value) const noexcept;

    static constexpr int kWidth = 400;
    static constexpr int kHeight = 440;
    static constexpr int kPadding = 12;
    static constexpr int kRowH = 24;
    static constexpr int kRowGap = 4;
    static constexpr int kBtnHeight = 28;

    HWND hwnd_ = nullptr;
    HINSTANCE hInstance_ = nullptr;
    ClassicTheme theme_;
    UINT dpi_ = 96;
    bool modified_ = false;

    // Case conversion toggles
    HWND checkAllCaps_ = nullptr;
    HWND checkAllLower_ = nullptr;
    HWND checkCapsFirst_ = nullptr;
    HWND checkCapsEach_ = nullptr;
    HWND checkRemoveMark_ = nullptr;

    // Options
    HWND checkAlertDone_ = nullptr;
    HWND checkAutoPaste_ = nullptr;
    HWND checkSequential_ = nullptr;

    // Encoding
    HWND comboSource_ = nullptr;
    HWND comboDest_ = nullptr;

    // Hotkey
    HWND checkHkCtrl_ = nullptr;
    HWND checkHkAlt_ = nullptr;
    HWND checkHkShift_ = nullptr;
    HWND checkHkWin_ = nullptr;
    HWND editHkKey_ = nullptr;

    // Buttons
    HWND btnConvert_ = nullptr;
    HWND btnClose_ = nullptr;

    ConvertConfig config_{};

    static constexpr const wchar_t* kClassName = L"NexusKeyConvertTool";
};

}  // namespace NextKey::Classic

#endif
