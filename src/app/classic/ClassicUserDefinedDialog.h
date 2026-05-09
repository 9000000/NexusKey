// NexusKey Classic — User Defined Input Dialog Header
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#ifdef _WIN32
#include "ClassicTheme.h"
#include "ClassicDialogUtils.h"
#include "core/engine/TypingAction.h"
#include <Windows.h>
#include <commctrl.h>
#include <string>
#include <array>

namespace NextKey::Classic {

/// Win32 native dialog for editing user-defined input method keymap.
class ClassicUserDefinedDialog {
public:
    /// Show modal dialog. Returns true if keymap was modified.
    static bool Show(HINSTANCE hInstance, HWND parent, bool forceLightTheme = false);

private:
    ClassicUserDefinedDialog() = default;

    bool Init(HINSTANCE hInstance, HWND parent, bool forceLightTheme);
    void CreateControls();
    void PopulateList();
    void AddKey();
    void DeleteSelected();
    void LoadTemplate(bool telex);
    void ImportFromFile();
    void ExportToFile();

    void LoadData();
    void SaveData();

    static LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
    int Dpi(int value) const noexcept;

    static constexpr int kWidth = 380;
    static constexpr int kHeight = 460;
    static constexpr int kPadding = 12;
    static constexpr int kBtnHeight = 28;
    static constexpr int kBtnGap = 6;

    HWND hwnd_ = nullptr;
    HINSTANCE hInstance_ = nullptr;
    ClassicTheme theme_;
    UINT dpi_ = 96;
    bool modified_ = false;

    HWND listView_ = nullptr;
    HWND editKey_ = nullptr;
    HWND comboAction_ = nullptr;
    HWND btnAdd_ = nullptr;
    HWND btnDelete_ = nullptr;
    HWND btnLoadTelex_ = nullptr;
    HWND btnLoadVni_ = nullptr;
    HWND btnImport_ = nullptr;
    HWND btnExport_ = nullptr;

    std::array<TypingAction, 128> keyMap_;

    static constexpr const wchar_t* kClassName = L"NexusKeyUserDefinedTable";
};

}  // namespace NextKey::Classic

#endif
