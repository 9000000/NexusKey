# Startup Mode Setting

**Issue**: [#73](https://github.com/phatMT97/NexusKey/issues/73)
**Date**: 2026-04-12

## Problem

When NexusKey auto-starts with Windows, Smart Switch loads persisted per-app English-mode apps from TOML. The first focus event (typically `explorer.exe` desktop window) triggers Smart Switch to flip from V to E mode. Users see E mode on startup without understanding why.

Root cause chain:
1. `InitDefaults()` sets `VIETNAMESE_MODE = true` (V mode)
2. `HookEngine::Start()` calls `LoadEnglishModeApps()` — fills `appModeMap_` from TOML
3. First `EVENT_SYSTEM_FOREGROUND` → `OnFocusChanged()` for desktop (`Progman`/`WorkerW` = `explorer.exe`)
4. `Progman`/`WorkerW` is NOT filtered by `IsTrayOrTaskbarWindow()` (only `Shell_TrayWnd` is filtered)
5. Smart Switch finds `explorer.exe` in `appModeMap_` as English → flips `vietnameseMode_ = false`
6. User sees E mode within ~100-500ms of startup

## Solution

Add a "Chế độ mặc định" (Default mode) dropdown setting with 3 options. When user selects "Always V" or "Always E", Smart Switch reverts to RAM-only behavior (no TOML persistence for per-app modes).

## Config

### SystemConfig.h

New field in `SystemConfig` struct:

```cpp
uint8_t startupMode = 0;  // 0=Vietnamese, 1=English, 2=Remember (Smart Switch persist)
```

### TOML

```toml
[system]
startup_mode = 0
```

## Behavior Matrix

| startupMode | Initial mode | LoadEnglishModeApps() | SaveEnglishModeAppsIfDirty() | Smart Switch in session |
|---|---|---|---|---|
| 0 (Vietnamese) | V | Skip | Skip | Active (RAM-only) |
| 1 (English) | E | Skip | Skip | Active (RAM-only) |
| 2 (Remember) | From TOML | Load | Save | Active + persist |

Key: Only `LoadEnglishModeApps()` and `SaveEnglishModeAppsIfDirty()` are gated — NOT the entire TOML config load/save. All other config (typing, hotkeys, system settings) loads normally.

When `appModeMap_` is empty (startupMode 0 or 1), Smart Switch line 1892 inherits current mode for all unknown apps — correct behavior.

## UI

### Placement — Tab 2 (Hệ thống), below "Khởi động cùng Windows"

```
☑ Khởi động cùng Windows
   Chế độ mặc định: [Tiếng Việt ▼]     ← NEW
☑ Bật bảng này khi khởi động
☐ Chạy với quyền Admin
☐ Tạo biểu tượng desktop
```

### Dropdown Options

| Value | Vietnamese | English |
|---|---|---|
| 0 | Tiếng Việt | Vietnamese |
| 1 | Tiếng Anh | English |
| 2 | Ghi nhớ | Remember last |

### Visibility

- Always visible (not gated by "Khởi động cùng Windows" or Smart Switch toggles)
- Applies every time the app starts (not just auto-start with Windows)
- Indented under "Khởi động cùng Windows" to show semantic grouping

## Code Changes

### 1. SystemConfig.h — Add field

```cpp
uint8_t startupMode = 0;  // 0=Vietnamese, 1=English, 2=Remember
```

### 2. ConfigManager.cpp — Load/save

Load (in system section):
```cpp
config.startupMode = static_cast<uint8_t>((*system)["startup_mode"].value_or(0));
```

Save (in system section):
```cpp
system.insert_or_assign("startup_mode", static_cast<unsigned>(config.startupMode));
```

### 3. main.cpp — Override initial mode

After loading config, before creating UI components:

```cpp
bool startVietnamese = (systemConfig.startupMode != 1);
```

SharedState initialization:
```cpp
SharedState state;
state.InitDefaults();
if (!startVietnamese) {
    state.flags &= ~SharedFlags::VIETNAMESE_MODE;
}
```

Pass initial mode to components:
```cpp
g_trayIcon.Create(hInstance, startVietnamese);
// FloatingIcon: same pattern
g_hookEngine.Start(hInstance, config, hotkeyConfig, startVietnamese);
```

### 4. main_lite.cpp — Same logic as main.cpp

### 5. TrayIcon.cpp/h — Accept initial mode in Create()

```cpp
bool Create(HINSTANCE hInstance, bool initialVietnamese = true);
```

Set `vietnameseMode_` before `RefreshIcon()` in Create() so the correct icon is shown from the first frame.

### 6. HookEngine.h — Add member

```cpp
uint8_t startupMode_ = 0;
```

### 7. HookEngine.cpp — Gate Smart Switch persistence

Start() signature change:
```cpp
bool Start(HINSTANCE hInstance, const TypingConfig& config,
           const HotkeyConfig& hotkey, bool initialVietnamese = true);
```

Set initial mode:
```cpp
vietnameseMode_ = initialVietnamese;
```

Gate TOML load (line ~96):
```cpp
if (smartSwitch_ && startupMode_ == 2) {
    (void)smartSwitchMgr_.Create();
    auto englishApps = ConfigManager::LoadEnglishModeApps(ConfigManager::GetConfigPath());
    // ... existing load logic
}
```

Gate TOML save (line ~181 in Stop()):
```cpp
if (startupMode_ == 2) {
    SaveEnglishModeAppsIfDirty();
}
```

### 8. SettingMetadata.h — Add dropdown entry

New entry in `kSettings[]`, Tab 2, col 0, after `run-startup`:

```cpp
NK_DROPDOWN("startup-mode", startupMode,
            "Chế độ mặc định", "Default mode",
            L"Chế độ gõ khi khởi động ứng dụng",
            L"Typing mode when app starts",    IDC_COMBO_STARTUP_MODE, 2, 0),
```

### 9. settings.html — Add dropdown row

Below "Khởi động cùng Windows" row, indented:

```html
<div class="setting-row" style="padding-left: 16px">
    <span class="setting-label" data-i18n="s.startup_mode">Chế độ mặc định</span>
    <div class="spacer" style="width: *"></div>
    <select id="startup-mode">
        <option value="0" data-i18n="s.startup_mode_v">Tiếng Việt</option>
        <option value="1" data-i18n="s.startup_mode_e">Tiếng Anh</option>
        <option value="2" data-i18n="s.startup_mode_remember">Ghi nhớ</option>
    </select>
</div>
```

### 10. strings.js — Add i18n strings

```js
"s.startup_mode": "Default mode",
"s.startup_mode_v": "Vietnamese",
"s.startup_mode_e": "English",
"s.startup_mode_remember": "Remember last",
```

### 11. SettingsDialog.cpp — Handle dropdown

- Load: set dropdown value from `systemConfig_.startupMode`
- Change: save to `systemConfig_.startupMode`, call `saveSystemSettings()`

### 12. ClassicSettingsDialog.cpp + resource.h — Handle dropdown (Classic UI)

- Add `IDC_COMBO_STARTUP_MODE` to resource.h
- Create ComboBox in system tab
- Load/save same as Sciter dialog

## Cold Boot Verification

### startupMode=0 (Always Vietnamese)
```
T=0ms    InitDefaults() → V mode, SharedState V ✅
T=5ms    TrayIcon.Create(hInstance, true) → V icon ✅
T=10ms   HookEngine.Start(..., true) → vietnameseMode_=true, skip LoadEnglishModeApps
T=300ms  First focus (explorer.exe) → appModeMap_ empty → inherit V ✅
         User sees V from start to finish ✅
```

### startupMode=1 (Always English)
```
T=0ms    InitDefaults() then clear VIETNAMESE_MODE → E mode in SharedState ✅
T=5ms    TrayIcon.Create(hInstance, false) → E icon ✅
T=10ms   HookEngine.Start(..., false) → vietnameseMode_=false, skip LoadEnglishModeApps
T=300ms  First focus (explorer.exe) → appModeMap_ empty → inherit E ✅
         User sees E from start to finish ✅
```

### startupMode=2 (Remember)
```
T=0ms    InitDefaults() → V mode (default)
T=5ms    TrayIcon.Create(hInstance, true) → V icon
T=10ms   HookEngine.Start(..., true) → LoadEnglishModeApps() → appModeMap_ filled
T=300ms  First focus (explorer.exe) → appModeMap_ has entry → may flip to E
         Current behavior preserved ✅
```

## Files Touched

| File | Change |
|---|---|
| `src/core/SystemConfig.h` | +1 field |
| `src/core/config/ConfigManager.cpp` | +2 lines (load/save) |
| `src/core/config/SettingMetadata.h` | +1 dropdown entry |
| `src/app/main.cpp` | +5 lines (override mode, pass to Create/Start) |
| `src/app/main_lite.cpp` | +5 lines (same) |
| `src/app/system/HookEngine.h` | +1 member |
| `src/app/system/HookEngine.cpp` | +2 guards, modify Start() signature |
| `src/app/system/TrayIcon.cpp` | Modify Create() to accept initial mode |
| `src/app/system/TrayIcon.h` | Update Create() signature |
| `src/app/ui/settings/settings.html` | +1 dropdown row |
| `src/app/ui/shared/strings.js` | +4 i18n strings |
| `src/app/dialogs/SettingsDialog.cpp` | Handle dropdown change + load |
| `src/app/classic/ClassicSettingsDialog.cpp` | Handle dropdown (Classic UI) |
| `src/app/classic/resource.h` | +1 IDC constant |
