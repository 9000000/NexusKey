// NexusKey Classic — Settings Dialog Implementation
// Compact (Unikey-style) + Advanced (EVKey-style) modes
// SPDX-License-Identifier: GPL-3.0-only

#include "ClassicSettingsDialog.h"
#include "core/config/ConfigManager.h"
#include "core/Debug.h"
#include "system/StartupHelper.h"

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

    DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
    int screenW = GetSystemMetrics(SM_CXSCREEN);
    int screenH = GetSystemMetrics(SM_CYSCREEN);

    hwnd_ = CreateWindowExW(
        0,
        kClassName,
        L"NexusKey",
        style,
        CW_USEDEFAULT, CW_USEDEFAULT, 400, 300,  // temporary size
        parent, nullptr, hInstance, this
    );

    if (!hwnd_)
        return false;

    // Get real DPI from the window's monitor
    dpi_ = 96;
    auto pfnGetDpiForWindow = reinterpret_cast<UINT(WINAPI*)(HWND)>(
        GetProcAddress(GetModuleHandleW(L"user32.dll"), "GetDpiForWindow"));
    if (pfnGetDpiForWindow) {
        dpi_ = pfnGetDpiForWindow(hwnd_);
    } else {
        HDC hdc = GetDC(hwnd_);
        if (hdc) {
            dpi_ = static_cast<UINT>(GetDeviceCaps(hdc, LOGPIXELSX));
            ReleaseDC(hwnd_, hdc);
        }
    }

    // Resize to correct DPI-scaled full size and center
    int width  = Dpi(kAdvancedWidth);
    int height = Dpi(kAdvancedHeight);
    RECT rc = { 0, 0, width, height };
    AdjustWindowRect(&rc, style, FALSE);
    int adjWidth  = rc.right - rc.left;
    int adjHeight = rc.bottom - rc.top;
    int x = (screenW - adjWidth) / 2;
    int y = (screenH - adjHeight) / 2;
    SetWindowPos(hwnd_, nullptr, x, y, adjWidth, adjHeight, SWP_NOZORDER | SWP_NOACTIVATE);

    theme_.Init(hwnd_);
    theme_.ApplyWindowAttributes(hwnd_);

    CreateCompactControls();
    CreateAdvancedControls();
    PopulateControls();
    SetFontOnAllChildren();
    theme_.ThemeAllChildren(hwnd_);

    if (tabControl_) ShowWindow(tabControl_, SW_SHOW);
    ShowTabPage(currentTab_);

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
    return atom != 0 || GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
}

// ════════════════════════════════════════════════════════════════════
// Control creation — Compact mode
// ════════════════════════════════════════════════════════════════════

void ClassicSettingsDialog::CreateCompactControls() {
    int offset = Dpi(8);
    int gbX = Dpi(8);

    CreateLabel(L"  C\x01A1 b\x1EA3n  ", gbX + Dpi(6), Dpi(2) + offset, Dpi(90), Dpi(18), 2999);

    int x1 = Dpi(kPadding);
    int y = Dpi(kPadding + 14) + offset;
    int contentW = Dpi(kAdvancedWidth - kPadding * 2);

    int colW = (contentW - Dpi(kPadding)) / 2;
    int col2X = x1 + colW + Dpi(kPadding);

    // Row 1: "Kieu go" and "Bang ma" side by side
    CreateLabel(L"Ki\x1EC3u g\x00F5", x1, y, colW, Dpi(kLabelHeight), IDC_STATIC_METHOD);
    CreateLabel(L"B\x1EA3ng m\x00E3", col2X, y, colW, Dpi(kLabelHeight), IDC_STATIC_ENCODING);
    y += Dpi(kLabelHeight + kRowGap);

    comboMethod_ = CreateCombo(x1, y, colW, Dpi(kComboHeight + 120), IDC_COMBO_METHOD);
    ComboBox_AddString(comboMethod_, L"Telex");
    ComboBox_AddString(comboMethod_, L"VNI");
    ComboBox_AddString(comboMethod_, L"Simple Telex");

    comboEncoding_ = CreateCombo(col2X, y, colW, Dpi(kComboHeight + 120), IDC_COMBO_ENCODING);
    ComboBox_AddString(comboEncoding_, L"Unicode");
    ComboBox_AddString(comboEncoding_, L"TCVN3 (ABC)");
    ComboBox_AddString(comboEncoding_, L"VNI Windows");
    ComboBox_AddString(comboEncoding_, L"Unicode t\x1ED5 h\x1EE3p");
    ComboBox_AddString(comboEncoding_, L"Vi\x1EC7t (CP 1258)");

    y += Dpi(kComboHeight + kSectionGap);

    // Row 2: "Phim tat"
    CreateLabel(L"Ph\x00EDm chuy\x1EC3n (Vi\x1EC7t/Anh)", x1, y, contentW, Dpi(kLabelHeight), IDC_STATIC_SWITCHKEY);
    y += Dpi(kLabelHeight + kRowGap);

    int btnW = Dpi(55);
    int cx = x1;
    for (size_t i = 0; i < kSettingsCount && i < kMaxControls; ++i) {
        if (kSettings[i].owner == SettingOwner::Hotkey) {
            checkControls_[i] = CreateCheck(kSettings[i].label, cx, y, btnW, Dpi(kControlHeight), kSettings[i].win32Id);
            cx += btnW + Dpi(4);
        }
    }
    
    int editH = Dpi(16);
    int editYOffset = (Dpi(kControlHeight) - editH) / 2;
    editHotkey_ = CreateEdit(cx + Dpi(4), y + editYOffset, Dpi(24), editH, IDC_EDIT_SWITCH_KEY);
    SendMessageW(editHotkey_, EM_SETLIMITTEXT, 1, 0);
    y += Dpi(kControlHeight + kSectionGap) + Dpi(4);
}

