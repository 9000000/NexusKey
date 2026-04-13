// NexusKey Classic — Excluded Apps Dialog
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#ifdef _WIN32
#include "ClassicTheme.h"
#include "ClassicDialogUtils.h"
#include <Windows.h>
#include <commctrl.h>
#include <string>
#include <vector>

namespace NextKey::Classic {

/// Win32 native dialog for managing excluded apps list.
/// Modal dialog with ListView + add/delete/import/export + window picker.
class ClassicExcludedAppsDialog {
public:
    /// Show modal dialog. Returns true if list was modified.
    static bool Show(HINSTANCE hInstance, HWND parent, bool forceLightTheme = false);

private:
    ClassicExcludedAppsDialog() = default;

    bool Init(HINSTANCE hInstance, HWND parent, bool forceLightTheme);
    void CreateControls();
    void PopulateList();
    void AddApp(const std::wstring& name);
    void DeleteSelected();
    void ImportFromFile();
    void ExportToFile();
    void OnPickWindow();

    void LoadData();
    void SaveData();

    static LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
    int Dpi(int value) const noexcept;

    // Layout
    static constexpr int kWidth = 420;
    static constexpr int kHeight = 298;
    static constexpr int kPadding = 12;
    static constexpr int kBtnHeight = 28;
    static constexpr int kBtnGap = 6;

    HWND hwnd_ = nullptr;
    HINSTANCE hInstance_ = nullptr;
    ClassicTheme theme_;
    UINT dpi_ = 96;
    bool modified_ = false;

    // Controls
    HWND listView_ = nullptr;
    HWND comboRunning_ = nullptr;
    HWND btnAdd_ = nullptr;
    HWND btnPick_ = nullptr;
    HWND btnDelete_ = nullptr;
    HWND btnImport_ = nullptr;
    HWND btnExport_ = nullptr;

    // State
    std::vector<std::wstring> appList_;
    WindowPicker picker_;

    static constexpr const wchar_t* kClassName = L"NexusKeyExcludedApps";
};

}  // namespace NextKey::Classic

#endif  // _WIN32
