# Auto-disable Vietnamese for Incompatible Keyboard Layouts — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** When the user switches to a CJK keyboard layout (Japanese/Chinese/Korean), NexusKey automatically switches to English mode; when they return to a compatible layout, the previous mode is restored (if smart switch is ON).

**Architecture:** Three-part change — (1) cache layout compatibility in `OnFocusChanged` and on modifier key-up, (2) `OnLayoutChanged()` drives mode transitions, (3) `layoutForcedEnglish_` guards smart switch from saving the forced state. Hot path reads one cached bool, zero API calls.

**Tech Stack:** C++20, Win32 (`GetKeyboardLayout`, `GetWindowThreadProcessId`, `GetForegroundWindow`), existing `modeChangeCallback_` pattern.

---

## File Map

| File | What changes |
|---|---|
| `src/app/system/HookEngine.h` | Add 3 fields + declare `OnLayoutChanged()` |
| `src/app/system/HookEngine.cpp` | `IsIncompatibleLayout()` helper, `OnLayoutChanged()`, update `ToggleVietnameseMode()`, `OnFocusChanged()`, `ProcessKeyUp()` |

No new files. No SharedState changes — `modeChangeCallback_` already propagates mode to tray + SharedState.

---

### Task 1: Add fields and declare OnLayoutChanged in HookEngine.h

**Files:**
- Modify: `src/app/system/HookEngine.h`

- [ ] **Step 1: Add 3 new fields to private section**

In `HookEngine.h`, after line 230 (`bool otherKeyPressed_ = false;`), add:

```cpp
    // Layout auto-disable: pause Vietnamese when CJK keyboard layout is active
    bool layoutForcedEnglish_  = false;  // True when auto-switched to English due to CJK layout
    bool preLayoutSwitchMode_  = false;  // V/E mode saved before forced switch
    bool cachedIsCompatLayout_ = true;   // Last known layout compatibility (updated in OnFocusChanged + key-up)
```

- [ ] **Step 2: Declare OnLayoutChanged in private methods**

In `HookEngine.h`, after the line `void OnFocusChanged();` (line 139), add:

```cpp
    void OnLayoutChanged(bool isCompatibleNow);
```

- [ ] **Step 3: Build to confirm header compiles**

