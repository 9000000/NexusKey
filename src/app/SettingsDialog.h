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

    // SOM functions exposed to JavaScript
    void onInputMethodChange(int method);  // 0=Telex, 1=VNI
    void onSpellCheckChange(bool enabled);
    void onClose();

    // SOM passport for JavaScript binding
    SOM_PASSPORT_BEGIN(SettingsDialog)
        SOM_FUNCS(
            SOM_FUNC(onInputMethodChange),
            SOM_FUNC(onSpellCheckChange),
            SOM_FUNC(onClose)
        )
    SOM_PASSPORT_END

private:
    void loadSettings();
    void saveSettings();

    // Subclass procedure for window dragging and close
    static LRESULT CALLBACK SubclassProc(HWND hwnd, UINT msg, WPARAM wParam, 
                                         LPARAM lParam, UINT_PTR uIdSubclass, 
                                         DWORD_PTR dwRefData);

    SettingsChangedCallback onSettingsChanged_;
    int currentMethod_ = 0;      // 0=Telex, 1=VNI
    bool spellCheck_ = false;
};

}  // namespace NextKey
