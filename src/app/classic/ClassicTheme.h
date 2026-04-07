// NexusKey Classic — Material Design 3 Theme Engine
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <Windows.h>
#include <dwmapi.h>
#include <cstdint>

namespace NextKey::Classic {

/// M3 color tokens (light and dark schemes)
struct ThemeColors {
    COLORREF background;        // Window background
    COLORREF surface;           // Card/panel surface
    COLORREF surfaceVariant;    // Input background, list bg
    COLORREF surfaceTonal1;     // Titlebar, elevated surfaces
    COLORREF primary;           // Accent: active state, buttons
    COLORREF onPrimary;         // Text on primary fill
    COLORREF primaryContainer;  // Chip bg, selected state
    COLORREF onSurface;         // Primary text
    COLORREF onSurfaceVariant;  // Secondary text, icons
    COLORREF outline;           // Input border, control border
    COLORREF outlineVariant;    // Subtle divider
    COLORREF error;             // Error state
    COLORREF onError;           // Text on error
};

/// Font handles (created once per DPI change)
struct FontSet {
    HFONT header;     // Segoe UI Variable Display, 11pt, SemiBold
    HFONT body;       // Segoe UI Variable Text, 9pt, Regular
    HFONT caption;    // Segoe UI Variable Text, 8pt, Regular
    HFONT label;      // Segoe UI Variable Text, 9pt, Medium (buttons)
};

/// Material Design 3 theme manager for Win32
class ClassicTheme {
public:
    ClassicTheme() = default;
    ~ClassicTheme();

    // Non-copyable
    ClassicTheme(const ClassicTheme&) = delete;
    ClassicTheme& operator=(const ClassicTheme&) = delete;

    /// Initialize theme (detect dark mode, create fonts/brushes)
    void Init(HWND hwnd);

    /// Clean up GDI resources
    void Destroy();

    /// Handle WM_SETTINGCHANGE to detect theme switch
    /// Returns true if theme actually changed (caller should InvalidateRect)
    bool OnSettingChange(LPARAM lParam);

    // -- WM_CTLCOLOR handlers (return HBRUSH) --
    HBRUSH OnCtlColorDlg(HDC hdc);
    HBRUSH OnCtlColorStatic(HDC hdc, HWND hCtrl);
    HBRUSH OnCtlColorBtn(HDC hdc, HWND hCtrl);
    HBRUSH OnCtlColorEdit(HDC hdc, HWND hCtrl);
    HBRUSH OnCtlColorListBox(HDC hdc, HWND hCtrl);

    /// Owner-draw button (WM_DRAWITEM for BS_OWNERDRAW buttons)
    void DrawButton(DRAWITEMSTRUCT* dis, bool isPrimary);

    /// Owner-draw checkbox (WM_DRAWITEM for BS_OWNERDRAW checkboxes)
    void DrawCheckbox(DRAWITEMSTRUCT* dis);

    /// Draw tab control item (TCS_OWNERDRAWFIXED)
    void DrawTabItem(DRAWITEMSTRUCT* dis);

    /// Draw 1px horizontal divider
    void DrawDivider(HDC hdc, int x, int y, int width);

    /// Apply dark mode to window caption (DWM)
    void ApplyWindowAttributes(HWND hwnd);

    // -- State layer helpers --
    static COLORREF BlendColors(COLORREF base, COLORREF overlay, BYTE alpha);

    // -- Accessors --
    [[nodiscard]] bool IsDark() const noexcept { return isDark_; }
    [[nodiscard]] const ThemeColors& Colors() const noexcept { return colors_; }
    [[nodiscard]] const FontSet& Fonts() const noexcept { return fonts_; }
    [[nodiscard]] HBRUSH BrushBackground() const noexcept { return brBackground_; }
    [[nodiscard]] HBRUSH BrushSurface() const noexcept { return brSurface_; }

private:
    void DetectDarkMode();
    void CreateBrushes();
    void DestroyBrushes();
    void CreateFonts(UINT dpi);
    void DestroyFonts();

    bool isDark_ = false;
    ThemeColors colors_{};
    FontSet fonts_{};

    // Cached GDI brushes
    HBRUSH brBackground_ = nullptr;
    HBRUSH brSurface_ = nullptr;
    HBRUSH brSurfaceVariant_ = nullptr;
    HBRUSH brPrimary_ = nullptr;
    HBRUSH brOutlineVariant_ = nullptr;

    HWND hwnd_ = nullptr;
};

} // namespace NextKey::Classic
