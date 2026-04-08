// NexusKey Classic — Material Design 3 Theme Engine
// SPDX-License-Identifier: GPL-3.0-only

#include "ClassicTheme.h"
#include <CommCtrl.h>
#include <uxtheme.h>
#include <VersionHelpers.h>

#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "uxtheme.lib")

// Undocumented uxtheme.dll APIs for dark mode (Win10 1903+)
enum class PreferredAppMode { Default = 0, AllowDark = 1, ForceDark = 2, ForceLight = 3 };
using fnSetPreferredAppMode = PreferredAppMode(WINAPI*)(PreferredAppMode);
using fnAllowDarkModeForWindow = bool(WINAPI*)(HWND, bool);
using fnRefreshImmersiveColorPolicyState = void(WINAPI*)();

namespace NextKey::Classic {

// -- IsWindows11OrGreater helper --
static bool IsWindows11OrGreater() {
    // Windows 11 = build >= 22000
    OSVERSIONINFOEXW osvi = { sizeof(osvi) };
    osvi.dwBuildNumber = 22000;
    DWORDLONG mask = 0;
    VER_SET_CONDITION(mask, VER_BUILDNUMBER, VER_GREATER_EQUAL);
    return VerifyVersionInfoW(&osvi, VER_BUILDNUMBER, mask) != FALSE;
}

// -- M3 Color Palettes (Indigo #5C6BC0 seed) --

static constexpr ThemeColors kLightColors = {
    .background       = RGB(255, 251, 254),  // #FFFBFE
    .surface          = RGB(255, 251, 254),
    .surfaceVariant   = RGB(228, 225, 236),  // #E4E1EC
    .surfaceTonal1    = RGB(242, 239, 254),  // #F2EFFE
    .primary          = RGB( 92, 107, 192),  // #5C6BC0
    .onPrimary        = RGB(255, 255, 255),
    .primaryContainer = RGB(225, 226, 255),  // #E1E2FF
    .onSurface        = RGB( 27,  27,  31),  // #1B1B1F
    .onSurfaceVariant = RGB( 70,  70,  79),  // #46464F
    .outline          = RGB(119, 118, 128),  // #777680
    .outlineVariant   = RGB(200, 198, 207),  // #C8C6CF
    .error            = RGB(186,  26,  26),  // #BA1A1A
    .onError          = RGB(255, 255, 255),
};

static constexpr ThemeColors kDarkColors = {
    .background       = RGB( 27,  27,  31),  // #1B1B1F
    .surface          = RGB( 27,  27,  31),
    .surfaceVariant   = RGB( 70,  70,  79),  // #46464F
    .surfaceTonal1    = RGB( 33,  33,  47),  // #21212F
    .primary          = RGB(190, 194, 255),  // #BEC2FF
    .onPrimary        = RGB( 38,  42, 112),  // #262A70
    .primaryContainer = RGB( 62,  67, 147),  // #3E4393
    .onSurface        = RGB(229, 225, 230),  // #E5E1E6
    .onSurfaceVariant = RGB(200, 198, 207),  // #C8C6CF
    .outline          = RGB(145, 143, 154),  // #918F9A
    .outlineVariant   = RGB( 70,  70,  79),  // #46464F
    .error            = RGB(255, 180, 171),  // #FFB4AB
    .onError          = RGB(105,   0,   5),  // #690005
};

// -- Lifecycle --

ClassicTheme::~ClassicTheme() {
    Destroy();
}

void ClassicTheme::Init(HWND hwnd) {
    hwnd_ = hwnd;
    DetectDarkMode();
    colors_ = isDark_ ? kDarkColors : kLightColors;

    // Enable dark mode at app level (affects context menus, scrollbars, etc.)
    {
        HMODULE hUxTheme = GetModuleHandleW(L"uxtheme.dll");
        if (hUxTheme) {
            auto setMode = reinterpret_cast<fnSetPreferredAppMode>(
                GetProcAddress(hUxTheme, MAKEINTRESOURCEA(135)));
            if (setMode) setMode(isDark_ ? PreferredAppMode::AllowDark : PreferredAppMode::Default);

            auto refresh = reinterpret_cast<fnRefreshImmersiveColorPolicyState>(
                GetProcAddress(hUxTheme, MAKEINTRESOURCEA(104)));
            if (refresh) refresh();
        }
    }

    UINT dpi = 96;
    // GetDpiForWindow requires Win10 1607+
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

bool ClassicTheme::OnSettingChange(LPARAM lParam) {
    if (lParam && wcscmp(reinterpret_cast<LPCWSTR>(lParam), L"ImmersiveColorSet") == 0) {
        bool wasDark = isDark_;
        DetectDarkMode();
        if (wasDark != isDark_) {
            colors_ = isDark_ ? kDarkColors : kLightColors;
            DestroyBrushes();
            CreateBrushes();
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

    // Win11: rounded corners + caption color
    if (IsWindows11OrGreater()) {
        auto corner = 2; // DWMWCP_ROUND
        DwmSetWindowAttribute(hwnd, 33 /*DWMWA_WINDOW_CORNER_PREFERENCE*/, &corner, sizeof(corner));

        COLORREF captionColor = isDark_ ? RGB(33, 33, 47) : RGB(242, 239, 254);
        DwmSetWindowAttribute(hwnd, 35 /*DWMWA_CAPTION_COLOR*/, &captionColor, sizeof(captionColor));
    }
}

void ClassicTheme::ThemeChildControl(HWND hwndCtrl) {
    if (!hwndCtrl) return;

    // DWM dark title bar for child windows
    BOOL darkBool = isDark_ ? TRUE : FALSE;
    DwmSetWindowAttribute(hwndCtrl, 20 /*DWMWA_USE_IMMERSIVE_DARK_MODE*/, &darkBool, sizeof(darkBool));

    // Undocumented: AllowDarkModeForWindow (ordinal 133)
    HMODULE hUxTheme = GetModuleHandleW(L"uxtheme.dll");
    if (hUxTheme) {
        auto allow = reinterpret_cast<fnAllowDarkModeForWindow>(
            GetProcAddress(hUxTheme, MAKEINTRESOURCEA(133)));
        if (allow) allow(hwndCtrl, isDark_);
    }

    // "DarkMode_Explorer" theme makes combobox, checkbox, scrollbar, etc. render dark
    SetWindowTheme(hwndCtrl, isDark_ ? L"DarkMode_Explorer" : L"Explorer", nullptr);
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
    brBackground_     = CreateSolidBrush(colors_.background);
    brSurface_        = CreateSolidBrush(colors_.surface);
    brSurfaceVariant_ = CreateSolidBrush(colors_.surfaceVariant);
    brPrimary_        = CreateSolidBrush(colors_.primary);
    brOutlineVariant_ = CreateSolidBrush(colors_.outlineVariant);
}

void ClassicTheme::DestroyBrushes() {
    auto del = [](HBRUSH& br) { if (br) { DeleteObject(br); br = nullptr; } };
    del(brBackground_); del(brSurface_); del(brSurfaceVariant_);
    del(brPrimary_); del(brOutlineVariant_);
}

// -- Fonts --

void ClassicTheme::CreateFonts(UINT dpi) {
    auto make = [&](int pt, int weight, const wchar_t* face) -> HFONT {
        int height = -MulDiv(pt, static_cast<int>(dpi), 72);
        return CreateFontW(height, 0, 0, 0, weight, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, face);
    };
    fonts_.header  = make(11, FW_SEMIBOLD, L"Segoe UI Variable Display");
    fonts_.body    = make(9,  FW_NORMAL,   L"Segoe UI Variable Text");
    fonts_.caption = make(8,  FW_NORMAL,   L"Segoe UI Variable Text");
    fonts_.label   = make(9,  FW_MEDIUM,   L"Segoe UI Variable Text");

    // Fallback: if Segoe UI Variable not available (Win10 pre-21H2)
    if (!fonts_.body) {
        fonts_.header  = make(11, FW_SEMIBOLD, L"Segoe UI");
        fonts_.body    = make(9,  FW_NORMAL,   L"Segoe UI");
        fonts_.caption = make(8,  FW_NORMAL,   L"Segoe UI");
        fonts_.label   = make(9,  FW_MEDIUM,   L"Segoe UI");
    }
}

void ClassicTheme::DestroyFonts() {
    auto del = [](HFONT& f) { if (f) { DeleteObject(f); f = nullptr; } };
    del(fonts_.header); del(fonts_.body); del(fonts_.caption); del(fonts_.label);
}

// -- WM_CTLCOLOR Handlers --

HBRUSH ClassicTheme::OnCtlColorDlg(HDC) {
    return brBackground_;
}

HBRUSH ClassicTheme::OnCtlColorStatic(HDC hdc, HWND) {
    SetTextColor(hdc, colors_.onSurface);
    SetBkMode(hdc, TRANSPARENT);
    SelectObject(hdc, fonts_.body);
    return brBackground_;
}

HBRUSH ClassicTheme::OnCtlColorBtn(HDC hdc, HWND) {
    SetTextColor(hdc, colors_.onSurface);
    SetBkMode(hdc, TRANSPARENT);
    return brBackground_;
}

HBRUSH ClassicTheme::OnCtlColorEdit(HDC hdc, HWND) {
    SetTextColor(hdc, colors_.onSurface);
    SetBkColor(hdc, colors_.surfaceVariant);
    return brSurfaceVariant_;
}

HBRUSH ClassicTheme::OnCtlColorListBox(HDC hdc, HWND) {
    SetTextColor(hdc, colors_.onSurface);
    SetBkColor(hdc, colors_.surface);
    return brSurface_;
}

// -- Owner-Draw: Button --

void ClassicTheme::DrawButton(DRAWITEMSTRUCT* dis, bool isPrimary) {
    HDC hdc = dis->hDC;
    RECT rc = dis->rcItem;
    bool pressed = (dis->itemState & ODS_SELECTED) != 0;
    bool disabled = (dis->itemState & ODS_DISABLED) != 0;
    bool focused = (dis->itemState & ODS_FOCUS) != 0;

    // Background
    COLORREF bg;
    if (isPrimary) {
        bg = disabled ? BlendColors(colors_.onSurface, colors_.background, 31)
                      : pressed ? BlendColors(colors_.primary, colors_.onPrimary, 31)
                                : colors_.primary;
    } else {
        bg = pressed ? BlendColors(colors_.surface, colors_.onSurface, 31)
                     : colors_.surface;
    }

    HBRUSH br = CreateSolidBrush(bg);
    FillRect(hdc, &rc, br);
    DeleteObject(br);

    // Border (outlined button only)
    if (!isPrimary) {
        HPEN pen = CreatePen(PS_SOLID, 1, focused ? colors_.primary : colors_.outline);
        HPEN old = static_cast<HPEN>(SelectObject(hdc, pen));
        HBRUSH null = static_cast<HBRUSH>(GetStockObject(NULL_BRUSH));
        HBRUSH oldBr = static_cast<HBRUSH>(SelectObject(hdc, null));
        Rectangle(hdc, rc.left, rc.top, rc.right, rc.bottom);
        SelectObject(hdc, old);
        SelectObject(hdc, oldBr);
        DeleteObject(pen);
    }

    // Text
    wchar_t text[128]{};
    GetWindowTextW(dis->hwndItem, text, 128);
    SetTextColor(hdc, disabled ? BlendColors(colors_.onSurface, colors_.background, 97)
                               : isPrimary ? colors_.onPrimary : colors_.primary);
    SetBkMode(hdc, TRANSPARENT);
    SelectObject(hdc, fonts_.label);
    DrawTextW(hdc, text, -1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
}

// -- Owner-Draw: Checkbox --

void ClassicTheme::DrawCheckbox(DRAWITEMSTRUCT* dis) {
    HDC hdc = dis->hDC;
    RECT rc = dis->rcItem;
    // For owner-draw checkboxes, state is managed manually via GWLP_USERDATA
    bool checked = (GetWindowLongPtrW(dis->hwndItem, GWLP_USERDATA) != 0);

    // Fill background
    FillRect(hdc, &rc, brBackground_);

    // Checkbox box: 16x16, vertically centered
    constexpr int boxSize = 16;
    int y = rc.top + (rc.bottom - rc.top - boxSize) / 2;
    RECT box = { rc.left, y, rc.left + boxSize, y + boxSize };

    // Box fill
    COLORREF fillColor = checked ? colors_.primary : colors_.surface;
    HBRUSH fillBr = CreateSolidBrush(fillColor);
    FillRect(hdc, &box, fillBr);
    DeleteObject(fillBr);

    // Box border
    COLORREF borderColor = checked ? colors_.primary : colors_.outline;
    HPEN pen = CreatePen(PS_SOLID, 1, borderColor);
    HPEN oldPen = static_cast<HPEN>(SelectObject(hdc, pen));
    HBRUSH nullBr = static_cast<HBRUSH>(SelectObject(hdc, GetStockObject(NULL_BRUSH)));
    Rectangle(hdc, box.left, box.top, box.right, box.bottom);
    SelectObject(hdc, oldPen);
    SelectObject(hdc, nullBr);
    DeleteObject(pen);

    // Checkmark (when checked)
    if (checked) {
        HPEN ckPen = CreatePen(PS_SOLID, 2, colors_.onPrimary);
        HPEN oldCk = static_cast<HPEN>(SelectObject(hdc, ckPen));
        POINT pts[] = {
            { box.left + 3, box.top + 8 },
            { box.left + 6, box.top + 11 },
            { box.left + 12, box.top + 4 },
        };
        Polyline(hdc, pts, 3);
        SelectObject(hdc, oldCk);
        DeleteObject(ckPen);
    }

    // Label text (to the right of checkbox)
    RECT textRc = { rc.left + boxSize + 8, rc.top, rc.right, rc.bottom };
    wchar_t label[256]{};
    GetWindowTextW(dis->hwndItem, label, 256);
    SetTextColor(hdc, colors_.onSurface);
    SetBkMode(hdc, TRANSPARENT);
    SelectObject(hdc, fonts_.body);
    DrawTextW(hdc, label, -1, &textRc, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
}

// -- Owner-Draw: Tab Item --

void ClassicTheme::DrawTabItem(DRAWITEMSTRUCT* dis) {
    HDC hdc = dis->hDC;
    RECT rc = dis->rcItem;
    bool selected = (dis->itemState & ODS_SELECTED) != 0;

    // Background
    COLORREF bg = selected ? colors_.surface : colors_.background;
    HBRUSH br = CreateSolidBrush(bg);
    FillRect(hdc, &rc, br);
    DeleteObject(br);

    // Active indicator: 3px line at bottom
    if (selected) {
        RECT indicator = { rc.left + 8, rc.bottom - 3, rc.right - 8, rc.bottom };
        HBRUSH indBr = CreateSolidBrush(colors_.primary);
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

    SetTextColor(hdc, selected ? colors_.primary : colors_.onSurfaceVariant);
    SetBkMode(hdc, TRANSPARENT);
    SelectObject(hdc, selected ? fonts_.label : fonts_.body);
    DrawTextW(hdc, text, -1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
}

// -- Divider --

void ClassicTheme::DrawDivider(HDC hdc, int x, int y, int width) {
    RECT line = { x, y, x + width, y + 1 };
    HBRUSH br = CreateSolidBrush(colors_.outlineVariant);
    FillRect(hdc, &line, br);
    DeleteObject(br);
}

// -- Utility --

COLORREF ClassicTheme::BlendColors(COLORREF base, COLORREF overlay, BYTE alpha) {
    auto blend = [](BYTE b, BYTE o, BYTE a) -> BYTE {
        return static_cast<BYTE>((b * (255 - a) + o * a) / 255);
    };
    return RGB(
        blend(GetRValue(base), GetRValue(overlay), alpha),
        blend(GetGValue(base), GetGValue(overlay), alpha),
        blend(GetBValue(base), GetBValue(overlay), alpha)
    );
}

} // namespace NextKey::Classic
