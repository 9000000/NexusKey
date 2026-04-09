// NexusKey Classic — Convert Tool Dialog Implementation
// SPDX-License-Identifier: GPL-3.0-only

#include "ClassicConvertToolDialog.h"
#include "core/config/ConfigManager.h"
#include "core/config/ConfigEvent.h"

#include <windowsx.h>

namespace NextKey::Classic {

enum {
    IDC_CHECK_ALL_CAPS = 3301,
    IDC_CHECK_ALL_LOWER,
    IDC_CHECK_CAPS_FIRST,
    IDC_CHECK_CAPS_EACH,
    IDC_CHECK_REMOVE_MARK,
    IDC_CHECK_ALERT_DONE,
    IDC_CHECK_AUTO_PASTE,
    IDC_CHECK_SEQUENTIAL,
    IDC_COMBO_SOURCE,
    IDC_COMBO_DEST,
    IDC_CHECK_HK_CTRL,
    IDC_CHECK_HK_ALT,
    IDC_CHECK_HK_SHIFT,
    IDC_CHECK_HK_WIN,
    IDC_EDIT_HK_KEY,
    IDC_BTN_CONVERT,
    IDC_BTN_CLOSE_DLG,
};

// ════════════════════════════════════════════════════════════

bool ClassicConvertToolDialog::Show(HINSTANCE hInstance, HWND parent) {
    ClassicConvertToolDialog dlg;
    if (!dlg.Init(hInstance, parent)) return false;

    MSG msg{};
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        if (!IsDialogMessageW(dlg.hwnd_, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
    return dlg.modified_;
}

bool ClassicConvertToolDialog::Init(HINSTANCE hInstance, HWND parent) {
    hInstance_ = hInstance;

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
    hwnd_ = CreateWindowExW(WS_EX_TOPMOST, kClassName, L"Công cụ chuyển đổi",
        style, CW_USEDEFAULT, CW_USEDEFAULT, 400, 300,
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

    config_ = ConfigManager::LoadConvertConfigOrDefault();
    CreateControls();
    PopulateFromConfig();

    EnumChildWindows(hwnd_, [](HWND h, LPARAM lp) -> BOOL {
        auto* self = reinterpret_cast<ClassicConvertToolDialog*>(lp);
        SendMessageW(h, WM_SETFONT, reinterpret_cast<WPARAM>(self->theme_.Fonts().body), TRUE);
        return TRUE;
    }, reinterpret_cast<LPARAM>(this));
    theme_.ThemeAllChildren(hwnd_);

    ShowWindow(hwnd_, SW_SHOW);
    UpdateWindow(hwnd_);
    return true;
}

// ════════════════════════════════════════════════════════════

void ClassicConvertToolDialog::CreateControls() {
    int x = Dpi(kPadding), y = Dpi(kPadding);
    int cw = Dpi(kWidth - kPadding * 2);
    int rowH = Dpi(kRowH);
    int gap = Dpi(kRowGap);
    int btnH = Dpi(kBtnHeight);
    int colW = (cw - Dpi(8)) / 2;
    int col2X = x + colW + Dpi(8);

    auto check = [&](const wchar_t* text, int cx, int cy, UINT id) -> HWND {
        return CreateWindowExW(0, L"BUTTON", text,
            WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX | WS_TABSTOP,
            cx, cy, colW, rowH, hwnd_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
            hInstance_, nullptr);
    };

    auto label = [&](const wchar_t* text, int cx, int cy, int w) {
        CreateWindowExW(0, L"STATIC", text, WS_CHILD | WS_VISIBLE | SS_LEFT,
            cx, cy, w, rowH, hwnd_, nullptr, hInstance_, nullptr);
    };

    // Section: Chuyển đổi chữ
    label(L"Chuyển đổi chữ:", x, y, cw);
    y += rowH + gap;

    checkAllCaps_ = check(L"Chữ HOA", x, y, IDC_CHECK_ALL_CAPS);
    checkAllLower_ = check(L"Chữ thường", col2X, y, IDC_CHECK_ALL_LOWER);
    y += rowH + gap;

    checkCapsFirst_ = check(L"Hoa đầu câu", x, y, IDC_CHECK_CAPS_FIRST);
    checkCapsEach_ = check(L"Hoa Đầu Từ", col2X, y, IDC_CHECK_CAPS_EACH);
    y += rowH + gap;

    checkRemoveMark_ = check(L"Loại bỏ dấu", x, y, IDC_CHECK_REMOVE_MARK);
    y += rowH + gap * 2;

    // Section: Tuỳ chọn
    label(L"Tuỳ chọn:", x, y, cw);
    y += rowH + gap;

    checkAlertDone_ = check(L"Thông báo khi xong", x, y, IDC_CHECK_ALERT_DONE);
    checkAutoPaste_ = check(L"Tự động dán", col2X, y, IDC_CHECK_AUTO_PASTE);
    y += rowH + gap;

    checkSequential_ = check(L"Chuyển tuần tự", x, y, IDC_CHECK_SEQUENTIAL);
    y += rowH + gap * 2;

    // Section: Bảng mã
    label(L"Bảng mã nguồn:", x, y + Dpi(4), Dpi(90));
    comboSource_ = CreateWindowExW(0, L"COMBOBOX", L"",
        WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_TABSTOP,
        x + Dpi(90), y, cw - Dpi(90), Dpi(120),
        hwnd_, reinterpret_cast<HMENU>(IDC_COMBO_SOURCE), hInstance_, nullptr);
    y += rowH + gap;

    label(L"Bảng mã đích:", x, y + Dpi(4), Dpi(90));
    comboDest_ = CreateWindowExW(0, L"COMBOBOX", L"",
        WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_TABSTOP,
        x + Dpi(90), y, cw - Dpi(90), Dpi(120),
        hwnd_, reinterpret_cast<HMENU>(IDC_COMBO_DEST), hInstance_, nullptr);

    // Populate encoding combos
    const wchar_t* encodings[] = {L"Unicode", L"TCVN3 (ABC)", L"VNI Windows", L"Unicode tổ hợp", L"Việt (CP 1258)"};
    for (auto& e : encodings) {
        ComboBox_AddString(comboSource_, e);
        ComboBox_AddString(comboDest_, e);
    }
    y += rowH + gap * 2;

    // Section: Phím tắt
    label(L"Phím tắt:", x, y, cw);
    y += rowH + gap;

    int hkBtnW = Dpi(50);
    checkHkCtrl_ = check(L"Ctrl", x, y, IDC_CHECK_HK_CTRL);
    checkHkAlt_ = CreateWindowExW(0, L"BUTTON", L"Alt",
        WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX | WS_TABSTOP,
        x + hkBtnW + Dpi(4), y, hkBtnW, rowH,
        hwnd_, reinterpret_cast<HMENU>(IDC_CHECK_HK_ALT), hInstance_, nullptr);
    checkHkShift_ = CreateWindowExW(0, L"BUTTON", L"Shift",
        WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX | WS_TABSTOP,
        x + (hkBtnW + Dpi(4)) * 2, y, hkBtnW, rowH,
        hwnd_, reinterpret_cast<HMENU>(IDC_CHECK_HK_SHIFT), hInstance_, nullptr);
    checkHkWin_ = CreateWindowExW(0, L"BUTTON", L"Win",
        WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX | WS_TABSTOP,
        x + (hkBtnW + Dpi(4)) * 3, y, hkBtnW, rowH,
        hwnd_, reinterpret_cast<HMENU>(IDC_CHECK_HK_WIN), hInstance_, nullptr);

    editHkKey_ = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_UPPERCASE | ES_CENTER | ES_AUTOHSCROLL,
        x + (hkBtnW + Dpi(4)) * 4, y, Dpi(30), rowH,
        hwnd_, reinterpret_cast<HMENU>(IDC_EDIT_HK_KEY), hInstance_, nullptr);
    SendMessageW(editHkKey_, EM_SETLIMITTEXT, 1, 0);
    y += rowH + gap * 3;

    // Action buttons
    int halfW = (cw - Dpi(8)) / 2;
    btnConvert_ = CreateWindowExW(0, L"BUTTON", L"Chuyển đổi",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON,
        x, y, halfW, btnH, hwnd_, reinterpret_cast<HMENU>(IDC_BTN_CONVERT), hInstance_, nullptr);
    btnClose_ = CreateWindowExW(0, L"BUTTON", L"Đóng",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
        x + halfW + Dpi(8), y, halfW, btnH,
        hwnd_, reinterpret_cast<HMENU>(IDC_BTN_CLOSE_DLG), hInstance_, nullptr);
}

void ClassicConvertToolDialog::PopulateFromConfig() {
    auto setCheck = [&](HWND h, bool v) {
        if (h) CheckDlgButton(hwnd_, GetDlgCtrlID(h), v ? BST_CHECKED : BST_UNCHECKED);
    };

    setCheck(checkAllCaps_, config_.allCaps);
    setCheck(checkAllLower_, config_.allLower);
    setCheck(checkCapsFirst_, config_.capsFirst);
    setCheck(checkCapsEach_, config_.capsEach);
    setCheck(checkRemoveMark_, config_.removeMark);
    setCheck(checkAlertDone_, config_.alertDone);
    setCheck(checkAutoPaste_, config_.autoPaste);
    setCheck(checkSequential_, config_.sequential);

    ComboBox_SetCurSel(comboSource_, config_.sourceEncoding);
    ComboBox_SetCurSel(comboDest_, config_.destEncoding);

    setCheck(checkHkCtrl_, config_.hotkey.ctrl);
    setCheck(checkHkAlt_, config_.hotkey.alt);
    setCheck(checkHkShift_, config_.hotkey.shift);
    setCheck(checkHkWin_, config_.hotkey.win);

    if (config_.hotkey.key) {
        wchar_t buf[2] = {config_.hotkey.key, 0};
        SetWindowTextW(editHkKey_, buf);
    }

    // Sequential only available when autoPaste is on
    EnableWindow(checkSequential_, config_.autoPaste ? TRUE : FALSE);
}

void ClassicConvertToolDialog::ReadToConfig() {
    auto isChecked = [&](UINT id) -> bool {
        return IsDlgButtonChecked(hwnd_, id) == BST_CHECKED;
    };

    config_.allCaps = isChecked(IDC_CHECK_ALL_CAPS);
    config_.allLower = isChecked(IDC_CHECK_ALL_LOWER);
    config_.capsFirst = isChecked(IDC_CHECK_CAPS_FIRST);
    config_.capsEach = isChecked(IDC_CHECK_CAPS_EACH);
    config_.removeMark = isChecked(IDC_CHECK_REMOVE_MARK);
    config_.alertDone = isChecked(IDC_CHECK_ALERT_DONE);
    config_.autoPaste = isChecked(IDC_CHECK_AUTO_PASTE);
    config_.sequential = isChecked(IDC_CHECK_SEQUENTIAL);

    int srcSel = ComboBox_GetCurSel(comboSource_);
    int dstSel = ComboBox_GetCurSel(comboDest_);
    if (srcSel >= 0) config_.sourceEncoding = static_cast<uint8_t>(srcSel);
    if (dstSel >= 0) config_.destEncoding = static_cast<uint8_t>(dstSel);

    config_.hotkey.ctrl = isChecked(IDC_CHECK_HK_CTRL);
    config_.hotkey.alt = isChecked(IDC_CHECK_HK_ALT);
    config_.hotkey.shift = isChecked(IDC_CHECK_HK_SHIFT);
    config_.hotkey.win = isChecked(IDC_CHECK_HK_WIN);

    wchar_t buf[2] = {};
    GetWindowTextW(editHkKey_, buf, 2);
    wchar_t key = buf[0];
    if (key >= L'a' && key <= L'z') key = key - L'a' + L'A';
    config_.hotkey.key = key;
}

void ClassicConvertToolDialog::SaveConfig() {
    ReadToConfig();
    modified_ = true;
    (void)ConfigManager::SaveConvertConfig(ConfigManager::GetConfigPath(), config_);

    ConfigEvent event;
    if (event.Initialize()) event.Signal();
}

void ClassicConvertToolDialog::DoConvert() {
    // Save current settings first
    SaveConfig();

    // TODO: Implement conversion pipeline (clipboard read → decode → transform → encode → clipboard write)
    // For now, just inform user
    MessageBoxW(hwnd_,
        L"Chức năng chuyển đổi sẽ thực hiện:\n"
        L"1. Đọc từ clipboard\n"
        L"2. Chuyển mã nguồn → Unicode\n"
        L"3. Áp dụng chuyển đổi chữ\n"
        L"4. Chuyển Unicode → mã đích\n"
        L"5. Ghi vào clipboard\n\n"
        L"Bạn có thể dùng phím tắt đã cấu hình để chuyển nhanh.",
        L"Chuyển đổi",
        MB_ICONINFORMATION);
}

int ClassicConvertToolDialog::Dpi(int value) const noexcept {
    return MulDiv(value, static_cast<int>(dpi_), 96);
}

// ════════════════════════════════════════════════════════════

LRESULT CALLBACK ClassicConvertToolDialog::WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    ClassicConvertToolDialog* self = nullptr;

    if (msg == WM_NCCREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
        self = reinterpret_cast<ClassicConvertToolDialog*>(cs->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        self->hwnd_ = hwnd;
    } else {
        self = reinterpret_cast<ClassicConvertToolDialog*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }

    if (!self) return DefWindowProcW(hwnd, msg, wParam, lParam);

    switch (msg) {
        case WM_COMMAND: {
            UINT id = LOWORD(wParam);
            UINT code = HIWORD(wParam);

            switch (id) {
                case IDC_BTN_CONVERT:   self->DoConvert();     return 0;
                case IDC_BTN_CLOSE_DLG: DestroyWindow(hwnd);   return 0;
            }

            // Auto-save on any toggle/combo/edit change
            if (code == BN_CLICKED || code == CBN_SELCHANGE) {
                self->SaveConfig();

                // Enable/disable sequential based on autoPaste
                if (id == IDC_CHECK_AUTO_PASTE) {
                    bool ap = IsDlgButtonChecked(hwnd, IDC_CHECK_AUTO_PASTE) == BST_CHECKED;
                    EnableWindow(self->checkSequential_, ap ? TRUE : FALSE);
                }
            }
            if (code == EN_CHANGE && id == IDC_EDIT_HK_KEY) {
                self->SaveConfig();
            }
            break;
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
        case WM_CTLCOLOREDIT:
            return reinterpret_cast<LRESULT>(self->theme_.OnCtlColorEdit(
                reinterpret_cast<HDC>(wParam), reinterpret_cast<HWND>(lParam)));
        case WM_CTLCOLORLISTBOX:
            return reinterpret_cast<LRESULT>(self->theme_.OnCtlColorListBox(
                reinterpret_cast<HDC>(wParam), reinterpret_cast<HWND>(lParam)));
        case WM_CTLCOLORBTN:
            return reinterpret_cast<LRESULT>(self->theme_.OnCtlColorBtn(
                reinterpret_cast<HDC>(wParam), reinterpret_cast<HWND>(lParam)));

        case WM_CLOSE:
            self->SaveConfig();
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
