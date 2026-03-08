// NexusKey - Sciter SubDialog Base Class Implementation
// SPDX-License-Identifier: GPL-3.0-only

#include "SciterSubDialog.h"
#include "sciter/ScaleHelper.h"
#include "sciter/SciterHelper.h"
#include "core/config/ConfigManager.h"
#include "core/Strings.h"
#include "sciter-x-dom.hpp"
#include "sciter-x-host-callback.h"
#include <dwmapi.h>
#include <commctrl.h>
#include <vector>

using namespace sciter::dom;

#pragma comment(lib, "comctl32.lib")

namespace NextKey {

// DWM constants for dark mode
#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif

SciterSubDialog* SciterSubDialog::s_instance = nullptr;

SciterSubDialog::SciterSubDialog(const SubDialogConfig& config)
    : sciter::window(SW_POPUP, RECT{0, 0, config.baseWidth, config.baseHeight})
    , config_(config) {

    s_instance = this;

    // Set up UI base path for Debug mode
#ifndef SCITER_USE_PACKFOLDER
    wchar_t exeDir[MAX_PATH] = {};
    if (GetModuleFileNameW(nullptr, exeDir, MAX_PATH) != 0) {
        wchar_t* lastSlash = wcsrchr(exeDir, L'\\');
        if (lastSlash) *lastSlash = L'\0';
        uiBasePath_ = exeDir;
        uiBasePath_ += L"\\ui\\";
    }
#endif

    // Enable transparency for blur effect
    SciterSetOption(get_hwnd(), SCITER_TRANSPARENT_WINDOW, 1);

    // Load HTML
    if (!load(config_.htmlPath)) {
        MessageBoxW(nullptr, L"Failed to load dialog HTML", L"NexusKey Error", MB_OK | MB_ICONERROR);
        return;
    }

    // Set theme class + language (before scripts run)
    {
        sciter::dom::element htmlRoot(get_root());
        // Dark class goes on <body> (CSS targets body.dark), lang goes on <html>
        sciter::dom::element body = htmlRoot.find_first("body");
        if (body.is_valid() && SciterHelper::IsWindowsDarkMode()) {
            body.set_attribute("class", L"dark");
        }
        if (GetLanguage() == Language::English) {
            htmlRoot.set_attribute("lang", L"en");
            // Re-apply translations: initSubDialog() already ran during load()
            // when lang was still "vi". Now that lang="en" is set, re-run.
            call_function("applyTranslations");
        }
    }

    // Show window
    expand();

    // Set title
    SetWindowTextW(get_hwnd(), config_.windowTitle);

    // Auto-fit to content with DPI scaling
    sciter::dom::element rootEl = get_root();
    sciter::dom::element container = rootEl.find_first(".container");
    if (container.is_valid()) {
        // Window auto-sizes based on CSS width/height: max-content
        // and transparent OS window mode natively handles sizing
    }

    // Center on parent or screen
    RECT rc;
    GetWindowRect(get_hwnd(), &rc);
    int winWidth = rc.right - rc.left;
    int winHeight = rc.bottom - rc.top;

    if (config_.parentHwnd) {
        RECT parentRect;
        GetWindowRect(config_.parentHwnd, &parentRect);
        int px = parentRect.left + (parentRect.right - parentRect.left - winWidth) / 2;
        int py = parentRect.top + (parentRect.bottom - parentRect.top - winHeight) / 2;
        SetWindowPos(get_hwnd(), config_.topmost ? HWND_TOPMOST : HWND_NOTOPMOST,
                     px, py, 0, 0, SWP_NOSIZE);
    } else {
        int screenWidth = GetSystemMetrics(SM_CXSCREEN);
        int screenHeight = GetSystemMetrics(SM_CYSCREEN);
        int x = (screenWidth - winWidth) / 2;
        int y = (screenHeight - winHeight) / 2;
        SetWindowPos(get_hwnd(), config_.topmost ? HWND_TOPMOST : HWND_NOTOPMOST,
                     x, y, 0, 0, SWP_NOSIZE);
    }

    // Theme-aware DWM mode + rounded corners + blur
    HWND hwnd = get_hwnd();
    if (hwnd) {
        bool dark = SciterHelper::IsWindowsDarkMode();
        SciterHelper::SetWindowDarkMode(hwnd, dark);

        int cornerPreference = DwmConstants::DWMWCP_ROUND;
        DwmSetWindowAttribute(hwnd, DwmConstants::DWMWA_WINDOW_CORNER_PREFERENCE,
                              &cornerPreference, sizeof(cornerPreference));

        SciterHelper::enableWindowBlur(hwnd, BlurMode::Blur);
    }

    // Subclass for dragging and close
    SetWindowSubclass(get_hwnd(), SubclassProc, 1, reinterpret_cast<DWORD_PTR>(this));

    // Apply background opacity from UIConfig (via DOM, not call_function)
    if (config_.applyBackgroundOpacity) {
        auto uiConfig = ConfigManager::LoadUIConfigOrDefault();
        bool isDark = SciterHelper::IsWindowsDarkMode();
        double opacity = uiConfig.backgroundOpacity / 100.0;
        sciter::dom::element rootEl2(get_root());
        sciter::dom::element mainContainer = rootEl2.find_first("#main-container");
        if (mainContainer.is_valid()) {
            wchar_t bgColor[64];
            if (isDark) {
                swprintf_s(bgColor, L"rgba(18, 20, 28, %.2f)", opacity * 0.9);
            } else {
                swprintf_s(bgColor, L"rgba(255, 255, 255, %.2f)", opacity);
            }
            mainContainer.set_style_attribute("background-color", bgColor);
        }
    }
}

SciterSubDialog::~SciterSubDialog() {
    s_instance = nullptr;
}

void SciterSubDialog::Show() {
    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);

