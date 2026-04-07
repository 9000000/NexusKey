// NexusKey Classic — Settings Dialog Implementation
// Compact (Unikey-style) + Advanced (EVKey-style) modes
// SPDX-License-Identifier: GPL-3.0-only

#include "ClassicSettingsDialog.h"
#include "core/config/ConfigManager.h"
#include "core/Debug.h"

#include <windowsx.h>

namespace NextKey::Classic {

// ════════════════════════════════════════════════════════════════════
// Lifecycle
// ════════════════════════════════════════════════════════════════════

ClassicSettingsDialog::~ClassicSettingsDialog() {
    if (hwnd_) {
        DestroyWindow(hwnd_);
        hwnd_ = nullptr;
    }
}

bool ClassicSettingsDialog::Show(HINSTANCE hInstance, HWND parent) {
    hInstance_ = hInstance;

    if (!RegisterWindowClass(hInstance))
        return false;

    LoadSettings();

    // DPI-aware sizing
    dpi_ = 96;
    {
        HDC hdc = GetDC(nullptr);
        if (hdc) {
            dpi_ = static_cast<UINT>(GetDeviceCaps(hdc, LOGPIXELSX));
            ReleaseDC(nullptr, hdc);
        }
    }

    int width  = Dpi(kCompactWidth);
    int height = Dpi(kCompactHeight);

    // Center on screen
    int screenW = GetSystemMetrics(SM_CXSCREEN);
    int screenH = GetSystemMetrics(SM_CYSCREEN);
    int x = (screenW - width) / 2;
    int y = (screenH - height) / 2;

    // Adjust for window chrome (non-client area)
    RECT rc = { 0, 0, width, height };
    DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
    AdjustWindowRect(&rc, style, FALSE);
    int adjWidth  = rc.right - rc.left;
    int adjHeight = rc.bottom - rc.top;
    x = (screenW - adjWidth) / 2;
    y = (screenH - adjHeight) / 2;

    hwnd_ = CreateWindowExW(
        0,
        kClassName,
        L"NexusKey",
        style,
        x, y, adjWidth, adjHeight,
        parent, nullptr, hInstance, this
    );

    if (!hwnd_)
        return false;

    theme_.Init(hwnd_);
    theme_.ApplyWindowAttributes(hwnd_);

    CreateCompactControls();
    PopulateControls();
    SetFontOnAllChildren();

    ShowWindow(hwnd_, SW_SHOW);
    UpdateWindow(hwnd_);

    // Modal message loop
    MSG msg{};
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        if (!IsDialogMessageW(hwnd_, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }

    return true;
}

// ════════════════════════════════════════════════════════════════════
// Window class registration
// ════════════════════════════════════════════════════════════════════

bool ClassicSettingsDialog::RegisterWindowClass(HINSTANCE hInstance) {
    WNDCLASSEXW wc{};
    wc.cbSize        = sizeof(wc);
    wc.style         = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc   = WndProc;
    wc.cbClsExtra    = 0;
    wc.cbWndExtra    = sizeof(void*);
    wc.hInstance     = hInstance;
    wc.hIcon         = LoadIconW(hInstance, MAKEINTRESOURCEW(101));
    wc.hCursor       = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = nullptr;  // We handle WM_ERASEBKGND
    wc.lpszMenuName  = nullptr;
    wc.lpszClassName = kClassName;
    wc.hIconSm      = wc.hIcon;

    ATOM atom = RegisterClassExW(&wc);
    // RegisterClassEx returns 0 if class already registered (ERROR_CLASS_ALREADY_EXISTS)
    return atom != 0 || GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
}

// ════════════════════════════════════════════════════════════════════
// Control creation — Compact mode
// ════════════════════════════════════════════════════════════════════

void ClassicSettingsDialog::CreateCompactControls() {
    int x = Dpi(kPadding);
    int y = Dpi(kPadding);
    int contentW = Dpi(kCompactWidth - kPadding * 2);

    // Row 1: "Kieu go" label + combo
    CreateLabel(L"Ki\x1EC3u g\x00F5", x, y, Dpi(60), Dpi(kLabelHeight), IDC_STATIC_METHOD);
    y += Dpi(kLabelHeight + 2);

    comboMethod_ = CreateCombo(x, y, contentW, Dpi(kComboHeight + 120), IDC_COMBO_METHOD);
    // Populate items
    ComboBox_AddString(comboMethod_, L"Telex");
    ComboBox_AddString(comboMethod_, L"VNI");
    ComboBox_AddString(comboMethod_, L"Simple Telex");
    y += Dpi(kComboHeight + kRowGap);

    // Row 2: "Bang ma" label + combo
    CreateLabel(L"B\x1EA3ng m\x00E3", x, y, Dpi(60), Dpi(kLabelHeight), IDC_STATIC_ENCODING);
    y += Dpi(kLabelHeight + 2);

    comboEncoding_ = CreateCombo(x, y, contentW, Dpi(kComboHeight + 120), IDC_COMBO_ENCODING);
    ComboBox_AddString(comboEncoding_, L"Unicode");
    ComboBox_AddString(comboEncoding_, L"TCVN3 (ABC)");
    ComboBox_AddString(comboEncoding_, L"VNI Windows");
    ComboBox_AddString(comboEncoding_, L"Unicode Compound");
    ComboBox_AddString(comboEncoding_, L"Vietnamese Locale");
    y += Dpi(kComboHeight + kRowGap + 4);

    // Row 3: "Mo rong" checkbox
    checkExpand_ = CreateCheck(
        L"M\x1EDF r\x1ED9ng",
        x, y, contentW, Dpi(kControlHeight),
        IDC_CHECK_EXPAND
    );
    y += Dpi(kControlHeight + kRowGap + 4);

    // Divider
    // (drawn in WM_PAINT via theme_.DrawDivider — store position)
    y += Dpi(8);

    // Row 4: Buttons — "Dong" and "Ket thuc" side by side
    int btnW = (contentW - Dpi(12)) / 2;  // gap between buttons
    btnClose_ = CreateBtn(
        L"\x0110\x00F3ng",  // "Dong"
        x, y, btnW, Dpi(kButtonHeight),
        IDC_BTN_CLOSE, true
    );
    btnExit_ = CreateBtn(
        L"K\x1EBFt th\x00FAc",  // "Ket thuc"
        x + btnW + Dpi(12), y, btnW, Dpi(kButtonHeight),
        IDC_BTN_EXIT, true
    );
}

// ════════════════════════════════════════════════════════════════════
// Control creation — Advanced mode (tab + checkboxes + footer)
// ════════════════════════════════════════════════════════════════════

void ClassicSettingsDialog::CreateAdvancedControls() {
    if (advancedCreated_) return;
    advancedCreated_ = true;

    int x = Dpi(kPadding);
    // Tab control starts below compact section
    int compactBottom = Dpi(kPadding + kLabelHeight + 2 + kComboHeight + kRowGap
                            + kLabelHeight + 2 + kComboHeight + kRowGap + 4
                            + kControlHeight + kRowGap + 4);
    int tabW = Dpi(kAdvancedWidth - kPadding * 2);
    int tabH = Dpi(kAdvancedHeight - kPadding) - compactBottom - Dpi(kButtonHeight + kRowGap + kPadding);

    tabControl_ = CreateWindowExW(
        0, WC_TABCONTROLW, L"",
        WS_CHILD | WS_CLIPSIBLINGS | TCS_OWNERDRAWFIXED,
        x, compactBottom, tabW, tabH,
        hwnd_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_TAB_ADVANCED)),
        hInstance_, nullptr
    );

