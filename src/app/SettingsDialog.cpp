// NexusKey - Settings Dialog Implementation
// SPDX-License-Identifier: GPL-3.0-only

#include "SettingsDialog.h"
#include "SubprocessHelper.h"
#include "helpers/ScaleHelper.h"
#include "helpers/SciterHelper.h"
#include "core/config/ConfigManager.h"
#include "core/UIConfig.h"
#include "core/ConfigEvent.h"
#include "core/SharedConstants.h"
#include "core/SharedStateManager.h"
#include "sciter-x-dom.hpp"
#include "sciter-x-host-callback.h"
#include <dwmapi.h>
#include <commctrl.h>
#include <windowsx.h>
#include <memory>
#include <vector>
#include <algorithm>

using namespace sciter::dom;  // For ELEMENT_AREAS enum (CONTENT_BOX, etc.)

#pragma comment(lib, "comctl32.lib")

namespace NextKey {

// Timer IDs for async operations
#define TIMER_RESIZE_WINDOW 1001

// DWM constants for dark mode
#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif

// Helper: Convert wide string to narrow string (ASCII subset only, for CSS selectors)
static std::string toNarrowString(const std::wstring& wide) {
    std::string result;
    result.reserve(wide.size());
    for (wchar_t wc : wide) {
        result.push_back(static_cast<char>(wc & 0x7F));  // ASCII only
    }
    return result;
}

// Base window dimensions (before DPI scaling)
constexpr int BASE_WIDTH_COLLAPSED = 350;
constexpr int BASE_HEIGHT_COLLAPSED = 460;  // Match OpenKey height

// Static dialog instance for SubclassProc access
static SettingsDialog* s_instance = nullptr;

SettingsDialog::SettingsDialog()
    : sciter::window(SW_POPUP, RECT{0, 0, BASE_WIDTH_COLLAPSED, BASE_HEIGHT_COLLAPSED}) {

    s_instance = this;

    // Load current settings first
    loadSettings();

    // ═══════════════════════════════════════════════════════════
    // ORDER IS CRITICAL - Do not rearrange these steps!
    // ═══════════════════════════════════════════════════════════

    // 0. Set up UI base path for Debug mode (file system loading)
#ifndef SCITER_USE_PACKFOLDER
    wchar_t exeDir[MAX_PATH] = {};
    if (GetModuleFileNameW(nullptr, exeDir, MAX_PATH) != 0) {
        wchar_t* lastSlash = wcsrchr(exeDir, L'\\');
        if (lastSlash) *lastSlash = L'\0';
        uiBasePath_ = exeDir;
        uiBasePath_ += L"\\ui\\";
    }
#endif

    // 1. Set transparent BEFORE load() - enables blur effect
    SciterSetOption(get_hwnd(), SCITER_TRANSPARENT_WINDOW, 1);

    // 2. Load HTML using this://app/ scheme (works for both Debug and Release)
    // In Debug: on_load_data() intercepts and loads from file system
    // In Release: on_load_data() loads from packed archive
    if (!load(WSTR("this://app/settings/settings.html"))) {
        MessageBoxW(nullptr, L"Failed to load settings.html", L"NexusKey Error", MB_OK | MB_ICONERROR);
        return;
    }

    // 3. Show window
    expand();

    // 4. Set title (used for FindWindow single-instance check)
    SetWindowTextW(get_hwnd(), L"NexusKey Settings");

    // 5. Auto-fit window to content size (like OpenKey)
    // Sciter renders at native DPI, DOM measurements are already in screen pixels
    sciter::dom::element rootEl = get_root();
    sciter::dom::element container = rootEl.find_first(".container");
    if (container.is_valid()) {
        RECT contentRect = container.get_location(CONTENT_BOX);
        double dpiScale = ScaleHelper::getDpiScale();

        // Scale minimum constraints, not DOM measurements
        int contentWidth = (std::max)(contentRect.right - contentRect.left, static_cast<LONG>(BASE_WIDTH_COLLAPSED * dpiScale));
        int contentHeight = (std::max)(contentRect.bottom - contentRect.top, static_cast<LONG>(200 * dpiScale));

        SetWindowPos(get_hwnd(), NULL, 0, 0, contentWidth, contentHeight, SWP_NOMOVE | SWP_NOZORDER);
    }

    // 6. Center window on screen
    RECT rc;
    GetWindowRect(get_hwnd(), &rc);
    int screenWidth = GetSystemMetrics(SM_CXSCREEN);
    int screenHeight = GetSystemMetrics(SM_CYSCREEN);
    int winWidth = rc.right - rc.left;
    int winHeight = rc.bottom - rc.top;
    int x = (screenWidth - winWidth) / 2;
    int y = (screenHeight - winHeight) / 2;
    SetWindowPos(get_hwnd(), HWND_NOTOPMOST, x, y, 0, 0, SWP_NOSIZE);

    // 7. Apply DWM dark mode, rounded corners, and Windows API blur
    HWND hwnd = get_hwnd();
    if (hwnd) {
        BOOL darkMode = TRUE;
        DwmSetWindowAttribute(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &darkMode, sizeof(darkMode));

        // Enable rounded corners on Windows 11
        int cornerPreference = DwmConstants::DWMWCP_ROUND;
        DwmSetWindowAttribute(hwnd, DwmConstants::DWMWA_WINDOW_CORNER_PREFERENCE,
                              &cornerPreference, sizeof(cornerPreference));

        // Enable Windows API blur effect (more beautiful than Sciter's native blur)
        SciterHelper::enableWindowBlur(hwnd, BlurMode::Blur);
    }

    // 8. Subclass for window dragging and close
    SetWindowSubclass(get_hwnd(), SubclassProc, 1, reinterpret_cast<DWORD_PTR>(this));

    // 9. Initialize UI with loaded settings
    initializeUI();
}

SettingsDialog::~SettingsDialog() {
    s_instance = nullptr;
}

// Custom resource loading handler - loads from file system in Debug, archive in Release
LRESULT SettingsDialog::on_load_data(LPSCN_LOAD_DATA pnmld) {
    // Check if this is a this://app/ URL
    aux::wchars uri = aux::chars_of(pnmld->uri);
    if (!uri.like(WSTR("this://app/*"))) {
        // Not our URL scheme, let Sciter handle it
        return LOAD_OK;
    }

    // Extract the relative path (skip "this://app/")
    const wchar_t* relativePath = pnmld->uri + 11;  // strlen("this://app/") = 11

#ifdef SCITER_USE_PACKFOLDER
    // Release: Load from packed archive
    aux::bytes data = sciter::archive::instance().get(relativePath);
    if (data.length) {
        ::SciterDataReady(pnmld->hwnd, pnmld->uri, data.start, UINT(data.length));
        return LOAD_OK;
    }
    OutputDebugStringW(L"NexusKey: Failed to load from archive: ");
    OutputDebugStringW(pnmld->uri);
    OutputDebugStringW(L"\n");
    return LOAD_DISCARD;
#else
    // Debug: Load from file system
    if (uiBasePath_.empty()) {
        OutputDebugStringW(L"NexusKey: UI base path not set\n");
        return LOAD_DISCARD;
    }

    // Build full file path (convert URL forward slashes to Windows backslashes)
    std::wstring filePath = uiBasePath_ + relativePath;
    for (wchar_t& c : filePath) {
        if (c == L'/') c = L'\\';
    }

    // Open and read the file
    HANDLE hFile = CreateFileW(filePath.c_str(), GENERIC_READ, FILE_SHARE_READ,
                               nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hFile == INVALID_HANDLE_VALUE) {
        OutputDebugStringW(L"NexusKey: Failed to open: ");
        OutputDebugStringW(filePath.c_str());
        OutputDebugStringW(L"\n");
        return LOAD_DISCARD;
    }

    DWORD fileSize = GetFileSize(hFile, nullptr);
    if (fileSize == INVALID_FILE_SIZE || fileSize == 0) {
        CloseHandle(hFile);
        return LOAD_DISCARD;
    }

    // Read file content
    std::vector<BYTE> buffer(fileSize);
    DWORD bytesRead = 0;
    if (!ReadFile(hFile, buffer.data(), fileSize, &bytesRead, nullptr) || bytesRead != fileSize) {
        CloseHandle(hFile);
        return LOAD_DISCARD;
    }
    CloseHandle(hFile);

    // Provide data to Sciter
    ::SciterDataReady(pnmld->hwnd, pnmld->uri, buffer.data(), fileSize);
    return LOAD_OK;
#endif
}

LRESULT CALLBACK SettingsDialog::SubclassProc(
    HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam,
    UINT_PTR uIdSubclass, DWORD_PTR dwRefData) {

    UNREFERENCED_PARAMETER(uIdSubclass);
    UNREFERENCED_PARAMETER(dwRefData);

    // WM_CLOSE: flush deferred save, then destroy
    // (MUST use DestroyWindow, not PostQuitMessage — Sciter assertion failures)
    if (msg == WM_CLOSE) {
        if (s_instance && s_instance->configDirty_) {
            KillTimer(hwnd, TIMER_DEFERRED_SAVE);
            s_instance->saveToToml();
        }
        DestroyWindow(hwnd);
        return 0;
    }

    if (msg == WM_DESTROY) {
        RemoveWindowSubclass(hwnd, SubclassProc, 1);
        return 0;
    }

    // Handle V/E mode change notification from main process
    if (msg == WM_NEXUSKEY_MODE_CHANGED) {
        if (s_instance) {
            bool vietnamese = (wParam != 0);
            s_instance->vietnameseMode_ = vietnamese;
            s_instance->setToggleState(L"toggle-language", vietnamese);
        }
        return 0;
    }

    // Handle timer for window resize after CSS transition
    if (msg == WM_TIMER && wParam == TIMER_RESIZE_WINDOW) {
        KillTimer(hwnd, TIMER_RESIZE_WINDOW);
        if (s_instance) {
            s_instance->recalcWindowSize();
        }
        return 0;
    }

    // Handle deferred TOML save timer (30s after last settings change)
    if (msg == WM_TIMER && wParam == TIMER_DEFERRED_SAVE) {
        KillTimer(hwnd, TIMER_DEFERRED_SAVE);
        if (s_instance && s_instance->configDirty_) {
            s_instance->saveToToml();
        }
        return 0;
    }

    // Deferred: open excluded apps dialog as subprocess
    // (must be a separate process — Sciter SOM assertion fires if a sciter::window
    //  is destroyed while the Sciter runtime is still active in this process)
    if (msg == WM_NEXUSKEY_OPEN_EXCLUDED) {
        SpawnSubprocess(L"NexusKey - Excluded Apps", L"--excludedapps");
        return 0;
    }

    // Window dragging via title bar area
    if (msg == WM_NCHITTEST) {
        LRESULT result = DefSubclassProc(hwnd, msg, wParam, lParam);
        if (result == HTCLIENT) {
            // Use SciterHelper for drag zone detection
            // Title height: 36px, Buttons zone: 70px (pin@40px + close@12px + margins)
            return SciterHelper::handleWindowDrag(hwnd, lParam, 36, 70);
        }
        return result;
    }

    return DefSubclassProc(hwnd, msg, wParam, lParam);
}

void SettingsDialog::SetVietnameseMode(bool vietnamese) {
    vietnameseMode_ = vietnamese;
    // Update toggle UI if window already exists (post-construction call)
    if (get_hwnd()) {
        setToggleState(L"toggle-language", vietnamese);
    }
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

bool SettingsDialog::handle_event(HELEMENT he, BEHAVIOR_EVENT_PARAMS& params) {
    UNREFERENCED_PARAMETER(he);

    // Handle BUTTON_CLICK events
    if (params.cmd == BUTTON_CLICK) {
        sciter::dom::element el(params.heTarget);
        std::wstring id = el.get_attribute("id");

        if (!id.empty()) {
            // Handle close button
            if (id == L"btn-close") {
                onClose();
                return true;
            }
            // Handle pin button
            if (id == L"btn-pin") {
                togglePin();
                return true;
            }
            // Handle other buttons
            handleButtonClick(id);
            return true;
        }
    }

    // Handle VALUE_CHANGED events (includes behavior:check native toggles)
    // behavior:check fires "change" which maps to VALUE_CHANGED
    else if (params.cmd == VALUE_CHANGED) {
        sciter::dom::element el(params.heTarget);
        std::wstring id = el.get_attribute("id");

        if (id.empty()) return sciter::window::handle_event(he, params);

        // Handle expand state change
        if (id == L"val-show-advanced" || id == L"val-expand-state") {
            sciter::value val = el.get_value();
            std::wstring strVal = val.is_string() ? val.get<std::wstring>() : L"0";
            bool expanded = (strVal == L"1");

            isExpanded_ = expanded;
            saveUISettings();
            SetTimer(get_hwnd(), TIMER_RESIZE_WINDOW, 10, NULL);
            return true;
        }

        // Handle background opacity change
        if (id == L"val-bg-opacity") {
            sciter::value val = el.get_value();
            int opacity = 80;
            if (val.is_int()) opacity = val.get<int>();
            else if (val.is_string()) opacity = _wtoi(val.get<std::wstring>().c_str());

            backgroundOpacity_ = static_cast<uint8_t>(std::clamp(opacity, 0, 100));
            saveUISettings();
            return true;
        }

        // Handle tab change
        if (id == L"val-tab-change") {
            if (isExpanded_) {
                SetTimer(get_hwnd(), TIMER_RESIZE_WINDOW, 50, NULL);
            }
            return true;
        }

        // Handle dropdown changes
        if (id == L"input-type" || id == L"bang-ma" || id == L"modern-icon") {
            sciter::value val = el.get_value();
            int intValue = 0;
            if (val.is_int()) intValue = val.get<int>();
            else if (val.is_string()) intValue = _wtoi(val.get<std::wstring>().c_str());
            handleDropdownChange(id, intValue);
            return true;
        }

        // Handle switch key character input
        if (id == L"switch-key-char") {
            sciter::value val = el.get_value();
            if (val.is_string()) {
                std::wstring raw = val.get<std::wstring>();
                // JS displays space as "Space" — convert back to actual space char
                if (raw == L"Space" || raw == L"space") {
                    switchKeyChar_ = L" ";
                } else {
                    switchKeyChar_ = raw;
                }
                saveSettings();
            }
            return true;
        }

        // Handle other toggle changes (val-* hidden inputs)
        if (id.find(L"val-") == 0) {
            std::wstring settingId = id.substr(4);  // Remove "val-" prefix
            sciter::value val = el.get_value();
            std::wstring strVal = val.is_string() ? val.get<std::wstring>() : L"0";
            bool boolValue = (strVal == L"1");
            handleToggleChange(settingId, boolValue);
            return true;
        }
    }

    // Call base class for default handling (e.g., DOCUMENT_PARSED)
    return sciter::window::handle_event(he, params);
}

void SettingsDialog::handleToggleChange(const std::wstring& id, bool value) {
    // Map toggle IDs to settings
    if (id == L"toggle-language") {
        // V/E toggle: send to main process via cross-process message
        // value=true means Vietnamese ON (checked), value=false means English
        vietnameseMode_ = value;
        HWND trayWnd = FindWindowW(L"NexusKeyTrayClass", nullptr);
        if (trayWnd) {
            PostMessageW(trayWnd, WM_NEXUSKEY_SET_MODE, value ? 1 : 0, 0);
        }
        return;  // V/E mode is not a persistent config setting
    }
    else if (id == L"beep-sound") {
        config_.beepOnSwitch = value;
    }
    else if (id == L"smart-switch") {
        config_.smartSwitch = value;
    }
    else if (id == L"exclude-apps") {
        config_.excludeApps = value;
    }
    else if (id == L"spell-check") {
        config_.spellCheckEnabled = value;
    }
    else if (id == L"key-ctrl") {
        hotkeyConfig_.ctrl = value;
    }
    else if (id == L"key-alt") {
        hotkeyConfig_.alt = value;
    }
    else if (id == L"key-win") {
        hotkeyConfig_.win = value;
    }
    else if (id == L"key-shift") {
        hotkeyConfig_.shift = value;
    }
    else if (id == L"modern-ortho") {
        config_.modernOrtho = value;
    }
    else if (id == L"auto-caps") {
        config_.autoCaps = value;
    }
    else if (id == L"allow-zwjf") {
        config_.allowZwjf = value;
    }

    saveSettings();
    if (onSettingsChanged_) onSettingsChanged_();
}

void SettingsDialog::handleDropdownChange(const std::wstring& id, int value) {
    if (id == L"input-type") {
        config_.inputMethod = static_cast<InputMethod>(value);
    }
    else if (id == L"bang-ma") {
        config_.codeTable = static_cast<CodeTable>(value);
    }
    // Add more dropdown handlers as needed...

    saveSettings();
    if (onSettingsChanged_) onSettingsChanged_();
}

void SettingsDialog::handleButtonClick(const std::wstring& id) {
    if (id == L"btn-excluded-apps") {
        // Defer dialog creation — creating a Sciter window inside handle_event causes reentrancy issues
        PostMessage(get_hwnd(), WM_NEXUSKEY_OPEN_EXCLUDED, 0, 0);
        return;
    }
    else if (id == L"btn-macro-table") {
        // TODO: Open macro table dialog
    }
    else if (id == L"btn-app-overrides") {
        // TODO: Open app overrides dialog
    }
    else if (id == L"btn-reset-settings") {
        // TODO: Reset all settings to defaults
    }
    else if (id == L"btn-check-update") {
        // TODO: Check for updates
    }
    else if (id == L"btn-open-log-folder") {
        // TODO: Open log folder in explorer
    }
    // Add more button handlers as needed...
}

void SettingsDialog::togglePin() {
    HWND hwnd = get_hwnd();
    if (!hwnd) return;

    isPinned_ = !isPinned_;

    // Set window topmost state
    SetWindowPos(hwnd, isPinned_ ? HWND_TOPMOST : HWND_NOTOPMOST,
                 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);

    // Update button visual state
    sciter::dom::element root = get_root();
    sciter::dom::element pinBtn = root.find_first("#btn-pin");
    if (pinBtn.is_valid()) {
        if (isPinned_) {
            pinBtn.set_attribute("class", L"btn-pin pinned");
        } else {
            pinBtn.set_attribute("class", L"btn-pin");
        }
    }

    // Save pinned state
    saveUISettings();
}

void SettingsDialog::recalcWindowSize() {
    HWND hwnd = get_hwnd();
    if (!hwnd) return;

    RECT rc;
    GetWindowRect(hwnd, &rc);

    sciter::dom::element rootEl = get_root();
    double dpiScale = ScaleHelper::getDpiScale();

    // Fixed widths (must match CSS)
    int COMPACT_WIDTH = static_cast<int>(BASE_WIDTH_COLLAPSED * dpiScale);
    int ADVANCED_WIDTH = static_cast<int>(400 * dpiScale);

    // Measure title bar and compact section
    sciter::dom::element title = rootEl.find_first(".title-bar");
    sciter::dom::element compact = rootEl.find_first(".compact-section");

    int titleHeight = 0;
    int compactHeight = 0;

    if (title.is_valid()) {
        RECT r = title.get_location(BORDER_BOX);
        titleHeight = r.bottom - r.top;
    }
    if (compact.is_valid()) {
        RECT r = compact.get_location(MARGIN_BOX);
        compactHeight = r.bottom - r.top;
    }

    int newWidth = COMPACT_WIDTH;
    int newHeight = titleHeight + compactHeight;

    if (isExpanded_) {
        newWidth = COMPACT_WIDTH + ADVANCED_WIDTH;

        // Measure advanced section
        sciter::dom::element advanced = rootEl.find_first(".advanced-section");
        if (advanced.is_valid()) {
            RECT r = advanced.get_location(MARGIN_BOX);
            int advancedHeight = r.bottom - r.top;
            newHeight = (std::max)(newHeight, titleHeight + advancedHeight);
        }
    }

    // Keep window position, just resize
    SetWindowPos(hwnd, NULL, rc.left, rc.top, newWidth, newHeight, SWP_NOZORDER);
}

void SettingsDialog::setToggleState(const std::wstring& id, bool checked) {
    sciter::dom::element root = get_root();
    std::string idStr = toNarrowString(id);
    sciter::dom::element toggle = root.find_first(("[id='" + idStr + "']").c_str());

    if (toggle.is_valid()) {
        // Div-based toggles use CSS class "checked" for styling
        std::wstring cls = toggle.get_attribute("class");
        std::wstring baseClass;

        // Remove existing "checked" class if present
        size_t pos = cls.find(L" checked");
        if (pos != std::wstring::npos) {
            baseClass = cls.substr(0, pos) + cls.substr(pos + 8);
        } else {
            pos = cls.find(L"checked ");
            if (pos != std::wstring::npos) {
                baseClass = cls.substr(0, pos) + cls.substr(pos + 8);
            } else {
                baseClass = cls;
            }
        }

        // Add "checked" class if needed
        if (checked) {
            toggle.set_attribute("class", (baseClass + L" checked").c_str());
        } else {
            toggle.set_attribute("class", baseClass.c_str());
        }
    }

    // Also update the hidden input value
    sciter::dom::element hiddenInput = root.find_first(("[id='val-" + idStr + "']").c_str());
    if (hiddenInput.is_valid()) {
        hiddenInput.set_value(sciter::value(checked ? 1 : 0));
    }
}

void SettingsDialog::setDropdownValue(const std::wstring& id, int value) {
    sciter::dom::element root = get_root();
    sciter::dom::element dropdown = root.find_first(("[id='" + toNarrowString(id) + "']").c_str());

    if (dropdown.is_valid()) {
        dropdown.set_value(sciter::value(value));
    }
}

void SettingsDialog::initializeUI() {
    sciter::dom::element root = get_root();
    if (!root.is_valid()) return;

    // Dark theme is set via class="dark" on body in HTML

    // Set input method dropdown
    setDropdownValue(L"input-type", static_cast<int>(config_.inputMethod));

    // Set code table dropdown
    setDropdownValue(L"bang-ma", static_cast<int>(config_.codeTable));

    // Set V/E toggle state (synced with main process)
    setToggleState(L"toggle-language", vietnameseMode_);

    // Set toggle states
    setToggleState(L"beep-sound", config_.beepOnSwitch);
    setToggleState(L"smart-switch", config_.smartSwitch);
    setToggleState(L"exclude-apps", config_.excludeApps);
    setToggleState(L"spell-check", config_.spellCheckEnabled);
    setToggleState(L"modern-ortho", config_.modernOrtho);
    setToggleState(L"auto-caps", config_.autoCaps);
    setToggleState(L"allow-zwjf", config_.allowZwjf);
    setToggleState(L"key-ctrl", hotkeyConfig_.ctrl);
    setToggleState(L"key-alt", hotkeyConfig_.alt);
    setToggleState(L"key-win", hotkeyConfig_.win);
    setToggleState(L"key-shift", hotkeyConfig_.shift);

    // Set switch key character (display "Space" for space char)
    sciter::dom::element switchKeyInput = root.find_first("#switch-key-char");
    if (switchKeyInput.is_valid()) {
        std::wstring display = (switchKeyChar_ == L" ") ? L"Space" : switchKeyChar_;
        switchKeyInput.set_value(sciter::value(display.c_str()));
    }

    // Set advanced toggle state
    setToggleState(L"show-advanced", isExpanded_);

    // Set expanded state on container
    if (isExpanded_) {
        sciter::dom::element container = root.find_first("#main-container");
        if (container.is_valid()) {
            std::wstring cls = container.get_attribute("class");
            if (cls.find(L"expanded") == std::wstring::npos) {
                container.set_attribute("class", (cls + L" expanded").c_str());
            }
        }
        // Resize window for expanded state
        SetTimer(get_hwnd(), TIMER_RESIZE_WINDOW, 50, NULL);
    }

    // Set background opacity (call JS function)
    call_function("setBackgroundOpacity", sciter::value(static_cast<int>(backgroundOpacity_)));

    // Apply pinned state if saved
    if (isPinned_) {
        SetWindowPos(get_hwnd(), HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
        sciter::dom::element pinBtn = root.find_first("#btn-pin");
        if (pinBtn.is_valid()) {
            pinBtn.set_attribute("class", L"btn-pin pinned");
        }
    }
}

void SettingsDialog::onInputMethodChange(int method) {
    config_.inputMethod = static_cast<InputMethod>(method);
    saveSettings();
    if (onSettingsChanged_) onSettingsChanged_();
}

void SettingsDialog::onSpellCheckChange(bool enabled) {
    config_.spellCheckEnabled = enabled;
    saveSettings();
    if (onSettingsChanged_) onSettingsChanged_();
}

void SettingsDialog::onExpandChange(bool expanded) {
    isExpanded_ = expanded;
    // Set timer to resize window after CSS transition completes
    SetTimer(get_hwnd(), TIMER_RESIZE_WINDOW, 50, NULL);
}

void SettingsDialog::onClose() {
    HWND hwnd = get_hwnd();
    if (hwnd) {
        PostMessage(hwnd, WM_CLOSE, 0, 0);
    }
}

void SettingsDialog::loadSettings() {
    // Load typing config
    config_ = ConfigManager::LoadOrDefault();

    // Load UI config
    auto uiConfig = ConfigManager::LoadUIConfigOrDefault();
    isExpanded_ = uiConfig.showAdvanced;
    backgroundOpacity_ = uiConfig.backgroundOpacity;
    isPinned_ = uiConfig.pinned;

    // Load hotkey config
    hotkeyConfig_ = ConfigManager::LoadHotkeyConfigOrDefault();
    switchKeyChar_ = (hotkeyConfig_.key != 0) ? std::wstring(1, hotkeyConfig_.key) : L"";
}

void SettingsDialog::saveSettings() {
    syncToSharedState();      // Immediate — DLL sees changes now
    configDirty_ = true;
    // Reset deferred save timer (30s from last change)
    SetTimer(get_hwnd(), TIMER_DEFERRED_SAVE, DEFERRED_SAVE_DELAY_MS, NULL);
}

void SettingsDialog::syncToSharedState() {
    // Update SharedState so DLL picks up changes immediately
    SharedStateManager sharedState;
    if (sharedState.OpenReadWrite()) {
        SharedState state = sharedState.Read();
        if (state.IsValid()) {
            state.inputMethod = static_cast<uint8_t>(config_.inputMethod);
            state.spellCheck = config_.spellCheckEnabled ? 1 : 0;
            state.featureFlags = EncodeFeatureFlags(config_);
            sharedState.Write(state);  // Write() auto-manages epoch via seqlock
        }
    }

    // Signal Engine that config has changed
    ConfigEvent event;
    if (event.Initialize()) {
        event.Signal();
    }
}

void SettingsDialog::saveToToml() {
    std::wstring path = ConfigManager::GetConfigPath();
    if (!ConfigManager::SaveToFile(path, config_)) {
        OutputDebugStringW(L"NexusKey: Failed to save config file\n");
    }

    // Save hotkey config (sync switchKeyChar_ → hotkeyConfig_.key)
    hotkeyConfig_.key = switchKeyChar_.empty() ? 0 : switchKeyChar_[0];
    (void)ConfigManager::SaveHotkeyConfig(path, hotkeyConfig_);

    configDirty_ = false;
}

void SettingsDialog::saveUISettings() {
    UIConfig uiConfig;
    uiConfig.showAdvanced = isExpanded_;
    uiConfig.backgroundOpacity = backgroundOpacity_;
    uiConfig.pinned = isPinned_;

    std::wstring path = ConfigManager::GetConfigPath();
    if (!ConfigManager::SaveUIConfig(path, uiConfig)) {
        OutputDebugStringW(L"NexusKey: Failed to save UI config\n");
    }
}

}  // namespace NextKey
