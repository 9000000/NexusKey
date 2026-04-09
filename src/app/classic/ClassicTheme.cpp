// NexusKey Classic — Native Windows Theme Engine
// SPDX-License-Identifier: GPL-3.0-only

#include "ClassicTheme.h"
#include "DarkModeHelper.h"
#include <CommCtrl.h>
#include <uxtheme.h>

#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "uxtheme.lib")

// Undocumented uxtheme.dll APIs for dark mode (Win10 1903+)
using fnAllowDarkModeForWindow = bool(WINAPI*)(HWND, bool);

namespace NextKey::Classic {

// -- IsWindows11OrGreater helper --
static bool IsWindows11OrGreater() {
    OSVERSIONINFOEXW osvi = { sizeof(osvi) };
    osvi.dwBuildNumber = 22000;
    DWORDLONG mask = 0;
    VER_SET_CONDITION(mask, VER_BUILDNUMBER, VER_GREATER_EQUAL);
    return VerifyVersionInfoW(&osvi, VER_BUILDNUMBER, mask) != FALSE;
}

// -- Lifecycle --

ClassicTheme::~ClassicTheme() {
    Destroy();
}

void ClassicTheme::Init(HWND hwnd) {
    hwnd_ = hwnd;
    DetectDarkMode();
    RefreshColors();

    // Enable dark mode at app level (affects native combobox, scrollbar, etc.)
    DarkModeHelper::ApplyDarkModeForApp();

    UINT dpi = 96;
    auto pfn = reinterpret_cast<UINT(WINAPI*)(HWND)>(
        GetProcAddress(GetModuleHandleW(L"user32.dll"), "GetDpiForWindow"));
    if (pfn) dpi = pfn(hwnd);

    CreateFonts(dpi);
    CreateBrushes();
    ApplyWindowAttributes(hwnd);
}

void ClassicTheme::Destroy() {
    DestroyBrushes();
    DestroyFonts();
}

void ClassicTheme::DetectDarkMode() {
    DWORD value = 1;  // default light
    DWORD size = sizeof(value);
    RegGetValueW(HKEY_CURRENT_USER,
        L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
        L"AppsUseLightTheme", RRF_RT_REG_DWORD, nullptr, &value, &size);
    isDark_ = (value == 0);
}

void ClassicTheme::RefreshColors() {
    // GetSysColor() does NOT change in dark mode for Win32 apps — only UWP/WinUI
    // gets automatic dark system colors. So we use system colors for light mode
    // and provide our own neutral dark palette.
    if (isDark_) {
        colors_.background    = RGB( 30,  30,  30);  // #1E1E1E
        colors_.surface       = RGB( 45,  45,  45);  // #2D2D2D
        colors_.text          = RGB(255, 255, 255);   // #FFFFFF
        colors_.textSecondary = RGB(153, 153, 153);   // #999999
        colors_.accent        = GetSysColor(COLOR_HIGHLIGHT);  // system accent works
        colors_.accentText    = GetSysColor(COLOR_HIGHLIGHTTEXT);
        colors_.border        = RGB(105, 105, 105);   // #696969
    } else {
        colors_.background    = GetSysColor(COLOR_WINDOW);
        colors_.surface       = GetSysColor(COLOR_BTNFACE);
        colors_.text          = GetSysColor(COLOR_WINDOWTEXT);
        colors_.textSecondary = GetSysColor(COLOR_GRAYTEXT);
        colors_.accent        = GetSysColor(COLOR_HIGHLIGHT);
        colors_.accentText    = GetSysColor(COLOR_HIGHLIGHTTEXT);
        colors_.border        = GetSysColor(COLOR_BTNSHADOW);
    }
}

bool ClassicTheme::OnSettingChange(LPARAM lParam) {
    if (lParam && wcscmp(reinterpret_cast<LPCWSTR>(lParam), L"ImmersiveColorSet") == 0) {
        bool wasDark = isDark_;
        DetectDarkMode();
        if (wasDark != isDark_) {
            RefreshColors();
            DestroyBrushes();
            CreateBrushes();
            DarkModeHelper::ApplyDarkModeForApp();
            ApplyWindowAttributes(hwnd_);
            return true;
        }
    }
    return false;
}

void ClassicTheme::ApplyWindowAttributes(HWND hwnd) {
    // Dark mode caption bar (Win10 build 18985+)
    BOOL darkBool = isDark_ ? TRUE : FALSE;
    DwmSetWindowAttribute(hwnd, 20 /*DWMWA_USE_IMMERSIVE_DARK_MODE*/, &darkBool, sizeof(darkBool));

    // Win11: rounded corners (no custom caption color — let Windows decide)
    if (IsWindows11OrGreater()) {
        auto corner = 2; // DWMWCP_ROUND
        DwmSetWindowAttribute(hwnd, 33 /*DWMWA_WINDOW_CORNER_PREFERENCE*/, &corner, sizeof(corner));
    }
}

// Subclass to prevent the white flash during the slide-down animation of ComboLBox
static LRESULT CALLBACK DarkListSubclassProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam, UINT_PTR /*uIdSubclass*/, DWORD_PTR dwRefData) {
    auto* theme = reinterpret_cast<ClassicTheme*>(dwRefData);
    if ((msg == WM_ERASEBKGND || msg == WM_PRINTCLIENT) && theme && theme->IsDark()) {
        HDC hdc = reinterpret_cast<HDC>(wParam);
        RECT rc;
        GetClientRect(hWnd, &rc);
        FillRect(hdc, &rc, theme->BrushBackground());
        if (msg == WM_ERASEBKGND) return 1; // Handled
    }
    return DefSubclassProc(hWnd, msg, wParam, lParam);
}