    // Insert tabs
    TCITEMW tie{};
    tie.mask = TCIF_TEXT;

    tie.pszText = const_cast<wchar_t*>(L"C\x01A1 b\x1EA3n");  // "Co ban"
    TabCtrl_InsertItem(tabControl_, 0, &tie);

    tie.pszText = const_cast<wchar_t*>(L"Ph\x00EDm t\x1EAFt");  // "Phim tat"
    TabCtrl_InsertItem(tabControl_, 1, &tie);

    tie.pszText = const_cast<wchar_t*>(L"H\x1EC7 th\x1ED1ng");  // "He thong"
    TabCtrl_InsertItem(tabControl_, 2, &tie);

    // Get tab content area
    RECT tabRect{};
    GetClientRect(tabControl_, &tabRect);
    TabCtrl_AdjustRect(tabControl_, FALSE, &tabRect);

    // Map tab content rect to parent coordinates
    POINT tabOrigin = { tabRect.left, tabRect.top };
    ClientToScreen(tabControl_, &tabOrigin);
    ScreenToClient(hwnd_, &tabOrigin);

    int contentLeft = tabOrigin.x;
    int contentTop  = tabOrigin.y + Dpi(4);
    int colWidth    = (tabRect.right - tabRect.left - Dpi(8)) / 2;