// ════════════════════════════════════════════════════════════════════
// Control creation — Advanced mode (tab + checkboxes)
// ════════════════════════════════════════════════════════════════════

static LRESULT CALLBACK TabSubclassProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam, UINT_PTR /*uIdSubclass*/, DWORD_PTR dwRefData) {
    auto* self = reinterpret_cast<ClassicSettingsDialog*>(dwRefData);

    if (uMsg == WM_ERASEBKGND || uMsg == WM_PRINTCLIENT) {
        HDC hdc = reinterpret_cast<HDC>(wParam);
        RECT rc;
        GetClientRect(hWnd, &rc);
        FillRect(hdc, &rc, self->theme().BrushBackground());
        return 1;
    }

    if (uMsg == WM_PAINT) {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hWnd, &ps);

        RECT rcClient;
        GetClientRect(hWnd, &rcClient);

        FillRect(hdc, &rcClient, self->theme().BrushBackground());

        int activeTab = TabCtrl_GetCurSel(hWnd);
        int tabCount = TabCtrl_GetItemCount(hWnd);

        HPEN pen = CreatePen(PS_SOLID, 1, self->theme().Colors().border);
        HBRUSH oldBr = (HBRUSH)SelectObject(hdc, GetStockObject(NULL_BRUSH));
        HPEN oldPen = (HPEN)SelectObject(hdc, pen);

        HFONT fontActive = self->theme().Fonts().bodyBold;
        HFONT fontInactive = self->theme().Fonts().body;
        SetBkMode(hdc, TRANSPARENT);

        RECT activeRc = {0};
        int topY = 0;
        if (tabCount > 0) {
            TabCtrl_GetItemRect(hWnd, 0, &activeRc);
            activeRc.left = 0;
            topY = activeRc.bottom;
            if (activeTab != -1) {
                TabCtrl_GetItemRect(hWnd, activeTab, &activeRc);
                if (activeTab == 0) activeRc.left = 0;
            }
        }

        RoundRect(hdc, rcClient.left, topY, rcClient.right, rcClient.bottom, 12, 12);

        if (activeTab == 0) {
            RECT cornerRc = { rcClient.left, topY, rcClient.left + 6, topY + 6 };
            FillRect(hdc, &cornerRc, self->theme().BrushBackground());
            
            MoveToEx(hdc, rcClient.left, topY, nullptr);
            LineTo(hdc, rcClient.left, topY + 7);
        }

        if (activeTab != -1) {
            HPEN eraPen = CreatePen(PS_SOLID, 1, self->theme().Colors().background);
            HPEN eraOld = (HPEN)SelectObject(hdc, eraPen);
            MoveToEx(hdc, activeRc.left + 1, topY, nullptr);
            LineTo(hdc, activeRc.right, topY);
            SelectObject(hdc, eraOld);
            DeleteObject(eraPen);
        }

        for (int i = 0; i < tabCount; ++i) {
            RECT rcTab;
            TabCtrl_GetItemRect(hWnd, i, &rcTab);
            if (i == 0) rcTab.left = 0;
            
            bool isSelected = (i == activeTab);
            
            HRGN hRgn = CreateRectRgn(rcTab.left, rcTab.top, rcTab.right, rcTab.bottom + (isSelected ? 1 : 0));
            SelectClipRgn(hdc, hRgn);

            RoundRect(hdc, rcTab.left, rcTab.top, rcTab.right, rcTab.bottom + 12, 12, 12);

            SelectClipRgn(hdc, NULL);
            DeleteObject(hRgn);

            wchar_t text[64]{};
            TCITEMW tci = { TCIF_TEXT, 0, 0, text, 64 };
            SendMessageW(hWnd, TCM_GETITEMW, i, reinterpret_cast<LPARAM>(&tci));

            SetTextColor(hdc, isSelected ? self->theme().Colors().text : self->theme().Colors().textSecondary);
            SelectObject(hdc, isSelected ? fontActive : fontInactive);
            DrawTextW(hdc, text, -1, &rcTab, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        }

        SelectObject(hdc, oldBr);
        SelectObject(hdc, oldPen);
        DeleteObject(pen);
        EndPaint(hWnd, &ps);
        return 0;
    }

    return DefSubclassProc(hWnd, uMsg, wParam, lParam);
}

