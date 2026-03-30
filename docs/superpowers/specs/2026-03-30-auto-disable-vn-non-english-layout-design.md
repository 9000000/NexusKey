# Design: Auto-disable Vietnamese for Non-English Keyboard Layouts

**Date:** 2026-03-30
**Status:** Approved
**Feature flag:** On by default (no opt-in required)

---

## Problem

Users who switch between English and non-Latin keyboard layouts (e.g. Japanese, Chinese, Korean) must perform two steps when switching to Japanese:
1. Switch Windows input language to JAP
2. Manually toggle NexusKey from Vietnamese → English

Step 2 is redundant — Vietnamese input cannot work on a Japanese layout anyway. NexusKey should detect the layout change automatically and switch to English mode without user intervention.

---

## Behavior Specification

### Layout → English (JAP, CN, KR, etc.)
- NexusKey auto-switches to English mode immediately
- Tray icon updates to English state (same as manual toggle)
- `preLayoutSwitchMode_` saves the current V/E mode for later restore
- `layoutForcedEnglish_` flag is set to `true`

### Layout → English (ENG restored)
- `layoutForcedEnglish_` cleared
- **Smart switch ON:** restore `preLayoutSwitchMode_` (the mode that was active before the JAP switch)
- **Smart switch OFF:** stay in English — user must manually toggle back

### Definition of "English layout"
Check: `PRIMARYLANGID(LOWORD(hkl)) == LANG_ENGLISH` (0x09)
This covers all English variants (en-US, en-GB, en-AU, etc.) and excludes all CJK and other non-Latin-alphabet IME languages.

---

## Architecture

### Detection Strategy: TID-cached per-keystroke check

**Hot path (ProcessKeyDown):**
```
cachedLayoutTid_  — foreground window thread ID from last check
cachedIsEngLayout_ — result of last GetKeyboardLayout check

On each ProcessKeyDown:
  tid = GetWindowThreadProcessId(GetForegroundWindow())
  if tid == cachedLayoutTid_:
      use cachedIsEngLayout_  ← zero syscalls
  else:
      hkl = GetKeyboardLayout(tid)
      cachedIsEngLayout_ = (PRIMARYLANGID(LOWORD(hkl)) == LANG_ENGLISH)
      cachedLayoutTid_ = tid
      → trigger transition logic if layout changed
```

**In-window layout switch (Win+Space / Ctrl+Shift without focus change):**
Re-check layout when a layout-switch modifier is released:
- `VK_LWIN` / `VK_RWIN` key-up
- `VK_LCONTROL` / `VK_RCONTROL` + `VK_LSHIFT` / `VK_RSHIFT` key-up combo

This covers the practical cases without polling or background timers.

### New fields in HookEngine

```cpp
bool layoutForcedEnglish_ = false;   // True when we forced English due to non-ENG layout
bool preLayoutSwitchMode_ = false;   // Saved V/E mode before forced switch
bool cachedIsEngLayout_ = true;      // Last known layout result
DWORD cachedLayoutTid_ = 0;          // TID used for last layout check
```

### Guard in OnFocusChanged

When `layoutForcedEnglish_` is true, `OnFocusChanged` must NOT save the current (forced-English) mode to `appModeMap_`. Without this guard, smart switch would permanently remember this app as "prefers English" even though the user was only in English because of the JAP layout.

```cpp
// In OnFocusChanged, before saving to appModeMap_:
if (smartSwitch_ && !layoutForcedEnglish_ && !wasExcluded && !wasTsfApp) {
    appModeMap_[currentExe_] = vietnameseMode_;
    ...
}
```

### Transition Logic

```cpp
void HookEngine::OnLayoutChanged(bool isEnglishNow) {
    if (!isEnglishNow && !layoutForcedEnglish_) {
        // ENG → non-ENG: save mode, force English
        preLayoutSwitchMode_ = vietnameseMode_;
        layoutForcedEnglish_ = true;
        SetVietnameseMode(false);
    } else if (isEnglishNow && layoutForcedEnglish_) {
        // non-ENG → ENG: restore
        layoutForcedEnglish_ = false;
        if (smartSwitch_) {
            SetVietnameseMode(preLayoutSwitchMode_);
        }
        // smartSwitch OFF: do nothing, stay English
    }
}
```

---

## UX

- **Tray icon:** updates immediately on layout change — same English/Vietnamese icons as manual toggle. No new icon states needed.
- **No notification:** icon change alone is sufficient feedback. Users understand the mental model.
- **No settings toggle needed:** behavior is always-on. It is the correct behavior for all users (typing Vietnamese on a Japanese keyboard layout is impossible anyway).

---

## Performance

| Scenario | Overhead |
|---|---|
| Same window, same layout (99% of keystrokes) | 1 integer comparison (`tid == cachedLayoutTid_`) |
| Window focus change | `GetForegroundWindow()` + `GetWindowThreadProcessId()` + `GetKeyboardLayout()` — already called in `OnFocusChanged()` |
| In-window layout switch via Win/Ctrl+Shift | Re-query on modifier key-up only |

Total hot-path overhead for normal typing: **~0 ns** (single cached comparison).

---

## Files to Change

| File | Change |
|---|---|
| `src/app/system/HookEngine.h` | Add 4 new fields, declare `OnLayoutChanged()` |
| `src/app/system/HookEngine.cpp` | TID-cache check in `ProcessKeyDown`, modifier key-up re-check, `OnLayoutChanged()`, guard in `OnFocusChanged` |
| `src/app/system/HookEngine.cpp` | `Reset()` / `ResetComposition()` — clear `layoutForcedEnglish_`, `cachedLayoutTid_` |

No SharedState changes required — `SetVietnameseMode()` already updates SharedState and tray icon.

---

## Out of Scope

- Per-layout mode memory (e.g. "always Vietnamese on ENG, always English on DE") — not requested
- Settings UI toggle for this feature — always-on by design
- Notification/toast when layout switches — icon change is sufficient
