// NexusKey - User Defined Input Dialog Header
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "SciterSubDialog.h"
#include "core/engine/TypingAction.h"
#include <string>
#include <array>

namespace NextKey {

/// Dialog for managing user-defined input method keymap (Sciter subdialog)
class UserDefinedDialog : public SciterSubDialog {
public:
    UserDefinedDialog(HWND parent);

    // Override event handler for VALUE_CHANGED on #val-action
    bool handle_event(HELEMENT he, BEHAVIOR_EVENT_PARAMS& params) override;

private:
    void populateList();
    void persistAndSignal();
    void importKeyMap();
    void exportKeyMap();
    void loadTemplate(bool telex);

    std::array<TypingAction, 128> keyMap_;
};

}  // namespace NextKey