    // Create checkboxes from SettingMetadata
    // Track row index per (tab, column) for positioning
    int rowCounts[3][2] = {};  // [tab][column]

    for (size_t i = 0; i < kSettingsCount && i < kMaxControls; ++i) {
        const auto& meta = kSettings[i];
        if (meta.type != SettingType::Toggle || meta.win32Id == 0)
            continue;

        int tab = meta.tab;
        int col = meta.column;
        int row = rowCounts[tab][col]++;

        int cx = contentLeft + col * (colWidth + Dpi(8));
        int cy = contentTop + row * Dpi(kControlHeight + kRowGap);

        checkControls_[i] = CreateCheck(
            meta.label,
            cx, cy, colWidth, Dpi(kControlHeight),
            meta.win32Id
        );

        // Initially hidden (ShowTabPage will reveal the right ones)
        ShowWindow(checkControls_[i], SW_HIDE);
    }

    // Footer: "Thiet lap mac dinh" (outlined) + "Luu thay doi" (primary)
    int footerY = compactBottom + tabH + Dpi(kRowGap);
    int btnW = (tabW - Dpi(12)) / 2;

    btnDefaults_ = CreateBtn(
        L"Thi\x1EBFt l\x1EADp m\x1EB7c \x0111\x1ECBnh",  // "Thiet lap mac dinh"
        x, footerY, btnW, Dpi(kButtonHeight),
        IDC_BTN_DEFAULTS, true
    );

    btnSave_ = CreateBtn(
        L"L\x01B0u thay \x0111\x1ED5i",  // "Luu thay doi"
        x + btnW + Dpi(12), footerY, btnW, Dpi(kButtonHeight),
        IDC_BTN_SAVE, true
    );

    // Apply font to newly created controls
    SetFontOnAllChildren();

    // Show first tab
    ShowTabPage(0);
}

// ════════════════════════════════════════════════════════════════════
// Advanced mode toggle
// ════════════════════════════════════════════════════════════════════