```bash
cmake --build build-linux --target NextKeyTests -j4 2>&1 | tail -3
```
Expected: `[100%] Built target NextKeyTests` (Linux tests don't compile HookEngine but confirms no header parse errors in shared includes)

---

### Task 2: Add IsIncompatibleLayout helper and OnLayoutChanged implementation

**Files:**
- Modify: `src/app/system/HookEngine.cpp`

- [ ] **Step 1: Add IsIncompatibleLayout as a file-local helper**

In `HookEngine.cpp`, find the anonymous-namespace or file-level helper section near the top (around the `IsConsoleApp`, `IsQtElectronApp` helpers). Add after the existing helpers:

```cpp
/// Returns true when the keyboard layout cannot produce Vietnamese input.
/// Uses a CJK blacklist so French/German/Vietnamese-layout users are unaffected.
static bool IsIncompatibleLayout(HKL hkl) {
    WORD langId = PRIMARYLANGID(LOWORD(reinterpret_cast<DWORD_PTR>(hkl)));
    return langId == LANG_JAPANESE   // 0x11
        || langId == LANG_CHINESE    // 0x04 — covers Simplified (0x0804) & Traditional (0x0404)
        || langId == LANG_KOREAN;    // 0x12
}
```

- [ ] **Step 2: Add OnLayoutChanged method implementation**

Find `void HookEngine::OnFocusChanged()` (line ~1257). Add the new method **before** it:

```cpp
void HookEngine::OnLayoutChanged(bool isCompatibleNow) {
    if (!isCompatibleNow && !layoutForcedEnglish_) {
        // Compatible → incompatible (CJK): save mode, force English
        if (engine_->Count() > 0) CommitComposition();
        CancelCommitUndo();
        preLayoutSwitchMode_ = vietnameseMode_;
        layoutForcedEnglish_ = true;
        vietnameseMode_ = false;
        HOOK_LOG(L"  LayoutAutoDisable: CJK layout detected, forcing English (saved mode=%d)",
                 preLayoutSwitchMode_ ? 1 : 0);
        if (modeChangeCallback_) modeChangeCallback_(false);
    } else if (isCompatibleNow && layoutForcedEnglish_) {
        // Incompatible → compatible: restore saved mode if smart switch is on
        layoutForcedEnglish_ = false;
        if (smartSwitch_) {
            vietnameseMode_ = preLayoutSwitchMode_;
            HOOK_LOG(L"  LayoutAutoDisable: compatible layout restored, mode=%d",
                     vietnameseMode_ ? 1 : 0);
            if (modeChangeCallback_) modeChangeCallback_(vietnameseMode_);
        }
        // smartSwitch OFF: stay in English, user must manually toggle back
    }
}
```

---

### Task 3: Clear layoutForcedEnglish_ on manual toggle

**Files:**
- Modify: `src/app/system/HookEngine.cpp` — `ToggleVietnameseMode()` at line ~194

When the user manually toggles mode via hotkey or tray icon, their explicit intent overrides the auto-detection.

- [ ] **Step 1: Add the clear at the start of ToggleVietnameseMode**

In `ToggleVietnameseMode()`, after the excluded-app early-return block (after line 198), add one line:

```cpp
void HookEngine::ToggleVietnameseMode() {
    // Block toggling in excluded apps
    if (excludeApps_ && isExcludedApp_) {
        HOOK_LOG(L"  ToggleVietnameseMode: BLOCKED (excluded app '%s')", currentExe_.c_str());
        return;
    }

    layoutForcedEnglish_ = false;  // User overriding auto-detection — clear forced state

    // Commit any pending composition before switching
    if (engine_->Count() > 0) {
```

---

### Task 4: Update OnFocusChanged — layout check + smart switch guard

**Files:**
- Modify: `src/app/system/HookEngine.cpp` — `OnFocusChanged()` at line ~1257

Two changes in this function.

- [ ] **Step 1: Add layout detection after the AppDetect block**

After line 1268 (the `HOOK_LOG AppDetect` line) and before the early-return at line 1271, add:

```cpp
    // Layout auto-disable: check CJK layout on every focus change
    if (fg) {
        DWORD tid = GetWindowThreadProcessId(fg, nullptr);
        HKL hkl = GetKeyboardLayout(tid);
        bool compatible = !IsIncompatibleLayout(hkl);
        if (compatible != cachedIsCompatLayout_) {
            cachedIsCompatLayout_ = compatible;
            OnLayoutChanged(compatible);
        }
    }
```

Result — the relevant section becomes:

```cpp
    HOOK_LOG(L"  AppDetect: console=%d skipEmpty=%d electron=%d",
             isConsoleApp_ ? 1 : 0, skipEmptyChar_ ? 1 : 0, isElectronApp_ ? 1 : 0);

    // Layout auto-disable: check CJK layout on every focus change
    if (fg) {
        DWORD tid = GetWindowThreadProcessId(fg, nullptr);
        HKL hkl = GetKeyboardLayout(tid);
        bool compatible = !IsIncompatibleLayout(hkl);
        if (compatible != cachedIsCompatLayout_) {
            cachedIsCompatLayout_ = compatible;
            OnLayoutChanged(compatible);
        }
    }

    // Skip focus tracking entirely if no feature needs it
    if (!smartSwitch_ && !excludeApps_ && !tsfApps_ && !rememberCodeTable_) return;
```

- [ ] **Step 2: Guard smart switch save with layoutForcedEnglish_**

At line 1277, change the smart switch save condition:

```cpp
    // BEFORE:
    if (smartSwitch_ && !currentExe_.empty() && !wasExcluded && !wasTsfApp) {

    // AFTER:
    if (smartSwitch_ && !layoutForcedEnglish_ && !currentExe_.empty() && !wasExcluded && !wasTsfApp) {
```

This prevents the forced-English mode from being saved as the app's "remembered" mode in smart switch.

---

### Task 5: Add layout re-check in ProcessKeyUp for in-window layout switches

**Files:**
- Modify: `src/app/system/HookEngine.cpp` — `ProcessKeyUp()` at line ~781

Covers Win+Space, Ctrl+Shift, Alt+Shift (the three Windows layout-switch shortcuts) used without changing window focus.

- [ ] **Step 1: Add layout re-check before TrackModifier call**

In `ProcessKeyUp`, find line 846 (`TrackModifier(vkCode, false);`). Add the layout re-check **immediately before** it, still inside the `if (isModifier)` block. At this point modifier bools reflect their pre-release state (TrackModifier hasn't run yet), which is correct for detecting combo releases.

```cpp
        // Layout auto-disable: re-check on Win+Space / Ctrl+Shift / Alt+Shift key-up.
        // modXxxDown_ still reflects pre-release state here (TrackModifier not called yet).
        {
            bool wasWin   = (vkCode == VK_LWIN    || vkCode == VK_RWIN);
            bool wasShift = (vkCode == VK_LSHIFT   || vkCode == VK_RSHIFT);
            bool wasAlt   = (vkCode == VK_LMENU    || vkCode == VK_RMENU);
            bool wasCtrl  = (vkCode == VK_LCONTROL || vkCode == VK_RCONTROL);
            bool triggerCheck = wasWin
                || (wasShift && modCtrlDown_)   // Ctrl+Shift release
                || (wasShift && modAltDown_)    // Alt+Shift release
                || (wasCtrl  && modShiftDown_)  // Ctrl+Shift release (ctrl side)
                || (wasAlt   && modShiftDown_); // Alt+Shift release (alt side)
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

        TrackModifier(vkCode, false);
```

---

### Task 6: Windows build and manual verification

HookEngine is Windows-only — no Linux unit tests apply. Verification is done on a Windows build.

- [ ] **Step 1: Build on Windows**

```powershell
powershell.exe -Command "cd '\\wsl.localhost\Ubuntu-24.04\home\phatmt\code\NexusKey\build'; cmake --build . --target NexusKey --config Debug 2>&1"
```
Expected: Build succeeds, no errors.

- [ ] **Step 2: Run Linux tests to confirm no regression**

```bash
./build-linux/tests/NextKeyTests 2>&1 | tail -3
```
Expected: All tests pass (same count as before).

- [ ] **Step 3: Install and test — Japanese layout switch**

On Windows with Japanese IME installed:
1. Start `NextKeyApp.exe`, confirm tray shows Vietnamese (🇻🇳)
2. Press `Win+Space` to switch to Japanese layout
3. **Expected:** Tray immediately shows English (EN), typing passes through to Japanese IME
4. Press `Win+Space` to switch back to English layout
5. **Expected (smart switch ON):** Tray shows Vietnamese again. **Expected (smart switch OFF):** Tray shows English (manual toggle required)

- [ ] **Step 4: Test manual override on CJK layout**

1. Switch to Japanese layout (tray shows English)
2. Click tray icon to force Vietnamese mode
3. **Expected:** Tray shows Vietnamese, `layoutForcedEnglish_` cleared
4. Switch to English layout and back to Japanese
5. **Expected:** Auto-disables again on next layout switch (detection resumed)

- [ ] **Step 5: Test smart switch guard**

1. Enable smart switch in settings
2. Open Chrome in Vietnamese mode
3. Switch to Japanese layout (auto → English)
4. Switch to Notepad and back to Chrome
5. Switch back to English layout
6. **Expected:** Chrome restores Vietnamese mode (smart switch remembered Vietnamese for Chrome, not the forced-English state)

- [ ] **Step 6: Commit**

```bash
git add src/app/system/HookEngine.h src/app/system/HookEngine.cpp
git commit -m "feat: auto-disable Vietnamese on CJK keyboard layout

When user switches to Japanese/Chinese/Korean layout, NexusKey
automatically switches to English mode. Returns to previous mode
on compatible layout restore if smart switch is enabled.

- IsIncompatibleLayout(): CJK blacklist (JA/ZH/KO)
- OnLayoutChanged(): drives mode transitions, guards smart switch save
- Zero hot-path overhead: cached bool in ProcessKeyDown
- Covers Win+Space / Ctrl+Shift / Alt+Shift in-window layout switches

Co-Authored-By: Claude Sonnet 4.6 <noreply@anthropic.com>"
```
