// NexusKey Classic — Icon Color Customization Dialog Implementation
// SPDX-License-Identifier: GPL-3.0-only

#include "ClassicIconColorDialog.h"
#include <commdlg.h>
#include <windowsx.h>

namespace NextKey::Classic {

COLORREF ClassicIconColorDialog::customColors_[16] = {};

enum {
    IDC_SWATCH_V = 3401,
    IDC_SWATCH_E,
    IDC_BTN_SAVE,
    IDC_BTN_CLOSE_DLG,
};

// ════════════════════════════════════════════════════════════

IconColorResult ClassicIconColorDialog::Show(HINSTANCE hInstance, HWND parent,
                                              uint32_t initialV, uint32_t initialE) {
    ClassicIconColorDialog dlg;
    if (!dlg.Init(hInstance, parent, initialV, initialE)) {
        return {initialV, initialE, false};
    }

    MSG msg{};
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        if (!IsDialogMessageW(dlg.hwnd_, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }

    return {dlg.colorV_, dlg.colorE_, dlg.accepted_};
}

bool ClassicIconColorDialog::Init(HINSTANCE hInstance, HWND parent,
                                   uint32_t initV, uint32_t initE) {
    hInstance_ = hInstance;
    colorV_ = initV ? initV : RGB(233, 30, 99);
    colorE_ = initE ? initE : RGB(33, 150, 243);

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WndProc;
    wc.cbWndExtra = sizeof(void*);
    wc.hInstance = hInstance;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.lpszClassName = kClassName;
    wc.hIcon = LoadIconW(hInstance, MAKEINTRESOURCEW(101));
    wc.hIconSm = wc.hIcon;
    RegisterClassExW(&wc);

    DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU;
    hwnd_ = CreateWindowExW(WS_EX_TOPMOST, kClassName, L"Tuỳ chỉnh màu icon",
        style, CW_USEDEFAULT, CW_USEDEFAULT, 200, 150,
        parent, nullptr, hInstance, this);
    if (!hwnd_) return false;

    auto pfn = reinterpret_cast<UINT(WINAPI*)(HWND)>(
        GetProcAddress(GetModuleHandleW(L"user32.dll"), "GetDpiForWindow"));
    dpi_ = pfn ? pfn(hwnd_) : 96;

    int w = Dpi(kWidth), h = Dpi(kHeight);
    RECT rc = {0, 0, w, h};
    AdjustWindowRectEx(&rc, style, FALSE, WS_EX_TOPMOST);
    int aw = rc.right - rc.left, ah = rc.bottom - rc.top;
    int sx = GetSystemMetrics(SM_CXSCREEN), sy = GetSystemMetrics(SM_CYSCREEN);
    SetWindowPos(hwnd_, nullptr, (sx - aw) / 2, (sy - ah) / 2, aw, ah, SWP_NOZORDER);

    theme_.Init(hwnd_);
    theme_.ApplyWindowAttributes(hwnd_);
    CreateControls();

    EnumChildWindows(hwnd_, [](HWND h, LPARAM lp) -> BOOL {
        auto* self = reinterpret_cast<ClassicIconColorDialog*>(lp);
        SendMessageW(h, WM_SETFONT, reinterpret_cast<WPARAM>(self->theme_.Fonts().body), TRUE);
        return TRUE;
    }, reinterpret_cast<LPARAM>(this));
    theme_.ThemeAllChildren(hwnd_);

    ShowWindow(hwnd_, SW_SHOW);
    UpdateWindow(hwnd_);
    return true;
}

// ════════════════════════════════════════════════════════════

void ClassicIconColorDialog::CreateControls() {
    int x = Dpi(kPadding);
    int cw = Dpi(kWidth - kPadding * 2);
    int btnH = Dpi(28);
    int btnW = Dpi(64);

    int lblW = Dpi(50);
    int sw = Dpi(28);
    int prevS = Dpi(32);
    int gapPrev = Dpi(16);
    int gapBtn = Dpi(16);
    int totalBlockW = lblW + sw + gapPrev + prevS + gapBtn + btnW;
    int startX = x + (cw - totalBlockW) / 2;

    int vRowCenter = Dpi(kPadding) + Dpi(20);
    int eRowCenter = vRowCenter + Dpi(44);

    // V row
    CreateWindowExW(0, L"STATIC", L"Màu V:",
        WS_CHILD | WS_VISIBLE | SS_LEFT,
        startX, vRowCenter - Dpi(8), lblW, Dpi(16), hwnd_, nullptr, hInstance_, nullptr);

    btnSwatchV_ = CreateWindowExW(0, L"BUTTON", L"",
        WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
        startX + lblW, vRowCenter - sw / 2, sw, sw, hwnd_,
        reinterpret_cast<HMENU>(IDC_SWATCH_V), hInstance_, nullptr);

    btnSave_ = CreateWindowExW(0, L"BUTTON", L"Lưu",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON,
        startX + lblW + sw + gapPrev + prevS + gapBtn, vRowCenter - btnH / 2, btnW, btnH, hwnd_,
        reinterpret_cast<HMENU>(IDC_BTN_SAVE), hInstance_, nullptr);

    // E row
    CreateWindowExW(0, L"STATIC", L"Màu E:",
        WS_CHILD | WS_VISIBLE | SS_LEFT,
        startX, eRowCenter - Dpi(8), lblW, Dpi(16), hwnd_, nullptr, hInstance_, nullptr);

    btnSwatchE_ = CreateWindowExW(0, L"BUTTON", L"",
        WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
        startX + lblW, eRowCenter - sw / 2, sw, sw, hwnd_,
        reinterpret_cast<HMENU>(IDC_SWATCH_E), hInstance_, nullptr);

    btnClose_ = CreateWindowExW(0, L"BUTTON", L"Đóng",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
        startX + lblW + sw + gapPrev + prevS + gapBtn, eRowCenter - btnH / 2, btnW, btnH, hwnd_,
        reinterpret_cast<HMENU>(IDC_BTN_CLOSE_DLG), hInstance_, nullptr);
}

void ClassicIconColorDialog::PickColor(bool isV) {
    CHOOSECOLORW cc{};
    cc.lStructSize = sizeof(cc);
    cc.hwndOwner = hwnd_;
    cc.rgbResult = isV ? colorV_ : colorE_;
    cc.lpCustColors = customColors_;
    cc.Flags = CC_FULLOPEN | CC_RGBINIT;

    if (ChooseColorW(&cc)) {
        if (isV) colorV_ = cc.rgbResult;
        else colorE_ = cc.rgbResult;

        // Redraw swatches + preview
        InvalidateRect(hwnd_, nullptr, TRUE);
    }
}

void ClassicIconColorDialog::PaintPreview(HDC hdc) {
    int cw = Dpi(kWidth - kPadding * 2);
    int btnW = Dpi(64);
    int lblW = Dpi(50);
    int sw = Dpi(28);
    int prevS = Dpi(32);
    int gapPrev = Dpi(16);
    int gapBtn = Dpi(16);
    int totalBlockW = lblW + sw + gapPrev + prevS + gapBtn + btnW;
    
    int startX = Dpi(kPadding) + (cw - totalBlockW) / 2;
    int previewX = startX + lblW + sw + gapPrev;

    int vRowCenter = Dpi(kPadding) + Dpi(20);
    int eRowCenter = vRowCenter + Dpi(44);

    auto drawIconPreview = [&](int cy, COLORREF color, const wchar_t* letter) {
        // Circle background
        HBRUSH br = CreateSolidBrush(color);
        HBRUSH oldBr = (HBRUSH)SelectObject(hdc, br);
        HPEN oldPen = (HPEN)SelectObject(hdc, GetStockObject(NULL_PEN));
        Ellipse(hdc, previewX, cy - prevS / 2, previewX + prevS, cy + prevS / 2);
        SelectObject(hdc, oldBr);
        SelectObject(hdc, oldPen);
        DeleteObject(br);

        // Letter
        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, RGB(255, 255, 255));
        HFONT hFont = CreateFontW(Dpi(18), 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
        HFONT oldFont = (HFONT)SelectObject(hdc, hFont);
        RECT textRc = {previewX, cy - prevS / 2, previewX + prevS, cy + prevS / 2};
        DrawTextW(hdc, letter, 1, &textRc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        SelectObject(hdc, oldFont);
        DeleteObject(hFont);
    };

    drawIconPreview(vRowCenter, colorV_, L"V");
    drawIconPreview(eRowCenter, colorE_, L"E");
}

int ClassicIconColorDialog::Dpi(int value) const noexcept {
    return MulDiv(value, static_cast<int>(dpi_), 96);
}

// ════════════════════════════════════════════════════════════

LRESULT CALLBACK ClassicIconColorDialog::WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    ClassicIconColorDialog* self = nullptr;

    if (msg == WM_NCCREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
        self = reinterpret_cast<ClassicIconColorDialog*>(cs->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        self->hwnd_ = hwnd;
    } else {
        self = reinterpret_cast<ClassicIconColorDialog*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }

    if (!self) return DefWindowProcW(hwnd, msg, wParam, lParam);

    switch (msg) {
        case WM_COMMAND: {
            UINT id = LOWORD(wParam);
            switch (id) {
                case IDC_SWATCH_V: self->PickColor(true);  return 0;
                case IDC_SWATCH_E: self->PickColor(false); return 0;
                case IDC_BTN_SAVE:
                    self->accepted_ = true;
                    DestroyWindow(hwnd);
                    return 0;
                case IDC_BTN_CLOSE_DLG:
                    DestroyWindow(hwnd);
                    return 0;
            }
            break;
        }

        case WM_DRAWITEM: {
            auto* dis = reinterpret_cast<DRAWITEMSTRUCT*>(lParam);
            UINT ctrlId = static_cast<UINT>(wParam);
            COLORREF color = (ctrlId == IDC_SWATCH_V) ? self->colorV_ : self->colorE_;

            HBRUSH br = CreateSolidBrush(color);
            FillRect(dis->hDC, &dis->rcItem, br);
            DeleteObject(br);

            // Border
            FrameRect(dis->hDC, &dis->rcItem,
                (HBRUSH)GetStockObject(self->theme_.IsDark() ? WHITE_BRUSH : BLACK_BRUSH));
            return TRUE;
        }

        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);
            self->PaintPreview(hdc);
            EndPaint(hwnd, &ps);
            return 0;
        }

        case WM_ERASEBKGND: {
            HDC hdc = reinterpret_cast<HDC>(wParam);
            RECT rc;
            GetClientRect(hwnd, &rc);
            FillRect(hdc, &rc, self->theme_.BrushBackground());
            return 1;
        }

        case WM_CTLCOLORSTATIC:
            return reinterpret_cast<LRESULT>(self->theme_.OnCtlColorStatic(
                reinterpret_cast<HDC>(wParam), reinterpret_cast<HWND>(lParam)));
        case WM_CTLCOLORBTN:
            return reinterpret_cast<LRESULT>(self->theme_.OnCtlColorBtn(
                reinterpret_cast<HDC>(wParam), reinterpret_cast<HWND>(lParam)));

        case WM_CLOSE:
            DestroyWindow(hwnd);
            return 0;
        case WM_DESTROY:
            self->theme_.Destroy();
            self->hwnd_ = nullptr;
            PostQuitMessage(0);
            return 0;
    }

    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

}  // namespace NextKey::Classic
