// NexusKey Classic — User Defined Input Dialog Implementation
// SPDX-License-Identifier: GPL-3.0-only

#include "ClassicUserDefinedDialog.h"
#include "core/config/ConfigManager.h"
#include "app/helpers/AppHelpers.h"
#include "core/ipc/SharedStateManager.h"
#include "core/config/ConfigEvent.h"

#include <windowsx.h>
#include <algorithm>
#include <fstream>
#include <vector>

namespace NextKey::Classic {

enum {
    IDC_LIST_KEYMAP = 3201,
    IDC_EDIT_KEY,
    IDC_COMBO_ACTION,
    IDC_BTN_ADD,
    IDC_BTN_DELETE,
    IDC_BTN_LOAD_TELEX,
    IDC_BTN_LOAD_VNI,
    IDC_BTN_IMPORT,
    IDC_BTN_EXPORT
};

// ════════════════════════════════════════════════════════════
// Public
// ════════════════════════════════════════════════════════════

bool ClassicUserDefinedDialog::Show(HINSTANCE hInstance, HWND parent, bool forceLightTheme) {
    ClassicUserDefinedDialog dlg;
    if (!dlg.Init(hInstance, parent, forceLightTheme)) return false;

    MSG msg{};
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        if (!IsDialogMessageW(dlg.hwnd_, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        if (!IsWindow(dlg.hwnd_)) break;
    }
    return dlg.modified_;
}

// ════════════════════════════════════════════════════════════
// Init
// ════════════════════════════════════════════════════════════

bool ClassicUserDefinedDialog::Init(HINSTANCE hInstance, HWND parent, bool forceLightTheme) {
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
    hwnd_ = CreateWindowExW(WS_EX_TOPMOST, kClassName, L"Kiểu gõ Tự định nghĩa",
        style, CW_USEDEFAULT, CW_USEDEFAULT, 400, 300,
        parent, nullptr, hInstance, this);
    if (!hwnd_) return false;

    dpi_ = Classic::GetWindowDpi(hwnd_);

    int w = Dpi(kWidth), h = Dpi(kHeight);
    RECT rc = {0, 0, w, h};
    AdjustWindowRectEx(&rc, style, FALSE, WS_EX_TOPMOST);
    int aw = rc.right - rc.left, ah = rc.bottom - rc.top;
    POINT pt = NextKey::GetCenteredPos(hwnd_, aw, ah);
    SetWindowPos(hwnd_, nullptr, pt.x, pt.y, aw, ah, SWP_NOZORDER);

    theme_.Init(hwnd_, forceLightTheme);
    theme_.ApplyWindowAttributes(hwnd_);

    CreateControls();
    LoadData();
    PopulateList();

    EnumChildWindows(hwnd_, [](HWND h, LPARAM lp) -> BOOL {
        auto* self = reinterpret_cast<ClassicUserDefinedDialog*>(lp);
        SendMessageW(h, WM_SETFONT, reinterpret_cast<WPARAM>(self->theme_.Fonts().body), TRUE);
        return TRUE;
    }, reinterpret_cast<LPARAM>(this));
    theme_.ThemeAllChildren(hwnd_);

    ShowWindow(hwnd_, SW_SHOW);
    UpdateWindow(hwnd_);
    return true;
}

void ClassicUserDefinedDialog::CreateControls() {
    int x = Dpi(kPadding), y = Dpi(kPadding);
    int cw = Dpi(kWidth - kPadding * 2);
    int btnH = Dpi(kBtnHeight);
    int gap = Dpi(kBtnGap);

    // ListView — 2 columns: Key, Action
    int listH = Dpi(180);
    listView_ = CreateWindowExW(WS_EX_CLIENTEDGE, WC_LISTVIEWW, L"",
        WS_CHILD | WS_VISIBLE | LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS | LVS_NOSORTHEADER,
        x, y, cw, listH, hwnd_, reinterpret_cast<HMENU>(IDC_LIST_KEYMAP), hInstance_, nullptr);
    ListView_SetExtendedListViewStyle(listView_, LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER);

    LVCOLUMNW col{};
    col.mask = LVCF_TEXT | LVCF_WIDTH;
    col.pszText = const_cast<wchar_t*>(L"Phím");
    col.cx = Dpi(60);
    ListView_InsertColumn(listView_, 0, &col);

    col.pszText = const_cast<wchar_t*>(L"Hành động");
    col.cx = cw - Dpi(60 + 24);
    ListView_InsertColumn(listView_, 1, &col);
    y += listH + gap;

    // Row: Key edit + Action combo + Add button
    int editH = theme_.ModernHeight();
    int keyW = Dpi(40);
    int addW = Dpi(60);
    int comboW = cw - keyW - addW - gap * 2;

    editKey_ = CreateWindowExW(0, L"EDIT", L"",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
        x, y, keyW, editH, hwnd_, reinterpret_cast<HMENU>(IDC_EDIT_KEY), hInstance_, nullptr);
    SendMessageW(editKey_, EM_SETLIMITTEXT, 1, 0);
    theme_.ApplyModernEntryStyle(editKey_);

    comboAction_ = CreateWindowExW(0, WC_COMBOBOXW, L"",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST | WS_VSCROLL,
        x + keyW + gap, y, comboW, Dpi(300), hwnd_, reinterpret_cast<HMENU>(IDC_COMBO_ACTION), hInstance_, nullptr);
    
    // Fill combo with all valid actions (None is skipped as we only want to map actual actions)
    for (int i = 1; i <= 34; ++i) { // 34 is current max action ID
        TypingAction action = static_cast<TypingAction>(i);
        std::string name = std::string(TypingActionToString(action));
        if (name != "None") {
            SendMessageW(comboAction_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(Utf8ToWide(name).c_str()));
        }
    }
    SendMessageW(comboAction_, CB_SETCURSEL, 0, 0);

    btnAdd_ = CreateWindowExW(0, L"BUTTON", L"Thêm",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
        x + cw - addW, y, addW, editH,
        hwnd_, reinterpret_cast<HMENU>(IDC_BTN_ADD), hInstance_, nullptr);
    y += editH + gap * 2;

    // Template row
    int templW = (cw - gap) / 2;
    btnLoadTelex_ = CreateWindowExW(0, L"BUTTON", L"Nạp mẫu Telex",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
        x, y, templW, btnH, hwnd_, reinterpret_cast<HMENU>(IDC_BTN_LOAD_TELEX), hInstance_, nullptr);
    btnLoadVni_ = CreateWindowExW(0, L"BUTTON", L"Nạp mẫu VNI",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
        x + templW + gap, y, templW, btnH, hwnd_, reinterpret_cast<HMENU>(IDC_BTN_LOAD_VNI), hInstance_, nullptr);
    y += btnH + gap * 2;

    // Action buttons row
    int abw = (cw - gap) / 2;
    btnDelete_ = CreateWindowExW(0, L"BUTTON", L"Xoá đã chọn",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
        x, y, cw, btnH, hwnd_, reinterpret_cast<HMENU>(IDC_BTN_DELETE), hInstance_, nullptr);
    y += btnH + gap;

    btnImport_ = CreateWindowExW(0, L"BUTTON", L"Nhập từ file...",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
        x, y, abw, btnH, hwnd_, reinterpret_cast<HMENU>(IDC_BTN_IMPORT), hInstance_, nullptr);
    btnExport_ = CreateWindowExW(0, L"BUTTON", L"Xuất ra file...",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
        x + abw + gap, y, abw, btnH, hwnd_, reinterpret_cast<HMENU>(IDC_BTN_EXPORT), hInstance_, nullptr);
}

void ClassicUserDefinedDialog::LoadData() {
    auto config = ConfigManager::LoadOrDefault();
    keyMap_ = config.customKeyMap;
}

void ClassicUserDefinedDialog::SaveData() {
    auto config = ConfigManager::LoadOrDefault();
    config.customKeyMap = keyMap_;
    ConfigManager::SaveToFile(ConfigManager::GetConfigPath(), config);
    
    // Signal reload to engine
    SharedStateManager sm;
    if (sm.OpenReadWrite()) {
        SharedState state = sm.Read();
        if (state.IsValid()) {
            state.configGeneration++;
            sm.Write(state);
        }
    }
    ConfigEvent event;
    if (event.Initialize()) event.Signal();
    
    modified_ = true;
}

void ClassicUserDefinedDialog::PopulateList() {
    ListView_DeleteAllItems(listView_);

    struct Entry { uint8_t key; TypingAction action; };
    std::vector<Entry> entries;
    for (size_t i = 0; i < 128; ++i) {
        if (keyMap_[i] != TypingAction::None) {
            entries.push_back({ static_cast<uint8_t>(i), keyMap_[i] });
        }
    }

    for (size_t i = 0; i < entries.size(); ++i) {
        LVITEMW item{};
        item.mask = LVIF_TEXT;
        item.iItem = static_cast<int>(i);

        wchar_t keyStr[2] = { static_cast<wchar_t>(entries[i].key), 0 };
        item.pszText = keyStr;
        ListView_InsertItem(listView_, &item);

        std::string actionName = std::string(TypingActionToString(entries[i].action));
        ListView_SetItemText(listView_, static_cast<int>(i), 1, const_cast<wchar_t*>(Utf8ToWide(actionName).c_str()));
    }
}

void ClassicUserDefinedDialog::AddKey() {
    wchar_t key[2] = {0};
    GetWindowTextW(editKey_, key, 2);
    if (!key[0]) return;

    wchar_t k = towlower(key[0]);
    if (k >= 128) return;

    wchar_t actionBuf[128] = {0};
    GetWindowTextW(comboAction_, actionBuf, 128);
    TypingAction action = StringToTypingAction(WideToUtf8(actionBuf));

    keyMap_[static_cast<uint8_t>(k)] = action;
    SaveData();
    PopulateList();
    SetWindowTextW(editKey_, L"");
}

void ClassicUserDefinedDialog::DeleteSelected() {
    int sel = ListView_GetNextItem(listView_, -1, LVNI_SELECTED);
    if (sel == -1) return;

    wchar_t key[2] = {0};
    ListView_GetItemText(listView_, sel, 0, key, 2);
    if (key[0] < 128) {
        keyMap_[static_cast<uint8_t>(key[0])] = TypingAction::None;
        SaveData();
        PopulateList();
    }
}

void ClassicUserDefinedDialog::LoadTemplate(bool telex) {
    keyMap_.fill(TypingAction::None);
    std::string chars = "abcdefghijklmnopqrstuvwxyz0123456789[]";
    for (char c : chars) {
        TypingAction action = ClassifyKey(static_cast<wchar_t>(c), telex, !telex);
        if (action != TypingAction::None) {
            keyMap_[static_cast<uint8_t>(c)] = action;
        }
    }
    SaveData();
    PopulateList();
}

void ClassicUserDefinedDialog::ImportFromFile() {
    std::wstring path = DialogUtils::OpenFileDialog(hwnd_, L"Keymap files (*.keymap)\0*.keymap\0All files (*.*)\0*.*\0", L"keymap");
    if (!path.empty()) {
        try {
            auto tbl = toml::parse_file(WideToUtf8(path));
            TypingConfig dummy;
            ConfigManager::LoadCustomKeyMap(&tbl, dummy);
            keyMap_ = dummy.customKeyMap;
            SaveData();
            PopulateList();
        } catch (...) {}
    }
}

void ClassicUserDefinedDialog::ExportToFile() {
    std::wstring path = DialogUtils::SaveFileDialog(hwnd_, L"Keymap files (*.keymap)\0*.keymap\0", L"keymap", L"custom.keymap");
    if (!path.empty()) {
        toml::table tbl;
        TypingConfig dummy;
        dummy.customKeyMap = keyMap_;
        ConfigManager::SaveCustomKeyMap(&tbl, dummy);
        std::ofstream file(path);
        if (file.is_open()) {
            file << tbl;
            file.close();
        }
    }
}

LRESULT CALLBACK ClassicUserDefinedDialog::WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    auto* self = reinterpret_cast<ClassicUserDefinedDialog*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));

    if (msg == WM_NCCREATE) {
        auto* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
        self = reinterpret_cast<ClassicUserDefinedDialog*>(create->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    }

    if (!self) return DefWindowProcW(hwnd, msg, wParam, lParam);

    switch (msg) {
        case WM_COMMAND: {
            uint16_t id = LOWORD(wParam);
            uint16_t code = HIWORD(wParam);
            if (id == IDC_BTN_ADD) self->AddKey();
            else if (id == IDC_BTN_DELETE) self->DeleteSelected();
            else if (id == IDC_BTN_LOAD_TELEX) self->LoadTemplate(true);
            else if (id == IDC_BTN_LOAD_VNI) self->LoadTemplate(false);
            else if (id == IDC_BTN_IMPORT) self->ImportFromFile();
            else if (id == IDC_BTN_EXPORT) self->ExportToFile();
            break;
        }
        case WM_CLOSE:
            DestroyWindow(hwnd);
            return 0;
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
    }

    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

int ClassicUserDefinedDialog::Dpi(int value) const noexcept {
    return MulDiv(value, static_cast<int>(dpi_), 96);
}

} // namespace NextKey::Classic
