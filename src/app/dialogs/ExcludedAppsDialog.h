// NexusKey - Excluded Apps Dialog Header
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "SciterSubDialog.h"
#include <string>
#include <vector>

#ifndef OCR_NORMAL
#define OCR_NORMAL 32512
#endif

namespace NextKey {

/// Dialog for managing excluded apps list (Sciter subdialog)
class ExcludedAppsDialog : public SciterSubDialog {
public:
    ExcludedAppsDialog(HWND parent);

    bool handle_event(HELEMENT he, BEHAVIOR_EVENT_PARAMS& params) override;

protected:
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

    bool isPickingWindow_ = false;
    HCURSOR savedArrowCursor_ = nullptr;
};

}  // namespace NextKey