void ClassicSettingsDialog::ToggleAdvancedMode(bool expand) {
    isAdvanced_ = expand;

    int width  = Dpi(expand ? kAdvancedWidth : kCompactWidth);
    int height = Dpi(expand ? kAdvancedHeight : kCompactHeight);

    // Adjust for window chrome
    RECT rc = { 0, 0, width, height };
    DWORD style = static_cast<DWORD>(GetWindowLongW(hwnd_, GWL_STYLE));
    AdjustWindowRect(&rc, style, FALSE);
    int adjWidth  = rc.right - rc.left;
    int adjHeight = rc.bottom - rc.top;

    // Keep centered
    RECT wr{};
    GetWindowRect(hwnd_, &wr);
    int cx = (wr.left + wr.right) / 2;
    int cy = (wr.top + wr.bottom) / 2;

    SetWindowPos(hwnd_, nullptr,
        cx - adjWidth / 2, cy - adjHeight / 2,
        adjWidth, adjHeight,
        SWP_NOZORDER | SWP_NOACTIVATE);

    if (expand) {
        CreateAdvancedControls();

        // Show tab + footer, hide compact buttons
        ShowWindow(tabControl_, SW_SHOW);
        ShowWindow(btnDefaults_, SW_SHOW);
        ShowWindow(btnSave_, SW_SHOW);
        ShowWindow(btnClose_, SW_HIDE);
        ShowWindow(btnExit_, SW_HIDE);

        // Re-populate and show current tab
        PopulateControls();
        ShowTabPage(currentTab_);
    } else {
        // Hide tab + footer + all checkboxes, show compact buttons
        if (tabControl_)  ShowWindow(tabControl_, SW_HIDE);
        if (btnDefaults_) ShowWindow(btnDefaults_, SW_HIDE);
        if (btnSave_)     ShowWindow(btnSave_, SW_HIDE);

        for (size_t i = 0; i < kSettingsCount && i < kMaxControls; ++i) {
            if (checkControls_[i])
                ShowWindow(checkControls_[i], SW_HIDE);
        }

        ShowWindow(btnClose_, SW_SHOW);
        ShowWindow(btnExit_, SW_SHOW);
    }

    InvalidateRect(hwnd_, nullptr, TRUE);
}

// ════════════════════════════════════════════════════════════════════
// Settings I/O
// ════════════════════════════════════════════════════════════════════

void ClassicSettingsDialog::LoadSettings() {
    config_       = ConfigManager::LoadOrDefault();
    hotkeyConfig_ = ConfigManager::LoadHotkeyConfigOrDefault();
    systemConfig_ = ConfigManager::LoadSystemConfigOrDefault();

    // Open IPC handles (reused for every save)
    (void)sharedState_.OpenReadWrite();
    (void)configEvent_.Initialize();
}

void ClassicSettingsDialog::PopulateControls() {
    // Dropdowns
    if (comboMethod_)
        ComboBox_SetCurSel(comboMethod_, static_cast<int>(config_.inputMethod));
    if (comboEncoding_)
        ComboBox_SetCurSel(comboEncoding_, static_cast<int>(config_.codeTable));

    // Toggle checkboxes from SettingMetadata
    for (size_t i = 0; i < kSettingsCount && i < kMaxControls; ++i) {
        const auto& meta = kSettings[i];
        if (meta.type != SettingType::Toggle || meta.win32Id == 0)
            continue;

        // Get the bool value from the right config struct via offset
        bool value = false;
        switch (meta.owner) {
            case SettingOwner::Typing:
                value = *reinterpret_cast<const bool*>(
                    reinterpret_cast<const char*>(&config_) + meta.offset);
                break;
            case SettingOwner::Hotkey:
                value = *reinterpret_cast<const bool*>(
                    reinterpret_cast<const char*>(&hotkeyConfig_) + meta.offset);
                break;
            case SettingOwner::System:
                value = *reinterpret_cast<const bool*>(
                    reinterpret_cast<const char*>(&systemConfig_) + meta.offset);
                break;
            default:
                break;
        }

        HWND ctrl = checkControls_[i];
        if (ctrl)
            CheckDlgButton(hwnd_, meta.win32Id, value ? BST_CHECKED : BST_UNCHECKED);
    }
}

void ClassicSettingsDialog::ReadControlValues() {
    // Dropdowns
    if (comboMethod_) {
        int sel = ComboBox_GetCurSel(comboMethod_);
        if (sel >= 0 && sel <= 2)
            config_.inputMethod = static_cast<InputMethod>(sel);
    }
    if (comboEncoding_) {
        int sel = ComboBox_GetCurSel(comboEncoding_);
        if (sel >= 0 && sel <= 4)
            config_.codeTable = static_cast<CodeTable>(sel);
    }

    // Toggle checkboxes
    for (size_t i = 0; i < kSettingsCount && i < kMaxControls; ++i) {
        const auto& meta = kSettings[i];
        if (meta.type != SettingType::Toggle || meta.win32Id == 0)
            continue;

        HWND ctrl = checkControls_[i];
        if (!ctrl) continue;

        bool checked = (IsDlgButtonChecked(hwnd_, meta.win32Id) == BST_CHECKED);

        switch (meta.owner) {
            case SettingOwner::Typing:
                *reinterpret_cast<bool*>(
                    reinterpret_cast<char*>(&config_) + meta.offset) = checked;
                break;
            case SettingOwner::Hotkey:
                *reinterpret_cast<bool*>(
                    reinterpret_cast<char*>(&hotkeyConfig_) + meta.offset) = checked;
                break;
            case SettingOwner::System:
                *reinterpret_cast<bool*>(
                    reinterpret_cast<char*>(&systemConfig_) + meta.offset) = checked;
                break;
            default:
                break;
        }
    }
}

