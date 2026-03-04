// NexusKey - System Tray Icon Implementation
// SPDX-License-Identifier: GPL-3.0-only

#include "TrayIcon.h"
#include "resource.h"
#include "UpdateChecker.h"
#include "sciter/SciterHelper.h"
#include "core/config/ConfigManager.h"
#include "core/Strings.h"
#include <strsafe.h>
#include <CommCtrl.h>
#include <uxtheme.h>
#include <thread>

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "uxtheme.lib")

namespace NextKey {

static TrayIcon* g_trayInstance = nullptr;

TrayIcon::TrayIcon() {
    g_trayInstance = this;
}

TrayIcon::~TrayIcon() {
    Destroy();
    if (customIcon_) {
        DestroyIcon(customIcon_);
        customIcon_ = nullptr;
    }
    g_trayInstance = nullptr;
}

bool TrayIcon::Create(HINSTANCE hInstance) {
    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(WNDCLASSEXW);
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.hIcon = LoadIconW(hInstance, MAKEINTRESOURCEW(IDI_APP));
    wc.hIconSm = LoadIconW(hInstance, MAKEINTRESOURCEW(IDI_APP));
    wc.lpszClassName = L"NexusKeyTrayClass";

    if (!RegisterClassExW(&wc)) {
        if (GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
            return false;
        }
    }

    // Create as WS_OVERLAPPEDWINDOW (like OpenKey) so Task Manager recognizes
    // the window and picks up hIcon for the process icon, then hide it.
    hwndMessage_ = CreateWindowExW(
        0, L"NexusKeyTrayClass", L"NexusKey", WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, 0, CW_USEDEFAULT, 0, nullptr, nullptr, hInstance, nullptr);

    if (!hwndMessage_) return false;

    // Enable dark mode for context menus (must be called before showing any menu)
    SciterHelper::ApplyDarkModeForApp();
    SciterHelper::SetWindowDarkMode(hwndMessage_, SciterHelper::IsWindowsDarkMode());

    ShowWindow(hwndMessage_, SW_HIDE);

    // Set window icon so Task Manager picks up the NK branding icon
    HICON appIcon = LoadIconW(hInstance, MAKEINTRESOURCEW(IDI_APP));
    if (appIcon) {
        SendMessageW(hwndMessage_, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(appIcon));
        SendMessageW(hwndMessage_, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(appIcon));
    }

    ZeroMemory(&nid_, sizeof(nid_));
    nid_.cbSize = sizeof(NOTIFYICONDATAW);
    nid_.hWnd = hwndMessage_;
    nid_.uID = 1;
    nid_.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    nid_.uCallbackMessage = WM_TRAYICON;

    // Load initial icon based on style
    RefreshIcon();
    StringCchCopyW(nid_.szTip, ARRAYSIZE(nid_.szTip), S(StringId::TIP_VIETNAMESE));

    // Always visible
    Shell_NotifyIconW(NIM_ADD, &nid_);
    return true;
}

void TrayIcon::Destroy() noexcept {
    if (nid_.hWnd) {
        Shell_NotifyIconW(NIM_DELETE, &nid_);
    }
    ZeroMemory(&nid_, sizeof(nid_));
    if (hwndMessage_) {
        DestroyWindow(hwndMessage_);
        hwndMessage_ = nullptr;
    }
}

void TrayIcon::SetVietnameseMode(bool enabled) noexcept {
    if (vietnameseMode_ == enabled) return;
    vietnameseMode_ = enabled;

    RefreshIcon();

    StringCchCopyW(nid_.szTip, ARRAYSIZE(nid_.szTip),
        enabled ? S(StringId::TIP_VIETNAMESE) : S(StringId::TIP_ENGLISH));

    if (nid_.hWnd) {
        Shell_NotifyIconW(NIM_MODIFY, &nid_);
    }
}

void TrayIcon::SetIconConfig(uint8_t style, uint32_t colorV, uint32_t colorE) noexcept {
    if (iconStyle_ == style && customColorV_ == colorV && customColorE_ == colorE) return;

    iconStyle_ = style;
    customColorV_ = colorV;
    customColorE_ = colorE;

    RefreshIcon();

    if (nid_.hWnd) {
        Shell_NotifyIconW(NIM_MODIFY, &nid_);
    }
}

void TrayIcon::RefreshIcon() noexcept {
    HINSTANCE hInstance = GetModuleHandleW(nullptr);

    // Free previous custom icon if any
    if (customIcon_) {
        DestroyIcon(customIcon_);
        customIcon_ = nullptr;
    }

    HICON newIcon = nullptr;

    switch (iconStyle_) {
        case 1:  // Dark mode (white icons)
            newIcon = LoadIconW(hInstance,
                MAKEINTRESOURCEW(vietnameseMode_ ? IDI_VIET_ON_WHITE : IDI_VIET_OFF_WHITE));
            break;

        case 2:  // Light mode (black icons)
            newIcon = LoadIconW(hInstance,
                MAKEINTRESOURCEW(vietnameseMode_ ? IDI_VIET_ON_BLACK : IDI_VIET_OFF_BLACK));
            break;

        case 3: {  // Custom color
            int baseIcon = vietnameseMode_ ? IDI_VIET_ON : IDI_VIET_OFF;
            COLORREF color = static_cast<COLORREF>(
                vietnameseMode_
                    ? (customColorV_ != 0 ? customColorV_ : DEFAULT_ICON_COLOR_V)
                    : (customColorE_ != 0 ? customColorE_ : DEFAULT_ICON_COLOR_E));
            newIcon = CreateColorizedIcon(baseIcon, color);
            if (newIcon) {
                customIcon_ = newIcon;  // Track for cleanup
            }
            break;
        }

        default:  // 0 = Color (default)
            newIcon = LoadIconW(hInstance,
                MAKEINTRESOURCEW(vietnameseMode_ ? IDI_VIET_ON : IDI_VIET_OFF));
            break;
    }

    if (newIcon) {
        nid_.hIcon = newIcon;
    } else {
        // Fallback
        nid_.hIcon = LoadIconW(hInstance, MAKEINTRESOURCEW(IDI_VIET_ON));
    }
}

HICON TrayIcon::CreateColorizedIcon(int baseIconId, COLORREF newColor) noexcept {
    HINSTANCE hInstance = GetModuleHandleW(nullptr);

    // DPI-aware icon loading
    int iconCx = GetSystemMetrics(SM_CXSMICON);
    int iconCy = GetSystemMetrics(SM_CYSMICON);

    HICON hBaseIcon = static_cast<HICON>(
        LoadImageW(hInstance, MAKEINTRESOURCEW(baseIconId), IMAGE_ICON, iconCx, iconCy, 0));
    if (!hBaseIcon) return nullptr;

    // Get icon bitmap data
    ICONINFO iconInfo = {};
    if (!GetIconInfo(hBaseIcon, &iconInfo)) {
        DestroyIcon(hBaseIcon);
        return nullptr;
    }

    HDC hdc = GetDC(nullptr);
    if (!hdc) {
        if (iconInfo.hbmColor) DeleteObject(iconInfo.hbmColor);
        if (iconInfo.hbmMask) DeleteObject(iconInfo.hbmMask);
        DestroyIcon(hBaseIcon);
        return nullptr;
    }

    // Get bitmap dimensions
    BITMAP bm = {};
    GetObject(iconInfo.hbmColor, sizeof(bm), &bm);
    int width = bm.bmWidth;
    int height = bm.bmHeight;

    // Extract pixels as 32-bit BGRA
    BITMAPINFO bmi = {};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = width;
    bmi.bmiHeader.biHeight = -height;  // Top-down
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    auto* pixels = new (std::nothrow) BYTE[width * height * 4];
    if (!pixels) {
        ReleaseDC(nullptr, hdc);
        if (iconInfo.hbmColor) DeleteObject(iconInfo.hbmColor);
        if (iconInfo.hbmMask) DeleteObject(iconInfo.hbmMask);
        DestroyIcon(hBaseIcon);
        return nullptr;
    }

    GetDIBits(hdc, iconInfo.hbmColor, 0, height, pixels, &bmi, DIB_RGB_COLORS);

    // Colorize: replace RGB while preserving alpha
    BYTE newR = GetRValue(newColor);
    BYTE newG = GetGValue(newColor);
    BYTE newB = GetBValue(newColor);

    for (int i = 0; i < width * height; i++) {
        BYTE* pixel = pixels + i * 4;
        BYTE alpha = pixel[3];
        if (alpha > 0) {
            pixel[0] = newB;  // DIB is BGRA
            pixel[1] = newG;
            pixel[2] = newR;
            // alpha unchanged
        }
    }

    // Write modified pixels back
    SetDIBits(hdc, iconInfo.hbmColor, 0, height, pixels, &bmi, DIB_RGB_COLORS);
    delete[] pixels;
    ReleaseDC(nullptr, hdc);

    // Create new icon from modified bitmaps
    HICON hNewIcon = CreateIconIndirect(&iconInfo);

    // Cleanup GDI objects
    if (iconInfo.hbmColor) DeleteObject(iconInfo.hbmColor);
    if (iconInfo.hbmMask) DeleteObject(iconInfo.hbmMask);
    DestroyIcon(hBaseIcon);

    return hNewIcon;
}

void TrayIcon::ShowContextMenu() {
    HMENU hMenu = CreatePopupMenu();
    if (!hMenu) return;

    // Query current state for checkmarks
    TrayMenuState state;
    if (menuStateGetter_) {
        state = menuStateGetter_();
    }

    auto checked = [](bool on) -> UINT { return MF_STRING | (on ? MF_CHECKED : 0); };

    // ── Section 1: Vietnamese mode toggle ──
    AppendMenuW(hMenu, checked(state.vietnamese),
        static_cast<UINT>(TrayMenuId::ToggleMode), S(StringId::MENU_TOGGLE_VIET));
    AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);