void ClassicTheme::ThemeChildControl(HWND hwndCtrl) {
    if (!hwndCtrl) return;

    BOOL darkBool = isDark_ ? TRUE : FALSE;
    DwmSetWindowAttribute(hwndCtrl, 20 /*DWMWA_USE_IMMERSIVE_DARK_MODE*/, &darkBool, sizeof(darkBool));

    fnAllowDarkModeForWindow allow = nullptr;
    HMODULE hUxTheme = GetModuleHandleW(L"uxtheme.dll");
    if (hUxTheme) {
        allow = reinterpret_cast<fnAllowDarkModeForWindow>(
            GetProcAddress(hUxTheme, MAKEINTRESOURCEA(133)));
        if (allow) allow(hwndCtrl, isDark_);
    }

    wchar_t className[32] = {0};
    if (GetClassNameW(hwndCtrl, className, 32) && wcscmp(className, L"ComboBox") == 0) {
        SetWindowTheme(hwndCtrl, isDark_ ? L"DarkMode_CFD" : L"Explorer", nullptr);
        COMBOBOXINFO info = { sizeof(COMBOBOXINFO) };
        if (GetComboBoxInfo(hwndCtrl, &info) && info.hwndList) {
            DwmSetWindowAttribute(info.hwndList, 20, &darkBool, sizeof(darkBool));
            if (hUxTheme && allow) allow(info.hwndList, isDark_);
            SetWindowTheme(info.hwndList, isDark_ ? L"DarkMode_CFD" : L"Explorer", nullptr);
            SetClassLongPtrW(info.hwndList, GCLP_HBRBACKGROUND,
                reinterpret_cast<LONG_PTR>(isDark_ ? brBackground_ : GetSysColorBrush(COLOR_WINDOW)));
            
            // Subclass the list to catch the animation background fill
            SetWindowSubclass(info.hwndList, DarkListSubclassProc, 1, reinterpret_cast<DWORD_PTR>(this));
        }
    } else {
        SetWindowTheme(hwndCtrl, isDark_ ? L"DarkMode_Explorer" : L"Explorer", nullptr);
    }
}

void ClassicTheme::ThemeAllChildren(HWND parent) {
    struct Ctx { ClassicTheme* self; };
    Ctx ctx{ this };
    EnumChildWindows(parent, [](HWND child, LPARAM lp) -> BOOL {
        auto* c = reinterpret_cast<Ctx*>(lp);
        c->self->ThemeChildControl(child);
        return TRUE;
    }, reinterpret_cast<LPARAM>(&ctx));
}

// -- Brushes --