void ClassicSettingsDialog::SaveSettings() {
    ReadControlValues();
    SyncToSharedState();

    // Start/reset deferred TOML save timer
    configDirty_ = true;
    KillTimer(hwnd_, kTimerDeferredSave);
    SetTimer(hwnd_, kTimerDeferredSave, kDeferredSaveDelayMs, nullptr);
}

void ClassicSettingsDialog::SyncToSharedState() {
    if (sharedState_.IsConnected()) {
        SharedState state = sharedState_.Read();
        if (state.IsValid()) {
            state.inputMethod = static_cast<uint8_t>(config_.inputMethod);
            state.spellCheck = config_.spellCheckEnabled ? 1 : 0;
            state.codeTable = static_cast<uint8_t>(config_.codeTable);
            state.SetFeatureFlags(EncodeFeatureFlags(config_));
            state.SetHotkey(hotkeyConfig_);
            state.configGeneration++;
            sharedState_.Write(state);
        }
    }

    if (configEvent_.IsValid()) {
        configEvent_.Signal();
    }
}

void ClassicSettingsDialog::SaveToToml() {
    std::wstring path = ConfigManager::GetConfigPath();
    if (!ConfigManager::SaveToFile(path, config_)) {
        NEXTKEY_LOG(L"[ClassicSettings] Failed to save typing config");
    }
    (void)ConfigManager::SaveHotkeyConfig(path, hotkeyConfig_);
    (void)ConfigManager::SaveSystemConfig(path, systemConfig_);

    configDirty_ = false;

    // Bump configGeneration again so HookEngine reloads TOML-only fields
    if (sharedState_.IsConnected()) {
        SharedState state = sharedState_.Read();
        if (state.IsValid()) {
            state.configGeneration++;
            sharedState_.Write(state);
        }
    }

    if (configEvent_.IsValid()) {
        configEvent_.Signal();
    }
}

void ClassicSettingsDialog::ResetToDefaults() {
    config_       = TypingConfig{};
    hotkeyConfig_ = HotkeyConfig{};
    systemConfig_ = SystemConfig{};
    PopulateControls();
    SaveSettings();
}

// ════════════════════════════════════════════════════════════════════
// Tab management
// ════════════════════════════════════════════════════════════════════

void ClassicSettingsDialog::OnTabChange() {
    if (!tabControl_) return;
    int tab = TabCtrl_GetCurSel(tabControl_);
    if (tab >= 0 && tab <= 2) {
        currentTab_ = tab;
        ShowTabPage(tab);
    }
}

void ClassicSettingsDialog::ShowTabPage(int tabIndex) {
    for (size_t i = 0; i < kSettingsCount && i < kMaxControls; ++i) {
        HWND ctrl = checkControls_[i];
        if (!ctrl) continue;

        const auto& meta = kSettings[i];
        ShowWindow(ctrl, (meta.tab == tabIndex) ? SW_SHOW : SW_HIDE);
    }
}

// ════════════════════════════════════════════════════════════════════
// Command handler
// ════════════════════════════════════════════════════════════════════