void ClassicSettingsDialog::CreateAdvancedControls() {
    if (advancedCreated_) return;
    advancedCreated_ = true;

    int x = Dpi(8);
    // Tab control starts below compact section
    RECT editRc;
    GetWindowRect(editHotkey_, &editRc);
    MapWindowPoints(HWND_DESKTOP, hwnd_, reinterpret_cast<LPPOINT>(&editRc), 2);
    int compactBottom = editRc.bottom + Dpi(kSectionGap + 12);

    int tabW = Dpi(kAdvancedWidth - 16);
    int tabH = Dpi(kAdvancedHeight - 8) - compactBottom;

    tabControl_ = CreateWindowExW(
        0, WC_TABCONTROLW, L"",
        WS_CHILD | WS_CLIPSIBLINGS | TCS_FIXEDWIDTH,
        x, compactBottom, tabW, tabH,
        hwnd_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_TAB_ADVANCED)),
        hInstance_, nullptr
    );

    SetWindowSubclass(tabControl_, TabSubclassProc, 1, reinterpret_cast<DWORD_PTR>(this));

    // Insert tabs
    TCITEMW tie{};
    tie.mask = TCIF_TEXT;

    tie.pszText = const_cast<wchar_t*>(L"\xD83D\xDCDD B\x1ED9 G\x00F5"); //Bộ gõ
    TabCtrl_InsertItem(tabControl_, 0, &tie);

    tie.pszText = const_cast<wchar_t*>(L"\xD83D\xDE80 G\x00F5 T\x1EAFt"); //Gõ tắt
    TabCtrl_InsertItem(tabControl_, 1, &tie);

    tie.pszText = const_cast<wchar_t*>(L"\x2699\xFE0F H\x1EC7 th\x1ED1ng"); //Hệ thống
    TabCtrl_InsertItem(tabControl_, 2, &tie);

    TabCtrl_SetItemSize(tabControl_, Dpi(120), Dpi(kTabHeight));

    // Get tab content area
    RECT tabRect{};
    GetClientRect(tabControl_, &tabRect);
    TabCtrl_AdjustRect(tabControl_, FALSE, &tabRect);

    tabRect.left += Dpi(12);
    tabRect.right -= Dpi(12);
    tabRect.top += Dpi(8);

    // Map tab content rect to parent coordinates
    POINT tabOrigin = { tabRect.left, tabRect.top };
    ClientToScreen(tabControl_, &tabOrigin);
    ScreenToClient(hwnd_, &tabOrigin);

    int contentLeft = tabOrigin.x;
    int contentTop  = tabOrigin.y;
    int colWidth    = (tabRect.right - tabRect.left - Dpi(8)) / 2;

    // Create checkboxes from SettingMetadata
    int rowCounts[3][2] = {};  // [tab][column]

    for (size_t i = 0; i < kSettingsCount && i < kMaxControls; ++i) {
        const auto& meta = kSettings[i];
        if (meta.win32Id == 0 || meta.owner == SettingOwner::Hotkey)
            continue;

        int tab = meta.tab;
        int col = meta.column;
        int row = rowCounts[tab][col]++;

        // If it's an inline action button
        bool isInlineAction = (meta.type == SettingType::Action && wcscmp(meta.label, L"...") == 0);
        if (isInlineAction) {
            rowCounts[tab][col]--; // stay on the same visual row
            row--; // go back to the row we just incremented past
        }

        int cx = contentLeft + col * (colWidth + Dpi(8));
        int cy = contentTop + row * Dpi(kControlHeight + kRowGap);

        if (meta.type == SettingType::Toggle) {
            bool hasInlineNext = ((i + 1 < kSettingsCount) && kSettings[i+1].type == SettingType::Action && wcscmp(kSettings[i+1].label, L"...") == 0);
            int checkW = hasInlineNext ? colWidth - Dpi(44) : colWidth;
            checkControls_[i] = CreateCheck(meta.label, cx, cy, checkW, Dpi(kControlHeight), meta.win32Id);
        } else if (meta.type == SettingType::Action) {
            if (isInlineAction) {
                int btnW = Dpi(40);
                int inlineCx = cx + colWidth - btnW;
                checkControls_[i] = CreateBtn(meta.label, inlineCx, cy, btnW, Dpi(kControlHeight), meta.win32Id);
            } else {
                checkControls_[i] = CreateBtn(meta.label, cx, cy, colWidth, Dpi(kControlHeight), meta.win32Id);
            }
        } else if (meta.type == SettingType::Dropdown) {
            int lblW = Dpi(90);
            int comboW = colWidth - lblW - Dpi(4);
            HWND lbl = CreateLabel(meta.label, cx, cy + Dpi(4), lblW, Dpi(kControlHeight), 0);
            extraControls_[i] = lbl;
            HWND combo = CreateCombo(cx + lblW + Dpi(4), cy, comboW, Dpi(kComboHeight + 60), meta.win32Id);
            if (wcscmp(meta.id, L"custom-icon-style") == 0) {
                ComboBox_AddString(combo, L"M\x00E0u m\x1EB7" L"c \x0111\x1ECBnh");
                ComboBox_AddString(combo, L"N\x1EC1n t\x1ED1i");
                ComboBox_AddString(combo, L"N\x1EC1n s\x00E1ng");
                ComboBox_AddString(combo, L"T\x1EF1 ch\x1ECDn");
            }
            checkControls_[i] = combo;
        }
        
        if (checkControls_[i]) {
            ShowWindow(checkControls_[i], SW_HIDE);
        }
        if (extraControls_[i]) {
            ShowWindow(extraControls_[i], SW_HIDE);
        }
    }

    // Apply font to newly created controls
    SetFontOnAllChildren();

    // Show first tab
    ShowTabPage(0);
}



