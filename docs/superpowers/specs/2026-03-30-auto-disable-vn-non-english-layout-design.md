# Design: Auto-disable Vietnamese for Incompatible Keyboard Layouts

**Date:** 2026-03-30
**Status:** Approved (rev 2 — post-review corrections)
**Feature flag:** On by default (no opt-in required)

---

## Problem

Users who switch between Latin-alphabet and non-Latin keyboard layouts (e.g. Japanese, Chinese, Korean) must perform two steps when switching to Japanese:
1. Switch Windows input language to JAP
2. Manually toggle NexusKey from Vietnamese → English

Step 2 is redundant — Vietnamese input cannot work on a Japanese/CJK layout anyway. NexusKey should detect the layout change automatically and switch to English mode without user intervention.

---

## Behavior Specification

### Layout → incompatible (JAP, CN, KR, etc.)
- NexusKey auto-switches to English mode immediately
- Tray icon updates to English state (same as manual toggle)
- `preLayoutSwitchMode_` saves the current V/E mode for later restore
- `layoutForcedEnglish_` flag is set to `true`

### Layout → compatible (ENG, FR, DE, VI, etc. restored)
- `layoutForcedEnglish_` cleared
- **Smart switch ON:** restore `preLayoutSwitchMode_` (the mode that was active before the CJK switch)
- **Smart switch OFF:** stay in English — user must manually toggle back

### Manual override while on incompatible layout
If the user manually toggles mode via tray icon or hotkey while `layoutForcedEnglish_` is true, the flag is cleared immediately. The user's explicit intent overrides the auto-detection. On the next layout change event, detection resumes normally.

### Definition of "incompatible layout" — CJK blacklist
Rather than whitelisting English, we blacklist known incompatible languages. This ensures users on French AZERTY, German QWERTZ, the Windows built-in Vietnamese layout (`LANG_VIETNAMESE` 0x2A), or any other Latin-alphabet layout are unaffected.

```cpp
static bool IsIncompatibleLayout(HKL hkl) {
    WORD langId = PRIMARYLANGID(LOWORD(reinterpret_cast<DWORD_PTR>(hkl)));
    return langId == LANG_JAPANESE        // 0x11
        || langId == LANG_CHINESE         // 0x04 (covers both Simplified & Traditional)
        || langId == LANG_KOREAN;         // 0x12
}
```

Additional languages (Arabic, Thai, Russian, etc.) can be added to this list later based on user feedback without architectural changes.

---

## Architecture

### Detection Strategy: update in OnFocusChanged + modifier key-up re-check

**Hot path (ProcessKeyDown) — zero API calls:**
```cpp
// ProcessKeyDown simply reads the cached bool — no syscalls
if (!cachedIsCompatLayout_) {
    OnLayoutChanged(false);  // already forced English, but handle any pending transitions
    return false;            // passthrough
}
```

The cached value is set exclusively in two places:

**1. OnFocusChanged (covers all cross-window focus changes):**
```cpp
void HookEngine::OnFocusChanged() {
    // ... existing focus logic ...
    HWND fg = GetForegroundWindow();
    if (fg) {
        DWORD tid = GetWindowThreadProcessId(fg, nullptr);
        HKL hkl = GetKeyboardLayout(tid);
        bool compatible = !IsIncompatibleLayout(hkl);
        if (compatible != cachedIsCompatLayout_) {
            cachedIsCompatLayout_ = compatible;
            OnLayoutChanged(compatible);
        }
    }
}
```

**2. ProcessKeyUp modifier re-check (covers in-window layout switches):**

Re-check layout when a layout-switch shortcut modifier is released. This catches Win+Space, Ctrl+Shift, and Alt+Shift — the three Windows layout-switch shortcuts — without polling or background timers.

```cpp
void HookEngine::ProcessKeyUp(DWORD vkCode) {
    TrackModifier(vkCode, false);

    // Re-check layout when a layout-switch shortcut modifier is released
    bool wasWin    = (vkCode == VK_LWIN  || vkCode == VK_RWIN);
    bool wasShift  = (vkCode == VK_LSHIFT || vkCode == VK_RSHIFT);
    bool wasAlt    = (vkCode == VK_LMENU  || vkCode == VK_RMENU);
    bool wasCtrl   = (vkCode == VK_LCONTROL || vkCode == VK_RCONTROL);

    bool triggerCheck = wasWin
        || (wasShift && (modifiers_ & MOD_CTRL))
        || (wasShift && (modifiers_ & MOD_ALT))
        || (wasCtrl  && (modifiers_ & MOD_SHIFT))
        || (wasAlt   && (modifiers_ & MOD_SHIFT));

    if (triggerCheck) {
        HWND fg = GetForegroundWindow();
        if (fg) {
            DWORD tid = GetWindowThreadProcessId(fg, nullptr);
            HKL hkl = GetKeyboardLayout(tid);
            bool compatible = !IsIncompatibleLayout(hkl);
            if (compatible != cachedIsCompatLayout_) {
                cachedIsCompatLayout_ = compatible;
                OnLayoutChanged(compatible);
            }
        }
    }
}
```