void ClassicSettingsDialog::OnCommand(WPARAM wParam, LPARAM lParam) {
    UINT code = HIWORD(wParam);
    UINT id   = LOWORD(wParam);

    switch (id) {
        case IDC_CHECK_EXPAND: {
            bool expand = (IsDlgButtonChecked(hwnd_, IDC_CHECK_EXPAND) == BST_CHECKED);
            ToggleAdvancedMode(expand);
            return;
        }

        case IDC_BTN_CLOSE:
            DestroyWindow(hwnd_);
            return;

        case IDC_BTN_EXIT:
            // Save any pending changes, then quit the app
            if (configDirty_) {
                KillTimer(hwnd_, kTimerDeferredSave);
                SaveToToml();
            }
            DestroyWindow(hwnd_);
            PostQuitMessage(0);
            return;

        case IDC_BTN_SAVE:
            // Force immediate TOML save
            KillTimer(hwnd_, kTimerDeferredSave);
            ReadControlValues();
            SyncToSharedState();
            SaveToToml();
            return;

        case IDC_BTN_DEFAULTS:
            ResetToDefaults();
            return;

        case IDC_COMBO_METHOD:
        case IDC_COMBO_ENCODING:
            if (code == CBN_SELCHANGE)
                SaveSettings();
            return;

        default:
            break;
    }

    // Check if this is a BN_CLICKED on a setting checkbox
    if (code == BN_CLICKED) {
        const auto* meta = FindSettingByControlId(static_cast<uint16_t>(id));
        if (meta) {
            SaveSettings();
        }
    }
}

// ════════════════════════════════════════════════════════════════════
// Control creation helpers
// ════════════════════════════════════════════════════════════════════

HWND ClassicSettingsDialog::CreateLabel(const wchar_t* text, int x, int y, int w, int h, UINT id) {
    return CreateWindowExW(
        0, L"STATIC", text,
        WS_CHILD | WS_VISIBLE | SS_LEFT,
        x, y, w, h,
        hwnd_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
        hInstance_, nullptr
    );
}

HWND ClassicSettingsDialog::CreateCombo(int x, int y, int w, int h, UINT id) {
    return CreateWindowExW(
        0, L"COMBOBOX", L"",
        WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL,
        x, y, w, h,
        hwnd_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
        hInstance_, nullptr
    );
}

HWND ClassicSettingsDialog::CreateCheck(const wchar_t* text, int x, int y, int w, int h, UINT id) {
    return CreateWindowExW(
        0, L"BUTTON", text,
        WS_CHILD | BS_AUTOCHECKBOX,
        x, y, w, h,
        hwnd_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
        hInstance_, nullptr
    );
}

HWND ClassicSettingsDialog::CreateBtn(const wchar_t* text, int x, int y, int w, int h, UINT id, bool ownerDraw) {
    DWORD style = WS_CHILD | WS_VISIBLE;
    if (ownerDraw) style |= BS_OWNERDRAW;

    return CreateWindowExW(
        0, L"BUTTON", text,
        style,
        x, y, w, h,
        hwnd_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
        hInstance_, nullptr
    );
}

void ClassicSettingsDialog::SetFontOnAllChildren() {
    EnumChildWindows(hwnd_, SetFontProc,
        reinterpret_cast<LPARAM>(theme_.Fonts().body));
}