// ════════════════════════════════════════════════════════════════════
// Settings I/O
// ════════════════════════════════════════════════════════════════════

void ClassicSettingsDialog::LoadSettings() {
    config_       = ConfigManager::LoadOrDefault();
    hotkeyConfig_ = ConfigManager::LoadHotkeyConfigOrDefault();
    systemConfig_ = ConfigManager::LoadSystemConfigOrDefault();

    (void)sharedState_.OpenReadWrite();
    (void)configEvent_.Initialize();
}

void ClassicSettingsDialog::PopulateControls() {
    if (comboMethod_)
        ComboBox_SetCurSel(comboMethod_, static_cast<int>(config_.inputMethod));
    if (comboEncoding_)
        ComboBox_SetCurSel(comboEncoding_, static_cast<int>(config_.codeTable));

    for (size_t i = 0; i < kSettingsCount && i < kMaxControls; ++i) {
        const auto& meta = kSettings[i];
        if (meta.win32Id == 0)
            continue;

        HWND ctrl = checkControls_[i];
        if (!ctrl) continue;

        if (meta.type == SettingType::Toggle) {
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
            CheckDlgButton(hwnd_, meta.win32Id, value ? BST_CHECKED : BST_UNCHECKED);
        } else if (meta.type == SettingType::Dropdown) {
            uint8_t value = 0;
            switch (meta.owner) {
                case SettingOwner::System:
                    value = *reinterpret_cast<const uint8_t*>(
                        reinterpret_cast<const char*>(&systemConfig_) + meta.offset);
                    break;
                default:
                    break;
            }
            ComboBox_SetCurSel(ctrl, static_cast<int>(value));
        }
    }

    if (editHotkey_) {
        if (hotkeyConfig_.key) {
            wchar_t buf[2] = { hotkeyConfig_.key, 0 };
            SetWindowTextW(editHotkey_, buf);
        } else {
            SetWindowTextW(editHotkey_, L"");
        }
    }
}