    // ── Section 2: Feature toggles ──
    AppendMenuW(hMenu, checked(state.spellCheck),
        static_cast<UINT>(TrayMenuId::SpellCheck), S(StringId::MENU_SPELL_CHECK));
    AppendMenuW(hMenu, checked(state.smartSwitch),
        static_cast<UINT>(TrayMenuId::SmartSwitch), S(StringId::MENU_SMART_SWITCH));
    AppendMenuW(hMenu, checked(state.macroEnabled),
        static_cast<UINT>(TrayMenuId::MacroEnabled), S(StringId::MENU_MACRO_TOGGLE));
    AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);

    // ── Section 3: Tools ──
    AppendMenuW(hMenu, MF_STRING,
        static_cast<UINT>(TrayMenuId::MacroTable), S(StringId::MENU_MACRO_CONFIG));
    AppendMenuW(hMenu, MF_STRING,
        static_cast<UINT>(TrayMenuId::ConvertTool), S(StringId::MENU_CONVERT_TOOL));

    // Quick Convert — show configured hotkey as accelerator text
    {
        auto cc = ConfigManager::LoadConvertConfigOrDefault();
        std::wstring label = S(StringId::MENU_QUICK_CONVERT);

        // Build hotkey text (e.g. "Ctrl+Shift+C")
        const auto& hk = cc.hotkey;
        if (hk.ctrl || hk.shift || hk.alt || hk.win || hk.key != 0) {
            label += L'\t';
            if (hk.ctrl)  label += L"Ctrl+";
            if (hk.alt)   label += L"Alt+";
            if (hk.shift) label += L"Shift+";
            if (hk.win)   label += L"Win+";
            if (hk.key != 0) {
                if (hk.key == L' ') {
                    label += L"Space";
                } else {
                    label += static_cast<wchar_t>(towupper(hk.key));
                }
            }
        }

        AppendMenuW(hMenu, MF_STRING,
            static_cast<UINT>(TrayMenuId::QuickConvert), label.c_str());
    }
    AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);

    // ── Section 4: Input method submenu ──
    HMENU hInputMenu = CreatePopupMenu();
    if (hInputMenu) {
        auto addRadio = [&](int method, TrayMenuId id, const wchar_t* label) {
            UINT flags = MF_STRING | (state.inputMethod == method ? MF_CHECKED : 0);
            AppendMenuW(hInputMenu, flags, static_cast<UINT>(id), label);
        };
        addRadio(0, TrayMenuId::InputTelex, L"Telex");
        addRadio(1, TrayMenuId::InputVNI, L"VNI");
        addRadio(2, TrayMenuId::InputSimpleTelex, L"Simple Telex");
        AppendMenuW(hMenu, MF_POPUP, reinterpret_cast<UINT_PTR>(hInputMenu), S(StringId::MENU_INPUT_METHOD));
    }

    // ── Code table submenu ──
    HMENU hCodeTableMenu = CreatePopupMenu();
    if (hCodeTableMenu) {
        auto addCT = [&](CodeTable ct, TrayMenuId id, const wchar_t* label) {
            UINT flags = MF_STRING | (state.codeTable == ct ? MF_CHECKED : 0);
            AppendMenuW(hCodeTableMenu, flags, static_cast<UINT>(id), label);
        };
        addCT(CodeTable::Unicode,         TrayMenuId::CodeTableUnicode,  L"Unicode");
        addCT(CodeTable::TCVN3,           TrayMenuId::CodeTableTCVN3,    L"TCVN3 (ABC)");
        addCT(CodeTable::VNIWindows,      TrayMenuId::CodeTableVNI,      L"VNI Windows");
        addCT(CodeTable::UnicodeCompound, TrayMenuId::CodeTableCompound, S(StringId::MENU_UNICODE_COMPOUND));
        addCT(CodeTable::VietnameseLocale,TrayMenuId::CodeTableCP1258,   L"CP 1258");
        AppendMenuW(hMenu, MF_POPUP, reinterpret_cast<UINT_PTR>(hCodeTableMenu), S(StringId::MENU_CODE_TABLE));
    }
    AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);

    // ── Section 5: System ──
    AppendMenuW(hMenu, MF_STRING,
        static_cast<UINT>(TrayMenuId::Settings), S(StringId::MENU_SETTINGS));
    AppendMenuW(hMenu, MF_STRING,
        static_cast<UINT>(TrayMenuId::About), S(StringId::MENU_ABOUT));
    AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(hMenu, MF_STRING,
        static_cast<UINT>(TrayMenuId::Exit), S(StringId::MENU_EXIT));

    POINT pt;
    GetCursorPos(&pt);
    SetForegroundWindow(hwndMessage_);

    UINT cmd = TrackPopupMenu(hMenu, TPM_RETURNCMD | TPM_NONOTIFY | TPM_RIGHTBUTTON,
        pt.x, pt.y, 0, hwndMessage_, nullptr);
    DestroyMenu(hMenu);

    if (cmd && menuCallback_) {
        menuCallback_(static_cast<TrayMenuId>(cmd));
    }
    PostMessageW(hwndMessage_, WM_NULL, 0, 0);
}

