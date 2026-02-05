// NexusKey - Settings Dialog Implementation
// SPDX-License-Identifier: GPL-3.0-only

#include "SettingsDialog.h"
#include "core/config/ConfigManager.h"
#include "core/ConfigEvent.h"
#include <dwmapi.h>
#include <commctrl.h>
#include <windowsx.h>

#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "comctl32.lib")

namespace NextKey {

// DWM constants for backdrop
#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif
#ifndef DWMWA_SYSTEMBACKDROP_TYPE
#define DWMWA_SYSTEMBACKDROP_TYPE 38
#endif

// Static dialog instance for SubclassProc access
static SettingsDialog* s_instance = nullptr;

SettingsDialog::SettingsDialog() 
    : sciter::window(SW_POPUP, RECT{0, 0, 400, 300}) {
    
    s_instance = this;
    
    // Load current settings first
    loadSettings();

    // ═══════════════════════════════════════════════════════════
    // ORDER IS CRITICAL - Do not rearrange these steps!
    // ═══════════════════════════════════════════════════════════

    // 1. Set transparent BEFORE load() - enables blur effect
    SciterSetOption(get_hwnd(), SCITER_TRANSPARENT_WINDOW, 1);

    // 2. Load HTML - use file path in Debug mode
#ifdef NDEBUG
    load(WSTR("this://app/ui/settings.htm"));
#else
    // Debug: load from file system for hot reload
    wchar_t exePath[MAX_PATH];
    GetModuleFileNameW(NULL, exePath, MAX_PATH);
    wchar_t* lastSlash = wcsrchr(exePath, L'\\');
    if (lastSlash) *lastSlash = L'\0';
    
    wchar_t relativePath[MAX_PATH];
    wchar_t htmlPath[MAX_PATH];
    // Navigate from build/Debug to src/app/ui
    swprintf_s(relativePath, MAX_PATH, L"%s\\..\\..\\src\\app\\ui\\settings.htm", exePath);
    
    // Convert to absolute path (resolves ..)
    GetFullPathNameW(relativePath, MAX_PATH, htmlPath, NULL);
    
    if (!load(htmlPath)) {
        MessageBoxW(NULL, htmlPath, L"Failed to load HTML", MB_OK | MB_ICONERROR);
        return;
    }
#endif

    // 3. Show window
    expand();

    // 4. Set title (used for FindWindow single-instance check)
    SetWindowTextW(get_hwnd(), L"NexusKey Settings");

    // 5. Size and center
    SetWindowPos(get_hwnd(), NULL, 0, 0, 400, 350, SWP_NOMOVE | SWP_NOZORDER);

    RECT rc;
    GetWindowRect(get_hwnd(), &rc);
    int x = (GetSystemMetrics(SM_CXSCREEN) - (rc.right - rc.left)) / 2;
    int y = (GetSystemMetrics(SM_CYSCREEN) - (rc.bottom - rc.top)) / 2;
    SetWindowPos(get_hwnd(), HWND_NOTOPMOST, x, y, 0, 0, SWP_NOSIZE);

    // 6. Apply DWM dark mode and Acrylic backdrop (AFTER sizing)
    HWND hwnd = get_hwnd();
    if (hwnd) {
        BOOL darkMode = TRUE;
        DwmSetWindowAttribute(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &darkMode, sizeof(darkMode));
        
        int backdropType = 3;  // Acrylic
        DwmSetWindowAttribute(hwnd, DWMWA_SYSTEMBACKDROP_TYPE, &backdropType, sizeof(backdropType));
    }

    // 7. Subclass for window dragging and close
    SetWindowSubclass(get_hwnd(), SubclassProc, 1, reinterpret_cast<DWORD_PTR>(this));
}

SettingsDialog::~SettingsDialog() {
    s_instance = nullptr;
}

LRESULT CALLBACK SettingsDialog::SubclassProc(
    HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam,
    UINT_PTR uIdSubclass, DWORD_PTR dwRefData) {
    
    UNREFERENCED_PARAMETER(uIdSubclass);
    UNREFERENCED_PARAMETER(dwRefData);

    // WM_CLOSE: MUST use DestroyWindow, not PostQuitMessage
    // (PostQuitMessage causes Sciter assertion failures)
    if (msg == WM_CLOSE) {
        DestroyWindow(hwnd);
        return 0;
    }

    if (msg == WM_DESTROY) {
        RemoveWindowSubclass(hwnd, SubclassProc, 1);
        return 0;
    }

    // Window dragging via title bar area
    if (msg == WM_NCHITTEST) {
        LRESULT result = DefSubclassProc(hwnd, msg, wParam, lParam);
        if (result == HTCLIENT) {
            POINT pt = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
            ScreenToClient(hwnd, &pt);

            RECT rc;
            GetClientRect(hwnd, &rc);

            // Drag zone: top 50px, excluding close button area (right 50px)
            if (pt.y < 50 && pt.x < rc.right - 50) {
                return HTCAPTION;
            }
        }
        return result;
    }

    return DefSubclassProc(hwnd, msg, wParam, lParam);
}

void SettingsDialog::Show() {
    // Message loop until window is closed
    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
        
        if (!IsWindow(get_hwnd())) break;
    }
}

void SettingsDialog::onInputMethodChange(int method) {
    currentMethod_ = method;
    saveSettings();
    if (onSettingsChanged_) onSettingsChanged_();
}

void SettingsDialog::onSpellCheckChange(bool enabled) {
    spellCheck_ = enabled;
    saveSettings();
    if (onSettingsChanged_) onSettingsChanged_();
}

void SettingsDialog::onClose() {
    HWND hwnd = get_hwnd();
    if (hwnd) {
        PostMessage(hwnd, WM_CLOSE, 0, 0);
    }
}

void SettingsDialog::loadSettings() {
    auto config = ConfigManager::LoadOrDefault();
    currentMethod_ = (config.inputMethod == InputMethod::VNI) ? 1 : 0;
    spellCheck_ = config.spellCheckEnabled;
}

void SettingsDialog::saveSettings() {
    TypingConfig config;
    config.inputMethod = (currentMethod_ == 1) ? InputMethod::VNI : InputMethod::Telex;
    config.spellCheckEnabled = spellCheck_;
    
    std::wstring path = ConfigManager::GetConfigPath();
    ConfigManager::SaveToFile(path, config);
    
    // Signal Engine that config has changed
    ConfigEvent event;
    if (event.Initialize()) {
        event.Signal();
    }
}

}  // namespace NextKey
