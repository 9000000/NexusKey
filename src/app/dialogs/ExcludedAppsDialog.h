// NexusKey - Excluded Apps Dialog Header
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "WindowPickerDialog.h"
#include <string>
#include <vector>

namespace NextKey {

/// Dialog for managing excluded apps list (Sciter subdialog)
class ExcludedAppsDialog : public WindowPickerDialog {
public:
    ExcludedAppsDialog(HWND parent);

    bool handle_event(HELEMENT he, BEHAVIOR_EVENT_PARAMS& params) override;

protected:
    void onWindowPicked(const std::wstring& exeName) override;

private:
    void populateList();
    void addApp(const std::wstring& name);
    void removeApp(const std::wstring& name);
    void persistAndSignal();
    void importApps();
    void exportApps();

    std::vector<std::wstring> appList_;
};

}  // namespace NextKey
