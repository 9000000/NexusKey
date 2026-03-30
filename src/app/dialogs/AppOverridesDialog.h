// NexusKey - App Overrides Dialog Header
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "SciterSubDialog.h"
#include "core/config/ConfigManager.h"
#include <string>
#include <unordered_map>
#include <vector>

#ifndef OCR_NORMAL
#define OCR_NORMAL 32512
#endif

namespace NextKey {

/// Dialog for managing per-app configuration (encoding + input method overrides)
class AppOverridesDialog : public SciterSubDialog {
public:
    AppOverridesDialog(HWND parent);

    bool handle_event(HELEMENT he, BEHAVIOR_EVENT_PARAMS& params) override;

protected:
    LRESULT onCustomMessage(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) override;

private:
    void populateList();
    void addEntry(const std::wstring& name, int8_t encoding, int8_t inputMethod);
    void removeEntry(const std::wstring& name);
    void persistAndSignal();
    std::vector<std::wstring> getRunningApps();

    void startWindowPicking();
    void stopWindowPicking();
    std::wstring getExeNameFromWindow(HWND hwnd);

    std::unordered_map<std::wstring, AppOverrideEntry> entries_;

    bool isPickingWindow_ = false;
    HCURSOR savedArrowCursor_ = nullptr;
};

}  // namespace NextKey
