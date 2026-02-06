// NexusKey - Settings Dialog Header
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

// Undefine Windows macros that conflict with Sciter enums
#ifdef KEY_DOWN
#undef KEY_DOWN
#endif
#ifdef KEY_UP
#undef KEY_UP
#endif

#include "sciter-x-window.hpp"
#include <functional>
#include <string>

namespace NextKey {

/// Callback for when settings change
using SettingsChangedCallback = std::function<void()>;

/// Settings dialog using Sciter for modern UI
class SettingsDialog : public sciter::window {
public:
    SettingsDialog();
    ~SettingsDialog();

    /// Show the settings dialog (blocks until closed)
    void Show();

    /// Set callback for settings changes
    void SetOnSettingsChanged(SettingsChangedCallback callback) { onSettingsChanged_ = callback; }

    // Override to avoid sciter::application dependency
    HINSTANCE get_resource_instance() const { return nullptr; }

    // Override resource loading to support both Debug (file system) and Release (packed)
    virtual LRESULT on_load_data(LPSCN_LOAD_DATA pnmld) override;

    // Override event handler for BUTTON_CLICK, VALUE_CHANGED events
    // Note: Must use handle_event, not on_event, because sciter::window::handle_event
    // doesn't call the base class, so on_event is never invoked.
    virtual bool handle_event(HELEMENT he, BEHAVIOR_EVENT_PARAMS& params) override;

    // SOM functions exposed to JavaScript
    void onInputMethodChange(int method);  // 0=Telex, 1=VNI
    void onSpellCheckChange(bool enabled);
    void onExpandChange(bool expanded);
    void onClose();

    // SOM passport for JavaScript binding
    SOM_PASSPORT_BEGIN(SettingsDialog)
        SOM_FUNCS(
            SOM_FUNC(onInputMethodChange),
            SOM_FUNC(onSpellCheckChange),
            SOM_FUNC(onExpandChange),
            SOM_FUNC(onClose)
        )
    SOM_PASSPORT_END

private:
    void loadSettings();
    void saveSettings();
    void initializeUI();

    // Event handlers for specific settings
    void handleToggleChange(const std::wstring& id, bool value);
    void handleDropdownChange(const std::wstring& id, int value);
    void handleExpandStateChange(bool expanded);
    void handleButtonClick(const std::wstring& id);

    // Window control
    void togglePin();

    // UI helpers
    void setToggleState(const std::wstring& id, bool checked);
    void setDropdownValue(const std::wstring& id, int value);
    void resizeWindow(bool expanded);
    void recalcWindowSize();  // Measure DOM and resize window to fit content

    // Subclass procedure for window dragging and close
    static LRESULT CALLBACK SubclassProc(HWND hwnd, UINT msg, WPARAM wParam,
                                         LPARAM lParam, UINT_PTR uIdSubclass,
                                         DWORD_PTR dwRefData);

    SettingsChangedCallback onSettingsChanged_;

    // Settings state
    int currentMethod_ = 0;      // 0=Telex, 1=VNI, 2=SimpleTelex1, 3=SimpleTelex2
    int codeTable_ = 0;          // 0=Unicode, 1=TCVN3, etc.
    bool spellCheck_ = false;
    bool beepSound_ = false;
    bool smartSwitch_ = false;
    bool excludeApps_ = false;
    bool isExpanded_ = false;
    bool isPinned_ = false;

    // Switch keys
    bool keyCtrl_ = false;
    bool keyAlt_ = false;
    bool keyWin_ = false;
    bool keyShift_ = false;
    std::wstring switchKeyChar_ = L"~";

    // UI base path for file system loading (Debug mode)
    std::wstring uiBasePath_;
};

}  // namespace NextKey