void ClassicSettingsDialog::ReadControlValues() {
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

    for (size_t i = 0; i < kSettingsCount && i < kMaxControls; ++i) {
        const auto& meta = kSettings[i];
        if (meta.win32Id == 0)
            continue;

        HWND ctrl = checkControls_[i];
        if (!ctrl) continue;

        if (meta.type == SettingType::Toggle) {
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
        } else if (meta.type == SettingType::Dropdown) {
            int sel = ComboBox_GetCurSel(ctrl);
            if (sel >= 0) {
                uint8_t value = static_cast<uint8_t>(sel);
                switch (meta.owner) {
                    case SettingOwner::System:
                        *reinterpret_cast<uint8_t*>(
                            reinterpret_cast<char*>(&systemConfig_) + meta.offset) = value;
                        break;
                    default:
                        break;
                }
            }
        }
    }

    if (editHotkey_) {
        wchar_t buf[2] = {0};
        GetWindowTextW(editHotkey_, buf, 2);
        wchar_t key = buf[0];
        if (key >= L'a' && key <= L'z') key = key - L'a' + L'A';
        hotkeyConfig_.key = key;
    }
}

void ClassicSettingsDialog::SaveSettings() {
    ReadControlValues();
    SyncToSharedState();

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
        if (meta.owner == SettingOwner::Hotkey) continue;

        int showCmd = (meta.tab == tabIndex) ? SW_SHOW : SW_HIDE;
        ShowWindow(ctrl, showCmd);
        
        if (extraControls_[i]) {
            ShowWindow(extraControls_[i], showCmd);
        }
    }
}

// ════════════════════════════════════════════════════════════════════
// Command handler
// ════════════════════════════════════════════════════════════════════

void ClassicSettingsDialog::OnCommand(WPARAM wParam, LPARAM lParam) {
    UINT code = HIWORD(wParam);
    UINT id   = LOWORD(wParam);

    switch (id) {

        case IDC_BTN_CLOSE:
            DestroyWindow(hwnd_);
            return;

        case IDC_BTN_EXIT:
            if (configDirty_) {
                KillTimer(hwnd_, kTimerDeferredSave);
                SaveToToml();
            }
            DestroyWindow(hwnd_);
            PostQuitMessage(0);
            return;

        case IDC_COMBO_METHOD:
        case IDC_COMBO_ENCODING:
            if (code == CBN_SELCHANGE) {
                SaveSettings();
            } else if (code == CBN_DROPDOWN && theme_.IsDark()) {
                BOOL anim = FALSE;
                SystemParametersInfoW(SPI_GETCOMBOBOXANIMATION, 0, &anim, 0);
                if (anim) {
                    SystemParametersInfoW(SPI_SETCOMBOBOXANIMATION, 0, (PVOID)FALSE, 0);
                    SetPropW(reinterpret_cast<HWND>(lParam), L"WasAnim", reinterpret_cast<HANDLE>(1));
                }
            } else if (code == CBN_CLOSEUP) {
                if (GetPropW(reinterpret_cast<HWND>(lParam), L"WasAnim")) {
                    SystemParametersInfoW(SPI_SETCOMBOBOXANIMATION, 0, (PVOID)TRUE, 0);
                    RemovePropW(reinterpret_cast<HWND>(lParam), L"WasAnim");
                }
            }
            return;

        default:
            break;
    }

    // Check if this is a BN_CLICKED on a setting checkbox/button or CBN_SELCHANGE
    if (code == BN_CLICKED || code == CBN_SELCHANGE) {
        const auto* meta = FindSettingByControlId(static_cast<uint16_t>(id));
        if (meta) {
            if (meta->type == SettingType::Action) {
                OnActionButton(meta->win32Id);
            } else {
                SaveSettings();
                // System toggles have side effects beyond config save
                if (meta->owner == SettingOwner::System && meta->type == SettingType::Toggle) {
                    bool checked = (IsDlgButtonChecked(hwnd_, meta->win32Id) == BST_CHECKED);
                    OnSystemToggle(meta->id, checked);
                }
            }
        }
    }

    // Check if this is an EN_CHANGE on edit control
    if (code == EN_CHANGE && id == IDC_EDIT_SWITCH_KEY) {
        SaveSettings();
    }
}