void ClassicTheme::CreateBrushes() {
    brBackground_ = CreateSolidBrush(colors_.background);
    brSurface_    = CreateSolidBrush(colors_.surface);
}

void ClassicTheme::DestroyBrushes() {
    auto del = [](HBRUSH& br) { if (br) { DeleteObject(br); br = nullptr; } };
    del(brBackground_); del(brSurface_);
}

// -- Fonts --

void ClassicTheme::CreateFonts(UINT dpi) {
    auto make = [&](int pt, int weight, const wchar_t* face) -> HFONT {
        int height = -MulDiv(pt, static_cast<int>(dpi), 72);
        return CreateFontW(height, 0, 0, 0, weight, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, face);
    };
    fonts_.header = make(11, FW_SEMIBOLD, L"Segoe UI Variable Display");
    fonts_.body   = make(9,  FW_NORMAL,   L"Segoe UI Variable Text");

    // Fallback: if Segoe UI Variable not available (Win10 pre-21H2)
    if (!fonts_.body) {
        fonts_.header = make(11, FW_SEMIBOLD, L"Segoe UI");
        fonts_.body   = make(9,  FW_NORMAL,   L"Segoe UI");
    }
}

void ClassicTheme::DestroyFonts() {
    auto del = [](HFONT& f) { if (f) { DeleteObject(f); f = nullptr; } };
    del(fonts_.header); del(fonts_.body);
}

// -- WM_CTLCOLOR Handlers --

HBRUSH ClassicTheme::OnCtlColorDlg(HDC) {
    return brBackground_;
}

HBRUSH ClassicTheme::OnCtlColorStatic(HDC hdc, HWND) {
    SetTextColor(hdc, colors_.text);
    SetBkMode(hdc, TRANSPARENT);
    SelectObject(hdc, fonts_.body);
    return brBackground_;
}

HBRUSH ClassicTheme::OnCtlColorBtn(HDC hdc, HWND) {
    SetTextColor(hdc, colors_.text);
    SetBkMode(hdc, TRANSPARENT);
    return brBackground_;
}

HBRUSH ClassicTheme::OnCtlColorEdit(HDC hdc, HWND) {
    SetTextColor(hdc, colors_.text);
    SetBkColor(hdc, colors_.surface);
    return brSurface_;
}

HBRUSH ClassicTheme::OnCtlColorListBox(HDC hdc, HWND) {
    SetTextColor(hdc, colors_.text);
    SetBkColor(hdc, colors_.background);
    return brBackground_;
}

// -- Owner-Draw: Tab Item --

void ClassicTheme::DrawTabItem(DRAWITEMSTRUCT* dis) {
    HDC hdc = dis->hDC;
    RECT rc = dis->rcItem;
    bool selected = (dis->itemState & ODS_SELECTED) != 0;

    // Background
    FillRect(hdc, &rc, brSurface_);

    // 2px accent line at bottom for selected tab
    if (selected) {
        RECT indicator = { rc.left + 6, rc.bottom - 2, rc.right - 6, rc.bottom };
        HBRUSH indBr = CreateSolidBrush(colors_.accent);
        FillRect(hdc, &indicator, indBr);
        DeleteObject(indBr);
    }

    // Tab text
    wchar_t text[64]{};
    TCITEMW tci = {};
    tci.mask = TCIF_TEXT;
    tci.pszText = text;
    tci.cchTextMax = 64;
    SendMessageW(dis->hwndItem, TCM_GETITEMW, dis->itemID, reinterpret_cast<LPARAM>(&tci));

    SetTextColor(hdc, selected ? colors_.text : colors_.textSecondary);
    SetBkMode(hdc, TRANSPARENT);
    SelectObject(hdc, selected ? fonts_.header : fonts_.body);
    DrawTextW(hdc, text, -1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
}

// -- Divider --

void ClassicTheme::DrawDivider(HDC hdc, int x, int y, int width) {
    RECT line = { x, y, x + width, y + 1 };
    HBRUSH br = CreateSolidBrush(colors_.border);
    FillRect(hdc, &line, br);
    DeleteObject(br);
}

} // namespace NextKey::Classic