        if (!IsWindow(get_hwnd())) break;
    }
}

LRESULT SciterSubDialog::on_load_data(LPSCN_LOAD_DATA pnmld) {
    aux::wchars uri = aux::chars_of(pnmld->uri);
    if (!uri.like(WSTR("this://app/*"))) {
        return LOAD_OK;
    }

    const wchar_t* relativePath = pnmld->uri + 11;

#ifdef SCITER_USE_PACKFOLDER
    aux::bytes data = sciter::archive::instance().get(relativePath);
    if (data.length) {
        ::SciterDataReady(pnmld->hwnd, pnmld->uri, data.start, UINT(data.length));
        return LOAD_OK;
    }
    return LOAD_DISCARD;
#else
    if (uiBasePath_.empty()) {
        return LOAD_DISCARD;
    }

    std::wstring filePath = uiBasePath_ + relativePath;
    for (wchar_t& c : filePath) {
        if (c == L'/') c = L'\\';
    }

    HANDLE hFile = CreateFileW(filePath.c_str(), GENERIC_READ, FILE_SHARE_READ,
                               nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hFile == INVALID_HANDLE_VALUE) {
        return LOAD_DISCARD;
    }

    DWORD fileSize = GetFileSize(hFile, nullptr);
    if (fileSize == INVALID_FILE_SIZE || fileSize == 0) {
        CloseHandle(hFile);
        return LOAD_DISCARD;
    }

    std::vector<BYTE> buffer(fileSize);
    DWORD bytesRead = 0;
    if (!ReadFile(hFile, buffer.data(), fileSize, &bytesRead, nullptr) || bytesRead != fileSize) {
        CloseHandle(hFile);
        return LOAD_DISCARD;
    }
    CloseHandle(hFile);

    ::SciterDataReady(pnmld->hwnd, pnmld->uri, buffer.data(), fileSize);
    return LOAD_OK;
#endif
}

LRESULT CALLBACK SciterSubDialog::SubclassProc(
    HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam,
    UINT_PTR uIdSubclass, DWORD_PTR dwRefData) {

    UNREFERENCED_PARAMETER(uIdSubclass);
    UNREFERENCED_PARAMETER(dwRefData);

    if (msg == WM_CLOSE) {
        if (s_instance) {
            s_instance->onBeforeClose();
        }
        RemoveWindowSubclass(hwnd, SubclassProc, 1);
        ExitProcess(0);
    }

    if (msg == WM_DESTROY) {
        RemoveWindowSubclass(hwnd, SubclassProc, 1);
        return 0;
    }

    // Real-time theme switch (like OpenKey — pure DOM manipulation, no call_function)
    if (msg == WM_SETTINGCHANGE && lParam) {
        if (wcscmp(reinterpret_cast<LPCWSTR>(lParam), L"ImmersiveColorSet") == 0) {
            if (s_instance) {
                bool dark = SciterHelper::IsWindowsDarkMode();
                SciterHelper::SetWindowDarkMode(hwnd, dark);

                // Toggle body.dark class (get_root() = <html>, need <body>)
                sciter::dom::element htmlRoot(s_instance->get_root());
                sciter::dom::element body = htmlRoot.find_first("body");
                if (body.is_valid()) {
                    body.set_attribute("class", dark ? L"dark" : L"");
                }

                // Update container background opacity for new theme
                if (s_instance->config_.applyBackgroundOpacity) {
                    auto uiConfig = ConfigManager::LoadUIConfigOrDefault();
                    double opacity = uiConfig.backgroundOpacity / 100.0;
                    sciter::dom::element root = s_instance->get_root();
                    sciter::dom::element container = root.find_first("#main-container");
                    if (container.is_valid()) {
                        wchar_t bgColor[64];
                        if (dark) {
                            swprintf_s(bgColor, L"rgba(18, 20, 28, %.2f)", opacity * 0.9);
                        } else {
                            swprintf_s(bgColor, L"rgba(255, 255, 255, %.2f)", opacity);
                        }
                        container.set_style_attribute("background-color", bgColor);
                    }
                }
            }
        }
        return DefSubclassProc(hwnd, msg, wParam, lParam);
    }

    // Window dragging via title bar
    if (msg == WM_NCHITTEST) {
        LRESULT result = DefSubclassProc(hwnd, msg, wParam, lParam);
        if (result == HTCLIENT && s_instance) {
            return SciterHelper::handleWindowDrag(hwnd, lParam,
                s_instance->config_.titleBarHeight, s_instance->config_.buttonsWidth);
        }
        return result;
    }

    // Let subclass handle custom messages
    if (s_instance) {
        LRESULT customResult = s_instance->onCustomMessage(hwnd, msg, wParam, lParam);
        if (customResult != -1) {
            return customResult;
        }
    }

    return DefSubclassProc(hwnd, msg, wParam, lParam);
}

}  // namespace NextKey