// ════════════════════════════════════════════════════════════════════
// Action button handlers
// ════════════════════════════════════════════════════════════════════

void ClassicSettingsDialog::OnActionButton(uint16_t controlId) {
    switch (controlId) {
        case IDC_BTN_SMART_SWITCH:
            MessageBoxW(hwnd_,
                L"T\x00EDnh n\x0103ng n\x00E0y s\x1EBD l\x01B0u ch\x1EBF \x0111\x1ED9 g\x00F5 (Vi\x1EC7t/Anh) "
                L"cho t\x1EEBng \x1EE9ng d\x1EE5ng ri\x00EAng.\n\n"
                L"Khi b\x1EA1n chuy\x1EC3n qua l\x1EA1i gi\x1EEFa c\x00E1c app, "
                L"NexusKey s\x1EBD t\x1EF1 \x0111\x1ED9ng kh\x00F4i ph\x1EE5c ch\x1EBF \x0111\x1ED9 g\x00F5 \x0111\x00E3 d\x00F9ng tr\x01B0\x1EDBc \x0111\x00F3.",
                L"L\x01B0u ch\x1EBF \x0111\x1ED9 g\x00F5 theo app",
                MB_ICONINFORMATION);
            break;

        case IDC_BTN_EXCLUDE_APPS:
            MessageBoxW(hwnd_,
                L"T\x00EDnh n\x0103ng n\x00E0y cho ph\x00E9p b\x1EA1n ch\x1ECDn nh\x1EEFng \x1EE9ng d\x1EE5ng "
                L"s\x1EBD t\x1EF1 \x0111\x1ED9ng t\x1EAFt g\x00F5 ti\x1EBFng Vi\x1EC7t.\n\n"
                L"V\x00ED d\x1EE5: Game, IDE code...\n\n"
                L"\x0110\x1EC3 ch\x1EC9nh s\x1EEDa danh s\x00E1ch, m\x1EDF file config t\x1EA1i:\n"
                L"%APPDATA%\\NexusKey\\config.toml",
                L"T\x1EAFt ti\x1EBFng Vi\x1EC7t theo app",
                MB_ICONINFORMATION);
            break;

        case IDC_BTN_MACRO_TABLE:
            MessageBoxW(hwnd_,
                L"B\x1EA3ng g\x00F5 t\x1EAFt cho ph\x00E9p b\x1EA1n \x0111\x1ECBnh ngh\x0129a c\x00E1c ph\x00EDm t\x1EAFt.\n\n"
                L"V\x00ED d\x1EE5: \"btv\" \x2192 \"b\x00E1o tu\x1ED5i tr\x1EBB\"\n\n"
                L"\x0110\x1EC3 ch\x1EC9nh s\x1EEDa, m\x1EDF file:\n"
                L"%APPDATA%\\NexusKey\\macros.txt",
                L"B\x1EA3ng g\x00F5 t\x1EAFt",
                MB_ICONINFORMATION);
            break;
    }
}

// ════════════════════════════════════════════════════════════════════
// System toggle side effects
// ════════════════════════════════════════════════════════════════════

void ClassicSettingsDialog::OnSystemToggle(const wchar_t* id, bool value) {
    if (wcscmp(id, L"run-startup") == 0) {
        RegisterRunOnStartup(value, systemConfig_.runAsAdmin);
    }
    else if (wcscmp(id, L"run-admin") == 0) {
        if (systemConfig_.runAtStartup) {
            RegisterRunOnStartup(true, value);
        }
    }
    else if (wcscmp(id, L"desktop-shortcut") == 0) {
        SetDesktopShortcut(value);
    }
    // floating-icon, show-on-startup, check-update: saved to config,
    // main_lite.cpp reads updated config when dialog closes.
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
        WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
        x, y, w, h,
        hwnd_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
        hInstance_, nullptr
    );
}

HWND ClassicSettingsDialog::CreateEdit(int x, int y, int w, int h, UINT id) {
    return CreateWindowExW(
        0, L"EDIT", L"",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_UPPERCASE | ES_CENTER | ES_AUTOHSCROLL,
        x, y, w, h,
        hwnd_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
        hInstance_, nullptr
    );
}