BOOL CALLBACK ClassicSettingsDialog::SetFontProc(HWND hwnd, LPARAM lParam) {
    auto* font = reinterpret_cast<HFONT>(lParam);
    if (font)
        SendMessageW(hwnd, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
    return TRUE;
}

int ClassicSettingsDialog::Dpi(int value) const noexcept {
    return MulDiv(value, static_cast<int>(dpi_), 96);
}

// ════════════════════════════════════════════════════════════════════
// Window procedure
// ════════════════════════════════════════════════════════════════════

LRESULT CALLBACK ClassicSettingsDialog::WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    ClassicSettingsDialog* self = nullptr;

    if (msg == WM_NCCREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
        self = reinterpret_cast<ClassicSettingsDialog*>(cs->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        self->hwnd_ = hwnd;
    } else {
        self = reinterpret_cast<ClassicSettingsDialog*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }

    if (!self)
        return DefWindowProcW(hwnd, msg, wParam, lParam);

    switch (msg) {
        case WM_COMMAND:
            self->OnCommand(wParam, lParam);
            return 0;

        case WM_NOTIFY: {
            auto* hdr = reinterpret_cast<NMHDR*>(lParam);
            if (hdr->idFrom == IDC_TAB_ADVANCED && hdr->code == TCN_SELCHANGE) {
                self->OnTabChange();
            }
            return 0;
        }

        case WM_ERASEBKGND: {
            HDC hdc = reinterpret_cast<HDC>(wParam);
            RECT rc{};
            GetClientRect(hwnd, &rc);
            FillRect(hdc, &rc, self->theme_.BrushBackground());
            return 1;  // We handled it
        }

        case WM_CTLCOLORSTATIC:
            return reinterpret_cast<LRESULT>(
                self->theme_.OnCtlColorStatic(
                    reinterpret_cast<HDC>(wParam),
                    reinterpret_cast<HWND>(lParam)));

        case WM_CTLCOLORBTN:
            return reinterpret_cast<LRESULT>(
                self->theme_.OnCtlColorBtn(
                    reinterpret_cast<HDC>(wParam),
                    reinterpret_cast<HWND>(lParam)));

        case WM_CTLCOLOREDIT:
            return reinterpret_cast<LRESULT>(
                self->theme_.OnCtlColorEdit(
                    reinterpret_cast<HDC>(wParam),
                    reinterpret_cast<HWND>(lParam)));

        case WM_CTLCOLORLISTBOX:
            return reinterpret_cast<LRESULT>(
                self->theme_.OnCtlColorListBox(
                    reinterpret_cast<HDC>(wParam),
                    reinterpret_cast<HWND>(lParam)));

        case WM_DRAWITEM: {
            auto* dis = reinterpret_cast<DRAWITEMSTRUCT*>(lParam);
            UINT ctrlId = static_cast<UINT>(wParam);

            // Tab control owner-draw
            if (ctrlId == IDC_TAB_ADVANCED) {
                self->theme_.DrawTabItem(dis);
                return TRUE;
            }

            // Owner-draw buttons
            switch (ctrlId) {
                case IDC_BTN_CLOSE:
                case IDC_BTN_DEFAULTS:
                    self->theme_.DrawButton(dis, false);
                    return TRUE;
                case IDC_BTN_EXIT:
                case IDC_BTN_SAVE:
                    self->theme_.DrawButton(dis, true);
                    return TRUE;
            }
            break;
        }

        case WM_PAINT: {
            PAINTSTRUCT ps{};
            HDC hdc = BeginPaint(hwnd, &ps);

            // Draw divider above compact buttons (when visible)
            if (!self->isAdvanced_) {
                int divY = self->Dpi(kPadding + kLabelHeight + 2 + kComboHeight + kRowGap
                                     + kLabelHeight + 2 + kComboHeight + kRowGap + 4
                                     + kControlHeight + kRowGap + 4) - self->Dpi(4);
                int divX = self->Dpi(kPadding);
                int divW = self->Dpi(kCompactWidth - kPadding * 2);
                self->theme_.DrawDivider(hdc, divX, divY, divW);
            }

            EndPaint(hwnd, &ps);
            return 0;
        }

        case WM_SETTINGCHANGE:
            if (self->theme_.OnSettingChange(lParam)) {
                // Theme changed (light ↔ dark)
                self->theme_.ApplyWindowAttributes(hwnd);
                InvalidateRect(hwnd, nullptr, TRUE);
            }
            return 0;

        case WM_TIMER:
            if (wParam == kTimerDeferredSave) {
                KillTimer(hwnd, kTimerDeferredSave);
                if (self->configDirty_)
                    self->SaveToToml();
            }
            return 0;

        case WM_CLOSE:
            // Save pending changes before closing
            if (self->configDirty_) {
                KillTimer(hwnd, kTimerDeferredSave);
                self->SaveToToml();
            }
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

} // namespace NextKey::Classic
