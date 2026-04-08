// NexusKey Classic — Settings Dialog
// Compact (Unikey-style) + Advanced (EVKey-style) modes
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "ClassicTheme.h"
#include "resource.h"
#include "core/config/TypingConfig.h"
#include "core/config/SettingMetadata.h"
#include "core/SystemConfig.h"
#include "core/UIConfig.h"
#include "core/ipc/SharedStateManager.h"
#include "core/config/ConfigEvent.h"
#include <Windows.h>
#include <commctrl.h>
#include <string>

namespace NextKey::Classic {

/// Win32 settings dialog with two layout modes:
/// - Compact (320x280): dropdowns + "Mo rong" checkbox + buttons (Unikey-like)
/// - Advanced (500x500): compact top + tabbed 2-column checkboxes (EVKey-like)
///
/// Reads/writes settings via SettingMetadata mapping table + offsetof.
/// Syncs to SharedState immediately; deferred TOML save via 30s timer.
class ClassicSettingsDialog {
public:
    ClassicSettingsDialog() = default;
    ~ClassicSettingsDialog();

    // Non-copyable
    ClassicSettingsDialog(const ClassicSettingsDialog&) = delete;
    ClassicSettingsDialog& operator=(const ClassicSettingsDialog&) = delete;

    /// Create and show the dialog. Runs a modal message loop.
    /// Returns true if the dialog was shown successfully.
    bool Show(HINSTANCE hInstance, HWND parent = nullptr);

    [[nodiscard]] HWND GetHwnd() const noexcept { return hwnd_; }

private:
    // ── Window setup ──
    bool RegisterWindowClass(HINSTANCE hInstance);
    void CreateCompactControls();
    void CreateAdvancedControls();
    void ToggleAdvancedMode(bool expand);

    // ── Settings I/O ──
    void LoadSettings();
    void SaveSettings();
    void SyncToSharedState();
    void SaveToToml();
    void PopulateControls();
    void ReadControlValues();
    void ResetToDefaults();

    // ── Event handlers ──
    void OnCommand(WPARAM wParam, LPARAM lParam);
    void OnTabChange();
    void ShowTabPage(int tabIndex);

    // ── Helpers ──
    HWND CreateLabel(const wchar_t* text, int x, int y, int w, int h, UINT id);
    HWND CreateCombo(int x, int y, int w, int h, UINT id);
    HWND CreateCheck(const wchar_t* text, int x, int y, int w, int h, UINT id);
    HWND CreateBtn(const wchar_t* text, int x, int y, int w, int h, UINT id, bool ownerDraw = true);
    void SetFontOnAllChildren();
    static BOOL CALLBACK SetFontProc(HWND hwnd, LPARAM lParam);
    int Dpi(int value) const noexcept;

    static LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);

    // ── Layout constants (pixels at 96 DPI, scaled by Dpi()) ──
    static constexpr int kCompactWidth  = 320;
    static constexpr int kCompactHeight = 280;
    static constexpr int kAdvancedWidth  = 500;
    static constexpr int kAdvancedHeight = 500;
    static constexpr int kPadding       = 16;
    static constexpr int kControlHeight = 22;
    static constexpr int kComboHeight   = 24;
    static constexpr int kButtonHeight  = 30;
    static constexpr int kRowGap        = 6;
    static constexpr int kLabelHeight   = 16;
    static constexpr int kTabHeight     = 28;

    // ── State ──
    HWND hwnd_ = nullptr;
    HINSTANCE hInstance_ = nullptr;
    ClassicTheme theme_;
    UINT dpi_ = 96;

    // Compact controls
    HWND comboMethod_   = nullptr;
    HWND comboEncoding_ = nullptr;
    HWND checkExpand_   = nullptr;
    HWND btnClose_      = nullptr;
    HWND btnExit_       = nullptr;

    // Advanced controls
    HWND tabControl_    = nullptr;
    HWND btnDefaults_   = nullptr;
    HWND btnSave_       = nullptr;
    bool isAdvanced_    = false;
    bool advancedCreated_ = false;
    int currentTab_     = 0;

    // Per-tab checkbox HWNDs (indexed by kSettings array index)
    // We store them so ShowTabPage can show/hide the right ones.
    static constexpr size_t kMaxControls = 64;
    HWND checkControls_[kMaxControls] = {};

    // Settings data
    TypingConfig config_{};
    HotkeyConfig hotkeyConfig_{};
    SystemConfig systemConfig_{};
    SharedStateManager sharedState_;
    ConfigEvent configEvent_;
    bool configDirty_ = false;

    static constexpr UINT_PTR kTimerDeferredSave = 1001;
    static constexpr DWORD kDeferredSaveDelayMs  = 30000;  // 30 seconds

    static constexpr const wchar_t* kClassName = L"NexusKeyClassicSettings";
};

} // namespace NextKey::Classic