> **Note on WM_INPUTLANGCHANGE:** This message is sent to the foreground application's window, not broadcast to all windows. NexusKey's tray HWND does not have keyboard focus during normal use, so it cannot reliably receive this message for other apps' layout changes. The modifier key-up approach above covers all three Windows layout-switch shortcuts reliably.

### New fields in HookEngine

```cpp
bool layoutForcedEnglish_  = false;  // True when auto-switched to English due to incompatible layout
bool preLayoutSwitchMode_  = false;  // Saved V/E mode before forced switch
bool cachedIsCompatLayout_ = true;   // Last known layout compatibility result
```

### Transition Logic

```cpp
void HookEngine::OnLayoutChanged(bool isCompatibleNow) {
    if (!isCompatibleNow && !layoutForcedEnglish_) {
        // Compatible → incompatible: save mode, force English
        preLayoutSwitchMode_ = vietnameseMode_;
        layoutForcedEnglish_ = true;
        SetVietnameseMode(false);
    } else if (isCompatibleNow && layoutForcedEnglish_) {
        // Incompatible → compatible: restore
        layoutForcedEnglish_ = false;
        if (smartSwitch_) {
            SetVietnameseMode(preLayoutSwitchMode_);
        }
        // smartSwitch OFF: do nothing, stay English
    }
}
```

### Manual toggle clears forced state

```cpp
void HookEngine::ToggleVietnameseMode() {
    layoutForcedEnglish_ = false;  // User explicitly overriding — clear forced state
    // ... existing toggle logic ...
}
```

### Guard in OnFocusChanged

When `layoutForcedEnglish_` is true, `OnFocusChanged` must NOT save the current (forced-English) mode to `appModeMap_`. Without this guard, smart switch would permanently remember this app as "prefers English" even though the user was only in English because of the CJK layout.

```cpp
// In OnFocusChanged, before saving to appModeMap_:
if (smartSwitch_ && !layoutForcedEnglish_ && !wasExcluded && !wasTsfApp) {
    appModeMap_[currentExe_] = vietnameseMode_;
    smartSwitchMgr_.SetAppMode(currentExe_, vietnameseMode_);
}
```

---

## UX

- **Tray icon:** updates immediately on layout change — same English/Vietnamese icons as manual toggle. No new icon states needed.
- **No notification:** icon change alone is sufficient feedback.
- **No settings toggle needed:** behavior is always-on. It is the correct behavior for all users (Vietnamese input on CJK layouts is impossible anyway).
- **Manual override respected:** user can force Vietnamese mode even on CJK layout; auto-detection resumes on next layout change.

---

## Performance

| Scenario | Overhead |
|---|---|
| Normal typing (99% of keystrokes) | Read 1 cached bool — **zero API calls** |
| Window focus change | `GetForegroundWindow()` + `GetWindowThreadProcessId()` + `GetKeyboardLayout()` — already in `OnFocusChanged()`, no additional cost |
| In-window layout switch (Win+Space / Ctrl+Shift / Alt+Shift) | 3 API calls on modifier key-up only — not on regular keystrokes |

---

## Files to Change

| File | Change |
|---|---|
| `src/app/system/HookEngine.h` | Add 3 new fields, declare `OnLayoutChanged()`, declare `IsIncompatibleLayout()` |
| `src/app/system/HookEngine.cpp` | Layout check in `OnFocusChanged`, `ProcessKeyUp` modifier re-check, `OnLayoutChanged()`, guard in smart switch save, clear flag in `ToggleVietnameseMode()` |
| `src/app/system/HookEngine.cpp` | `Reset()` — clear `layoutForcedEnglish_`, `cachedIsCompatLayout_ = true` |

No SharedState changes required — `SetVietnameseMode()` already updates SharedState and tray icon.

---

## Out of Scope

- Per-layout mode memory (e.g. "always Vietnamese on ENG, always English on DE") — not requested
- Settings UI toggle for this feature — always-on by design
- Notification/toast when layout switches — icon change is sufficient
- Extending blacklist beyond CJK (Arabic, Thai, Russian, etc.) — add later based on user feedback
