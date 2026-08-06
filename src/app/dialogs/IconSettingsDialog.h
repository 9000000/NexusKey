// VKey - Icon Settings Dialog
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "SciterSubDialog.h"
#include "core/SystemConfig.h"

namespace NextKey {

/// Focused sub-dialog for the three icon-related settings that would otherwise
/// crowd the main System tab: tray style, TSF indicator, and floating V/E icon.
class IconSettingsDialog final : public SciterSubDialog {
public:
    explicit IconSettingsDialog(HWND parent);

    bool handle_event(HELEMENT he, BEHAVIOR_EVENT_PARAMS& params) override;

private:
    void populate();
    void saveAndNotify(WPARAM iconChangeFlags = 0);
    void openColorPicker(bool forVietnamese);
    void updateColorSwatches();
    void setToggleState(const char* id, bool checked);
    void setDropdownValue(const char* id, int value);
    void setCustomColorsVisible(bool visible);

    SystemConfig systemConfig_;
    bool customColorsVisible_ = false;
};

}  // namespace NextKey