bool TrayIcon::ProcessMessage(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) noexcept {
    // Settings dialog requesting a specific V/E mode (cross-process)
    if (msg == WM_NEXUSKEY_SET_MODE && hwnd == hwndMessage_) {
        if (modeRequestCallback_) {
            modeRequestCallback_(wParam != 0);
        }
        return true;
    }

    // WM_CLOSE on tray window: clean shutdown (used by updater to signal exit)
    if (msg == WM_CLOSE && hwnd == hwndMessage_) {
        Destroy();
        PostQuitMessage(0);
        return true;
    }

    // Auto-update check found an available update — show TaskDialog
    if (msg == WM_NEXUSKEY_UPDATE_AVAILABLE && hwnd == hwndMessage_) {
        auto* info = reinterpret_cast<UpdateInfo*>(lParam);
        if (info) {
            if (UpdateChecker::ShowUpdateDialog(hwnd, *info)) {
                // User clicked "Update now" — download + launch updater + exit
                std::wstring downloadUrl = info->downloadUrl;
                delete info;

                std::thread([hwnd, downloadUrl]() {
                    wchar_t tempDir[MAX_PATH] = {};
                    GetTempPathW(MAX_PATH, tempDir);
                    std::wstring zipPath = std::wstring(tempDir) + L"NexusKey_update.zip";

                    if (!UpdateChecker::DownloadFile(downloadUrl, zipPath)) return;

                    wchar_t exePath[MAX_PATH] = {};
                    GetModuleFileNameW(nullptr, exePath, MAX_PATH);

                    std::wstring cmdLine = L"\"";
                    cmdLine += exePath;
                    cmdLine += L"\" --install-update \"";
                    cmdLine += zipPath;
                    cmdLine += L"\"";

                    STARTUPINFOW si = { sizeof(si) };
                    PROCESS_INFORMATION pi = {};
                    if (CreateProcessW(nullptr, cmdLine.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi)) {
                        CloseHandle(pi.hThread);
                        CloseHandle(pi.hProcess);
                    }

                    PostMessageW(hwnd, WM_CLOSE, 0, 0);
                }).detach();
            } else {
                delete info;
            }
        }
        return true;
    }

    // System config changed (icon style, language, etc.) — re-read from TOML and refresh
    if (msg == WM_NEXUSKEY_ICON_CHANGED && hwnd == hwndMessage_) {
        auto sysConfig = ConfigManager::LoadSystemConfigOrDefault();
        SetIconConfig(sysConfig.iconStyle, sysConfig.customColorV, sysConfig.customColorE);
        SetLanguage(static_cast<Language>(sysConfig.language));
        return true;
    }

    // WM_HOTKEY is handled by the caller's WndProc, not here
    if (msg == WM_HOTKEY && hwnd == hwndMessage_) {
        if (menuCallback_) {
            menuCallback_(TrayMenuId::ToggleMode);
        }
        return true;
    }

    if (msg != WM_TRAYICON || hwnd != hwndMessage_) return false;

    switch (LOWORD(lParam)) {
        case WM_RBUTTONUP:
        case WM_CONTEXTMENU:
            ShowContextMenu();
            return true;
        case WM_LBUTTONUP:
            // Toggle immediately (no delay)
            toggledByClick_ = true;
            if (menuCallback_) {
                menuCallback_(TrayMenuId::ToggleMode);
            }
            return true;
        case WM_LBUTTONDBLCLK:
            // Undo the toggle from the first click, then open settings
            if (toggledByClick_ && menuCallback_) {
                menuCallback_(TrayMenuId::ToggleMode);  // Undo
            }
            toggledByClick_ = false;
            if (menuCallback_) {
                menuCallback_(TrayMenuId::Settings);
            }
            return true;
    }

    // Any non-left-button event clears the toggle tracking
    toggledByClick_ = false;
    return false;
}

LRESULT CALLBACK TrayIcon::WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    // Real-time theme switch for context menus
    if (msg == WM_SETTINGCHANGE && lParam) {
        if (wcscmp(reinterpret_cast<LPCWSTR>(lParam), L"ImmersiveColorSet") == 0) {
            bool dark = SciterHelper::IsWindowsDarkMode();
            SciterHelper::SetWindowDarkMode(hwnd, dark);
        }
    }

    if (g_trayInstance && g_trayInstance->ProcessMessage(hwnd, msg, wParam, lParam)) {
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

}  // namespace NextKey
