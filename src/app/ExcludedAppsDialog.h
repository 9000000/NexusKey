// NexusKey - Excluded Apps Dialog Header
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "SciterSubDialog.h"
#include <functional>
#include <string>
#include <vector>

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

private:
    void populateList();
    void addApp(const std::wstring& name);
    void removeApp(const std::wstring& name);
    std::vector<std::wstring> getRunningApps();

    std::vector<std::wstring> appList_;
    ExcludedAppsChangedCallback onChanged_;
};

}  // namespace NextKey