HWND ClassicSettingsDialog::CreateBtn(const wchar_t* text, int x, int y, int w, int h, UINT id, bool isPrimary) {
    DWORD style = WS_CHILD | WS_VISIBLE | WS_TABSTOP;
    style |= isPrimary ? BS_DEFPUSHBUTTON : BS_PUSHBUTTON;

    return CreateWindowExW(
        0, L"BUTTON", text,
        style,
        x, y, w, h,
        hwnd_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
        hInstance_, nullptr
    );
}

void ClassicSettingsDialog::SetFontOnAllChildren() {
    EnumChildWindows(hwnd_, SetFontProc, reinterpret_cast<LPARAM>(this));
}

BOOL CALLBACK ClassicSettingsDialog::SetFontProc(HWND hwnd, LPARAM lParam) {
    auto* self = reinterpret_cast<ClassicSettingsDialog*>(lParam);
    if (!self) return TRUE;

    HFONT font = self->theme_.Fonts().body;
    if (GetDlgCtrlID(hwnd) == 2999) {
        font = self->theme_.Fonts().header;
    }

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

        case WM_ERASEBKGND:
        case WM_PRINTCLIENT: {
            HDC hdc = reinterpret_cast<HDC>(wParam);
            RECT rc{};
            GetClientRect(hwnd, &rc);
            FillRect(hdc, &rc, self->theme_.BrushBackground());
            return 1;
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

            // Only tab control is owner-drawn now
            if (ctrlId == IDC_TAB_ADVANCED) {
                self->theme_.DrawTabItem(dis);
                return TRUE;
            }
            break;
        }

        case WM_PAINT: {
            PAINTSTRUCT ps{};
            HDC hdc = BeginPaint(hwnd, &ps);

            if (self->editHotkey_) {
                RECT rcEx;
                GetWindowRect(self->editHotkey_, &rcEx);
                MapWindowPoints(HWND_DESKTOP, hwnd, reinterpret_cast<LPPOINT>(&rcEx), 2);
                
                HPEN pen = CreatePen(PS_SOLID, 1, self->theme_.Colors().border);
                HBRUSH oldBr = (HBRUSH)SelectObject(hdc, GetStockObject(NULL_BRUSH));
                HPEN oldPen = (HPEN)SelectObject(hdc, pen);

                int offset = self->Dpi(8);
                int gbY = self->Dpi(10) + offset;
                int bottom = rcEx.bottom + self->Dpi(8);
                RoundRect(hdc, self->Dpi(8), gbY, self->Dpi(kAdvancedWidth - 8), bottom, 12, 12);

                if (self->editHotkey_) {
                    RECT rcE;
                    GetWindowRect(self->editHotkey_, &rcE);
                    MapWindowPoints(HWND_DESKTOP, hwnd, reinterpret_cast<LPPOINT>(&rcE), 2);
                    int padY = (self->Dpi(kControlHeight) - (rcE.bottom - rcE.top)) / 2;
                    rcE.top -= padY;
                    rcE.bottom += padY;
                    InflateRect(&rcE, self->Dpi(6), 0);
                    RoundRect(hdc, rcE.left, rcE.top, rcE.right, rcE.bottom, 6, 6);
                }

                SelectObject(hdc, oldBr);
                SelectObject(hdc, oldPen);
                DeleteObject(pen);
            }

            EndPaint(hwnd, &ps);
            return 0;
        }

        case WM_SETTINGCHANGE:
            if (self->theme_.OnSettingChange(lParam)) {
                self->theme_.ApplyWindowAttributes(hwnd);
                self->theme_.ThemeAllChildren(hwnd);
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
            if (self->configDirty_) {
                KillTimer(hwnd, kTimerDeferredSave);
                self->SaveToToml();
            }
            DestroyWindow(hwnd);
            return 0;

        case WM_DESTROY: {
            auto restoreAnim = [](HWND combo) {
                if (combo && GetPropW(combo, L"WasAnim")) {
                    SystemParametersInfoW(SPI_SETCOMBOBOXANIMATION, 0, (PVOID)TRUE, 0);
                    RemovePropW(combo, L"WasAnim");
                }
            };
            restoreAnim(self->comboMethod_);
            restoreAnim(self->comboEncoding_);

            self->theme_.Destroy();
            self->hwnd_ = nullptr;
            PostQuitMessage(0);
            return 0;
        }
    }

    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

} // namespace NextKey::Classic
