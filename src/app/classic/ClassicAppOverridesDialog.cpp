// NexusKey Classic — App Overrides Dialog Implementation
// SPDX-License-Identifier: GPL-3.0-only

#include "ClassicAppOverridesDialog.h"
#include "core/config/ConfigEvent.h"

#include <windowsx.h>
#include <algorithm>
#include <vector>

namespace NextKey::Classic {

enum {
    IDC_LIST_OVERRIDES = 3201,
    IDC_EDIT_APP,
    IDC_COMBO_METHOD,
    IDC_COMBO_ENCODING,
    IDC_BTN_ADD,
    IDC_BTN_PICK,
    IDC_BTN_DELETE,
    IDC_BTN_CLOSE_DLG,
};

// ════════════════════════════════════════════════════════════

bool ClassicAppOverridesDialog::Show(HINSTANCE hInstance, HWND parent) {
    ClassicAppOverridesDialog dlg;
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

bool ClassicAppOverridesDialog::Init(HINSTANCE hInstance, HWND parent) {
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
    hwnd_ = CreateWindowExW(WS_EX_TOPMOST, kClassName, L"Tuỳ chỉnh theo ứng dụng",
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

    LoadData();
    CreateControls();
    PopulateList();

    EnumChildWindows(hwnd_, [](HWND h, LPARAM lp) -> BOOL {
        auto* self = reinterpret_cast<ClassicAppOverridesDialog*>(lp);
        SendMessageW(h, WM_SETFONT, reinterpret_cast<WPARAM>(self->theme_.Fonts().body), TRUE);
        return TRUE;
    }, reinterpret_cast<LPARAM>(this));
    theme_.ThemeAllChildren(hwnd_);

    ShowWindow(hwnd_, SW_SHOW);
    UpdateWindow(hwnd_);
    return true;
}

// ════════════════════════════════════════════════════════════

void ClassicAppOverridesDialog::CreateControls() {
    int x = Dpi(kPadding), y = Dpi(kPadding);
    int cw = Dpi(kWidth - kPadding * 2);
    int btnH = Dpi(kBtnHeight);
    int gap = Dpi(kBtnGap);

    // ListView: app | method | encoding
    int listH = Dpi(200);
    listView_ = CreateWindowExW(WS_EX_CLIENTEDGE, WC_LISTVIEWW, L"",
        WS_CHILD | WS_VISIBLE | LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS | LVS_NOSORTHEADER,
        x, y, cw, listH, hwnd_, reinterpret_cast<HMENU>(IDC_LIST_OVERRIDES), hInstance_, nullptr);
    ListView_SetExtendedListViewStyle(listView_, LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER);

    LVCOLUMNW col{};
    col.mask = LVCF_TEXT | LVCF_WIDTH;
    col.pszText = const_cast<wchar_t*>(L"Ứng dụng");
    col.cx = Dpi(140);
    ListView_InsertColumn(listView_, 0, &col);
    col.pszText = const_cast<wchar_t*>(L"Kiểu gõ");
    col.cx = Dpi(100);
    ListView_InsertColumn(listView_, 1, &col);
    col.pszText = const_cast<wchar_t*>(L"Bảng mã");
    col.cx = cw - Dpi(140 + 100 + 24);
    ListView_InsertColumn(listView_, 2, &col);
    y += listH + gap;

    // Row: app name edit + pick button
    int editW = cw - Dpi(90) - gap;
    editApp_ = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL | ES_LOWERCASE,
        x, y, editW, btnH, hwnd_, reinterpret_cast<HMENU>(IDC_EDIT_APP), hInstance_, nullptr);
    SendMessageW(editApp_, EM_SETCUEBANNER, FALSE, reinterpret_cast<LPARAM>(L"chrome.exe"));

    btnPick_ = CreateWindowExW(0, L"BUTTON", L"\u2316 Chọn cửa sổ",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
        x + editW + gap, y, Dpi(90), btnH,
        hwnd_, reinterpret_cast<HMENU>(IDC_BTN_PICK), hInstance_, nullptr);
    y += btnH + gap;

    // Row: method combo + encoding combo + add button
    int lblW = Dpi(50);
    int comboW = Dpi(110);

    CreateWindowExW(0, L"STATIC", L"Kiểu gõ:",
        WS_CHILD | WS_VISIBLE | SS_LEFT,
        x, y + Dpi(4), lblW, btnH, hwnd_, nullptr, hInstance_, nullptr);
    comboMethod_ = CreateWindowExW(0, L"COMBOBOX", L"",
        WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_TABSTOP,
        x + lblW, y, comboW, Dpi(120), hwnd_, reinterpret_cast<HMENU>(IDC_COMBO_METHOD), hInstance_, nullptr);
    ComboBox_AddString(comboMethod_, L"Theo mặc định");
    ComboBox_AddString(comboMethod_, L"Telex");
    ComboBox_AddString(comboMethod_, L"VNI");
    ComboBox_AddString(comboMethod_, L"Simple Telex");
    ComboBox_SetCurSel(comboMethod_, 0);

    int col2X = x + lblW + comboW + gap;
    CreateWindowExW(0, L"STATIC", L"Bảng mã:",
        WS_CHILD | WS_VISIBLE | SS_LEFT,
        col2X, y + Dpi(4), lblW, btnH, hwnd_, nullptr, hInstance_, nullptr);
    comboEncoding_ = CreateWindowExW(0, L"COMBOBOX", L"",
        WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_TABSTOP,
        col2X + lblW, y, comboW, Dpi(120), hwnd_, reinterpret_cast<HMENU>(IDC_COMBO_ENCODING), hInstance_, nullptr);
    ComboBox_AddString(comboEncoding_, L"Theo mặc định");
    ComboBox_AddString(comboEncoding_, L"Unicode");
    ComboBox_AddString(comboEncoding_, L"TCVN3");
    ComboBox_AddString(comboEncoding_, L"VNI Windows");
    ComboBox_AddString(comboEncoding_, L"Unicode tổ hợp");
    ComboBox_AddString(comboEncoding_, L"Việt CP1258");
    ComboBox_SetCurSel(comboEncoding_, 0);
    y += btnH + gap;

    // Add + Delete buttons
    int halfW = (cw - gap) / 2;
    btnAdd_ = CreateWindowExW(0, L"BUTTON", L"Thêm / Cập nhật",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
        x, y, halfW, btnH, hwnd_, reinterpret_cast<HMENU>(IDC_BTN_ADD), hInstance_, nullptr);
    btnDelete_ = CreateWindowExW(0, L"BUTTON", L"Xoá",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
        x + halfW + gap, y, halfW, btnH, hwnd_, reinterpret_cast<HMENU>(IDC_BTN_DELETE), hInstance_, nullptr);
    y += btnH + gap * 2;

    btnClose_ = CreateWindowExW(0, L"BUTTON", L"Đóng",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON,
        x + cw - Dpi(80), y, Dpi(80), btnH,
        hwnd_, reinterpret_cast<HMENU>(IDC_BTN_CLOSE_DLG), hInstance_, nullptr);
}

static const wchar_t* MethodName(int8_t m) {
    switch (m) {
        case 0: return L"Telex";
        case 1: return L"VNI";
        case 2: return L"Simple Telex";
        default: return L"Mặc định";
    }
}

static const wchar_t* EncodingName(int8_t e) {
    switch (e) {
        case 0: return L"Unicode";
        case 1: return L"TCVN3";
        case 2: return L"VNI Windows";
        case 3: return L"Unicode tổ hợp";
        case 4: return L"Việt CP1258";
        default: return L"Mặc định";
    }
}

void ClassicAppOverridesDialog::PopulateList() {
    ListView_DeleteAllItems(listView_);

    std::vector<std::pair<std::wstring, AppOverrideEntry>> sorted(entries_.begin(), entries_.end());
    std::sort(sorted.begin(), sorted.end(),
        [](auto& a, auto& b) { return a.first < b.first; });

    for (int i = 0; i < static_cast<int>(sorted.size()); ++i) {
        LVITEMW item{};
        item.mask = LVIF_TEXT;
        item.iItem = i;
        item.pszText = const_cast<wchar_t*>(sorted[i].first.c_str());
        ListView_InsertItem(listView_, &item);

        ListView_SetItemText(listView_, i, 1, const_cast<wchar_t*>(MethodName(sorted[i].second.inputMethod)));
        ListView_SetItemText(listView_, i, 2, const_cast<wchar_t*>(EncodingName(sorted[i].second.encodingOverride)));
    }
}

// ════════════════════════════════════════════════════════════

void ClassicAppOverridesDialog::AddOverride() {
    wchar_t buf[256] = {};
    GetWindowTextW(editApp_, buf, 256);
    std::wstring app = ToLowerAscii(buf);
    if (app.empty()) return;

    int methodSel = ComboBox_GetCurSel(comboMethod_);
    int encodingSel = ComboBox_GetCurSel(comboEncoding_);

    AppOverrideEntry entry;
    entry.inputMethod = static_cast<int8_t>(methodSel - 1);   // 0="default"→-1, 1=Telex→0, etc.
    entry.encodingOverride = static_cast<int8_t>(encodingSel - 1);

    entries_[app] = entry;
    PopulateList();
    SaveData();

    SetWindowTextW(editApp_, L"");
    ComboBox_SetCurSel(comboMethod_, 0);
    ComboBox_SetCurSel(comboEncoding_, 0);
}

void ClassicAppOverridesDialog::DeleteSelected() {
    int sel = ListView_GetNextItem(listView_, -1, LVNI_SELECTED);
    if (sel < 0) return;

    wchar_t key[256] = {};
    ListView_GetItemText(listView_, sel, 0, key, 256);
    entries_.erase(key);
    PopulateList();
    SaveData();
}

void ClassicAppOverridesDialog::OnPickWindow() {
    picker_.Start(hwnd_, [this](const std::wstring& exeName) {
        SetWindowTextW(editApp_, exeName.c_str());
    });
}

void ClassicAppOverridesDialog::LoadData() {
    entries_ = ConfigManager::LoadAppOverrides(ConfigManager::GetConfigPath());
}

void ClassicAppOverridesDialog::SaveData() {
    modified_ = true;
    (void)ConfigManager::SaveAppOverrides(ConfigManager::GetConfigPath(), entries_);

    ConfigEvent event;
    if (event.Initialize()) event.Signal();
}

int ClassicAppOverridesDialog::Dpi(int value) const noexcept {
    return MulDiv(value, static_cast<int>(dpi_), 96);
}

// ════════════════════════════════════════════════════════════

LRESULT CALLBACK ClassicAppOverridesDialog::WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    ClassicAppOverridesDialog* self = nullptr;

    if (msg == WM_NCCREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
        self = reinterpret_cast<ClassicAppOverridesDialog*>(cs->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        self->hwnd_ = hwnd;
    } else {
        self = reinterpret_cast<ClassicAppOverridesDialog*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }

    if (!self) return DefWindowProcW(hwnd, msg, wParam, lParam);

    if (self->picker_.HandleMessage(msg, wParam)) return 0;

    switch (msg) {
        case WM_COMMAND: {
            UINT id = LOWORD(wParam);
            switch (id) {
                case IDC_BTN_ADD:       self->AddOverride();    return 0;
                case IDC_BTN_PICK:      self->OnPickWindow();   return 0;
                case IDC_BTN_DELETE:    self->DeleteSelected(); return 0;
                case IDC_BTN_CLOSE_DLG: DestroyWindow(hwnd);    return 0;
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
