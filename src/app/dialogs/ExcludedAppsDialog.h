// NexusKey - Excluded Apps Dialog Header
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "SciterSubDialog.h"
#include <functional>
#include <string>
#include <vector>

#ifndef OCR_NORMAL
#define OCR_NORMAL 32512
#endif

namespace NextKey {

/// Callback when excluded apps list changes
using ExcludedAppsChangedCallback = std::function<void()>;

/// Dialog for managing excluded apps list (Sciter subdialog)
class ExcludedAppsDialog : public SciterSubDialog {
public:
    ExcludedAppsDialog(HWND parent);

    /// Set callback for when list changes
    void SetOnChanged(ExcludedAppsChangedCallback callback) { onChanged_ = std::move(callback); }

    // Override event handler for VALUE_CHANGED on #val-action
    bool handle_event(HELEMENT he, BEHAVIOR_EVENT_PARAMS& params) override;

protected:
    void onBeforeClose() override;
    LRESULT onCustomMessage(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) override;

private:
    void populateList();
    void addApp(const std::wstring& name);
    void removeApp(const std::wstring& name);
    void persistAndSignal();
    std::vector<std::wstring> getRunningApps();

    void startWindowPicking();
    void stopWindowPicking();
    std::wstring getExeNameFromWindow(HWND hwnd);

    std::vector<std::wstring> appList_;
    ExcludedAppsChangedCallback onChanged_;

    bool isPickingWindow_ = false;
    HCURSOR savedArrowCursor_ = nullptr;
};

}  // namespace NextKey
