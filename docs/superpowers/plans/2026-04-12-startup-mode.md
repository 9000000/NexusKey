# Startup Mode Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Let users choose default V/E mode on startup, gating Smart Switch TOML persistence accordingly.

**Architecture:** Add `startupMode` field to `SystemConfig` (0=V, 1=E, 2=Remember). Pass initial mode to `TrayIcon::Create()`, `FloatingIcon::Create()`, and `HookEngine::Start()`. Gate `LoadEnglishModeApps()` and `SaveEnglishModeAppsIfDirty()` on `startupMode == 2`.

**Tech Stack:** C++20, TOML config, Sciter UI, Win32 Classic UI

**Spec:** `docs/superpowers/specs/2026-04-12-startup-mode-design.md`

---

### Task 1: Add startupMode field to SystemConfig + ConfigManager

**Files:**
- Modify: `src/core/SystemConfig.h:28-30`
- Modify: `src/core/config/ConfigManager.cpp:558` (load) and `:584` (save)

- [ ] **Step 1: Add field to SystemConfig**

In `src/core/SystemConfig.h`, add after `bool showOnStartup = true;` (line 30):

```cpp
uint8_t startupMode = 0;   // 0=Vietnamese, 1=English, 2=Remember (Smart Switch persist)
```

- [ ] **Step 2: Add TOML load in ConfigManager**

In `src/core/config/ConfigManager.cpp`, inside `LoadSystemConfig()` after line 558 (`config.autoCheckUpdate = ...`), add:

```cpp
            config.startupMode = static_cast<uint8_t>((*system)["startup_mode"].value_or(0));
```

- [ ] **Step 3: Add TOML save in ConfigManager**

In `src/core/config/ConfigManager.cpp`, inside `SaveSystemConfig()` after line 584 (`system.insert_or_assign("auto_check_update", ...)`), add:

```cpp
        system.insert_or_assign("startup_mode", static_cast<int64_t>(config.startupMode));
```

- [ ] **Step 4: Commit**

```bash
git add src/core/SystemConfig.h src/core/config/ConfigManager.cpp
git commit -m "feat: add startupMode field to SystemConfig + TOML load/save"
```

---

### Task 2: Add SettingMetadata entry + resource ID

**Files:**
- Modify: `src/core/config/SettingMetadata.h:168`
- Modify: `src/app/classic/resource.h:58`

- [ ] **Step 1: Add IDC constant for Classic UI**

In `src/app/classic/resource.h`, add after `#define IDC_COMBO_ICON_STYLE    2408` (line 58):

```cpp
#define IDC_COMBO_STARTUP_MODE  2409
```

- [ ] **Step 2: Add dropdown entry to SettingMetadata**

In `src/core/config/SettingMetadata.h`, add immediately after the `run-startup` entry (after line 168). Insert between `run-startup` and `show-on-startup`:

```cpp
    NK_DROPDOWN("startup-mode",       startupMode,
              "Chế độ mặc định",      "Default mode",
              L"Chế độ gõ khi khởi động ứng dụng",
              L"Typing mode when app starts",                            2409, 2, 0),
```

- [ ] **Step 3: Commit**

```bash
git add src/core/config/SettingMetadata.h src/app/classic/resource.h
git commit -m "feat: add startup-mode to SettingMetadata + IDC constant"
```

---

### Task 3: Wire initial mode through TrayIcon and FloatingIcon

**Files:**
- Modify: `src/app/system/TrayIcon.h:72`
- Modify: `src/app/system/TrayIcon.cpp:42` (Create function signature, line ~42 where `bool TrayIcon::Create` starts)
- Modify: `src/app/system/FloatingIcon.h:24`
- Modify: `src/app/system/FloatingIcon.cpp:142` (Create function)

- [ ] **Step 1: Update TrayIcon::Create signature**

In `src/app/system/TrayIcon.h`, change line 72:

```cpp
// Before:
[[nodiscard]] bool Create(HINSTANCE hInstance);

// After:
[[nodiscard]] bool Create(HINSTANCE hInstance, bool initialVietnamese = true);
```

- [ ] **Step 2: Set vietnameseMode_ before RefreshIcon in TrayIcon::Create**

In `src/app/system/TrayIcon.cpp`, update the `Create` function signature to accept the parameter, and set `vietnameseMode_` before `RefreshIcon()` is called.

Change the function signature (around line 42):

```cpp
// Before:
bool TrayIcon::Create(HINSTANCE hInstance) {

// After:
bool TrayIcon::Create(HINSTANCE hInstance, bool initialVietnamese) {
```

Add this line immediately before `RefreshIcon();` (before line 81):

```cpp
    vietnameseMode_ = initialVietnamese;
```

Also update the tooltip on line 82 to use the correct initial string:

```cpp
// Before:
    StringCchCopyW(nid_.szTip, ARRAYSIZE(nid_.szTip), S(StringId::TIP_VIETNAMESE));

// After:
    StringCchCopyW(nid_.szTip, ARRAYSIZE(nid_.szTip),
                   S(initialVietnamese ? StringId::TIP_VIETNAMESE : StringId::TIP_ENGLISH));
```

- [ ] **Step 3: Update FloatingIcon::Create signature**

In `src/app/system/FloatingIcon.h`, change line 24:

```cpp
// Before:
[[nodiscard]] bool Create(HINSTANCE hInstance);

// After:
[[nodiscard]] bool Create(HINSTANCE hInstance, bool initialVietnamese = true);
```

- [ ] **Step 4: Set vietnameseMode_ in FloatingIcon::Create**

In `src/app/system/FloatingIcon.cpp`, update the function signature (around line 142):

```cpp
// Before:
bool FloatingIcon::Create(HINSTANCE hInstance) {

// After:
bool FloatingIcon::Create(HINSTANCE hInstance, bool initialVietnamese) {
```

Add `vietnameseMode_ = initialVietnamese;` early in the function body, before any code that reads `vietnameseMode_`.

- [ ] **Step 5: Commit**

```bash
git add src/app/system/TrayIcon.h src/app/system/TrayIcon.cpp \
        src/app/system/FloatingIcon.h src/app/system/FloatingIcon.cpp
git commit -m "feat: TrayIcon/FloatingIcon accept initial V/E mode in Create()"
```

---

### Task 4: Wire initial mode through HookEngine + gate Smart Switch persistence

**Files:**
- Modify: `src/app/system/HookEngine.h:41` (Start signature) and `:168` (add startupMode_ member)
- Modify: `src/app/system/HookEngine.cpp:76` (Start), `:96` (load gate), `:181` (save gate)

- [ ] **Step 1: Update HookEngine header**

In `src/app/system/HookEngine.h`, change the Start signature on line 41:

```cpp
// Before:
bool Start(HINSTANCE hInstance, const TypingConfig& config, const HotkeyConfig& hotkey);

// After:
bool Start(HINSTANCE hInstance, const TypingConfig& config, const HotkeyConfig& hotkey,
           bool initialVietnamese = true, uint8_t startupMode = 0);
```

Add a member after `vietnameseMode_` (near line 168):

```cpp
    uint8_t startupMode_ = 0;  // 0=Vietnamese, 1=English, 2=Remember
```

- [ ] **Step 2: Update HookEngine::Start — accept params and set initial mode**

In `src/app/system/HookEngine.cpp`, update the Start signature (line 76):

```cpp
// Before:
bool HookEngine::Start(HINSTANCE hInstance, const TypingConfig& config, const HotkeyConfig& hotkey) {

// After:
bool HookEngine::Start(HINSTANCE hInstance, const TypingConfig& config, const HotkeyConfig& hotkey,
                        bool initialVietnamese, uint8_t startupMode) {
```

After `engine_ = EngineFactory::Create(config);` (line 93), add:

```cpp
    vietnameseMode_ = initialVietnamese;
    startupMode_ = startupMode;
```

- [ ] **Step 3: Gate LoadEnglishModeApps on startupMode == 2**

In `src/app/system/HookEngine.cpp`, modify the Smart Switch load block (line 96):

```cpp
// Before:
    if (smartSwitch_) {
        (void)smartSwitchMgr_.Create();
        auto englishApps = ConfigManager::LoadEnglishModeApps(ConfigManager::GetConfigPath());

// After:
    if (smartSwitch_) {
        (void)smartSwitchMgr_.Create();
        if (startupMode_ == 2) {  // Remember: load persisted per-app modes
        auto englishApps = ConfigManager::LoadEnglishModeApps(ConfigManager::GetConfigPath());
```

And close the inner `if` after line 104 (after `smartSwitchMgr_.LoadFromMap(appModeMap_);` block):

```cpp
        }  // startupMode_ == 2
    }
```

The full block becomes:

```cpp
    if (smartSwitch_) {
        (void)smartSwitchMgr_.Create();
        if (startupMode_ == 2) {  // Remember: load persisted per-app modes
            auto englishApps = ConfigManager::LoadEnglishModeApps(ConfigManager::GetConfigPath());
            for (auto& app : englishApps) {
                appModeMap_[std::move(app)] = false;  // false = English mode
            }
            if (!appModeMap_.empty()) {
                smartSwitchMgr_.LoadFromMap(appModeMap_);
            }
        }
    }
```

- [ ] **Step 4: Gate SaveEnglishModeAppsIfDirty on startupMode == 2**

In `src/app/system/HookEngine.cpp`, modify `Stop()` (line 181):

```cpp
// Before:
    SaveEnglishModeAppsIfDirty();

// After:
    if (startupMode_ == 2) {  // Remember: persist per-app modes
        SaveEnglishModeAppsIfDirty();
    }
```

- [ ] **Step 5: Commit**

```bash
git add src/app/system/HookEngine.h src/app/system/HookEngine.cpp
git commit -m "feat: HookEngine accepts initial mode, gates Smart Switch TOML persistence"
```

---

### Task 5: Wire startup mode through main.cpp

**Files:**
- Modify: `src/app/main.cpp:304-397`

- [ ] **Step 1: Compute startVietnamese from config**

In `src/app/main.cpp`, after loading `systemConfig` (before line 304 `if (g_sharedState.Create())`), add:

```cpp
    bool startVietnamese = (systemConfig.startupMode != 1);
```

- [ ] **Step 2: Override SharedState flags for Always English**

In `src/app/main.cpp`, after `state.SetFeatureFlags(EncodeFeatureFlags(config));` (line 311), add:

```cpp
        if (!startVietnamese) {
            state.flags &= ~SharedFlags::VIETNAMESE_MODE;
        }
```

- [ ] **Step 3: Pass initial mode to TrayIcon::Create**

In `src/app/main.cpp`, change line 318:

```cpp
// Before:
    if (!g_trayIcon.Create(hInstance)) {

// After:
    if (!g_trayIcon.Create(hInstance, startVietnamese)) {
```

- [ ] **Step 4: Pass initial mode to HookEngine::Start**

In `src/app/main.cpp`, change line 397:

```cpp
// Before:
    if (!g_hookEngine.Start(hInstance, config, hotkeyConfig)) {

// After:
    if (!g_hookEngine.Start(hInstance, config, hotkeyConfig, startVietnamese, systemConfig.startupMode)) {
```

- [ ] **Step 5: Pass initial mode to FloatingIcon**

In `src/app/main.cpp`, in the `InitFloatingIcon` function (line 101), update the Create call:

```cpp
// Before:
        if (g_floatingIcon.Create(hInstance)) {

// After:
        if (g_floatingIcon.Create(hInstance, startVietnamese)) {
```

Note: `InitFloatingIcon` is a static function — it needs access to `startVietnamese`. Either:
- Change its signature to `static void InitFloatingIcon(HINSTANCE hInstance, const SystemConfig& sc, bool startVietnamese)` and pass it through, OR
- Compute it locally from `sc.startupMode != 1`

The cleanest approach is to compute locally inside `InitFloatingIcon`:

```cpp
static void InitFloatingIcon(HINSTANCE hInstance, const SystemConfig& sc) {
    bool startVietnamese = (sc.startupMode != 1);
    if (sc.showFloatingIcon) {
        if (g_floatingIcon.Create(hInstance, startVietnamese)) {
```

- [ ] **Step 6: Also update EnsureFloatingIconCreated**

The lazy-create function `EnsureFloatingIconCreated()` (line 91-94) also calls `Create()`. It needs the initial mode too. Since this is called later (when user enables floating icon from settings), it should use current HookEngine mode:

```cpp
static void EnsureFloatingIconCreated() {
    if (g_floatingIcon.IsCreated()) return;
    (void)g_floatingIcon.Create(g_hInstance, g_hookEngine.IsVietnameseMode());
}
```

- [ ] **Step 7: Commit**

```bash
git add src/app/main.cpp
git commit -m "feat: wire startupMode through main.cpp initialization"
```

---

### Task 6: Wire startup mode through main_lite.cpp

**Files:**
- Modify: `src/app/main_lite.cpp:384-473`

- [ ] **Step 1: Compute startVietnamese**

In `src/app/main_lite.cpp`, before the SharedState init block (before line 384), add:

```cpp
    bool startVietnamese = (systemConfig.startupMode != 1);
```

- [ ] **Step 2: Override SharedState flags**

After `state.SetFeatureFlags(EncodeFeatureFlags(config));` (around line 392), add:

```cpp
        if (!startVietnamese) {
            state.flags &= ~SharedFlags::VIETNAMESE_MODE;
        }
```

- [ ] **Step 3: Pass initial mode to TrayIcon::Create**

Change line 399:

```cpp
// Before:
    if (!g_trayIcon.Create(hInstance)) {

// After:
    if (!g_trayIcon.Create(hInstance, startVietnamese)) {
```

- [ ] **Step 4: Pass initial mode to HookEngine::Start**

Change line 473:

```cpp
// Before:
    if (!g_hookEngine.Start(hInstance, config, hotkeyConfig)) {

// After:
    if (!g_hookEngine.Start(hInstance, config, hotkeyConfig, startVietnamese, systemConfig.startupMode)) {
```

- [ ] **Step 5: Update InitFloatingIcon and EnsureFloatingIconCreated**

Same pattern as main.cpp — compute `startVietnamese` from `sc.startupMode` inside `InitFloatingIcon`, and use `g_hookEngine.IsVietnameseMode()` in `EnsureFloatingIconCreated`.

- [ ] **Step 6: Commit**

```bash
git add src/app/main_lite.cpp
git commit -m "feat: wire startupMode through main_lite.cpp initialization"
```

---

### Task 7: Sciter UI — Add dropdown to settings.html + i18n strings

**Files:**
- Modify: `src/app/ui/settings/settings.html:523`
- Modify: `src/app/ui/shared/strings.js:72`

- [ ] **Step 1: Add dropdown row in settings.html**

In `src/app/ui/settings/settings.html`, after the "run-startup" row closing `</div>` (after line 523), add:

```html
                            <!-- 1b. Chế độ mặc định -->
                            <div class="setting-row" style="padding-left: 16dip">
                                <span class="setting-label" data-i18n="s.startup_mode">Chế độ mặc định</span>
                                <div class="spacer" style="width: *"></div>
                                <select class="setting-dropdown-small" id="startup-mode">
                                    <option value="0" data-i18n="s.startup_mode_v">Tiếng Việt</option>
                                    <option value="1" data-i18n="s.startup_mode_e">Tiếng Anh</option>
                                    <option value="2" data-i18n="s.startup_mode_r">Ghi nhớ</option>
                                </select>
                            </div>
```

Note: Sciter uses `dip` units for DPI-aware spacing. Check existing indented rows for the exact pattern; `16dip` matches the visual indent seen in other sub-settings.

- [ ] **Step 2: Add i18n strings**

In `src/app/ui/shared/strings.js`, in the English strings section (after line 72 `"s.run_startup": "Run on Windows startup",`), add:

```js
        "s.startup_mode": "Default mode",
        "s.startup_mode_v": "Vietnamese",
        "s.startup_mode_e": "English",
        "s.startup_mode_r": "Remember last",
```

Also add the Vietnamese strings in the corresponding Vietnamese section (search for the `vi:` block):

```js
        "s.startup_mode": "Chế độ mặc định",
        "s.startup_mode_v": "Tiếng Việt",
        "s.startup_mode_e": "Tiếng Anh",
        "s.startup_mode_r": "Ghi nhớ",
```

- [ ] **Step 3: Commit**

```bash
git add src/app/ui/settings/settings.html src/app/ui/shared/strings.js
git commit -m "feat: add startup mode dropdown to Sciter settings UI"
```

---

### Task 8: SettingsDialog.cpp — Handle dropdown load + change

**Files:**
- Modify: `src/app/dialogs/SettingsDialog.cpp:511` (dropdown handler) and `:1003` (load)

- [ ] **Step 1: Add startup-mode to dropdown change handler**

In `src/app/dialogs/SettingsDialog.cpp`, modify the dropdown ID check on line 511:

```cpp
// Before:
        if (id == L"input-type" || id == L"bang-ma" || id == L"modern-icon") {

// After:
        if (id == L"input-type" || id == L"bang-ma" || id == L"modern-icon" || id == L"startup-mode") {
```

- [ ] **Step 2: Handle startup-mode in handleDropdownChange**

In `src/app/dialogs/SettingsDialog.cpp`, add a new case in `handleDropdownChange()` after the `modern-icon` case (after line 742):

```cpp
    else if (id == L"startup-mode") {
        systemConfig_.startupMode = static_cast<uint8_t>(value);
        saveSystemSettings();
        return;  // System setting, not typing config
    }
```

- [ ] **Step 3: Load dropdown value in populateSettings**

In `src/app/dialogs/SettingsDialog.cpp`, after the icon style dropdown load (after line 1003 `setDropdownValue(L"modern-icon", ...)`), add:

```cpp
    // Startup mode dropdown
    setDropdownValue(L"startup-mode", static_cast<int>(systemConfig_.startupMode));
```

- [ ] **Step 4: Commit**

```bash
git add src/app/dialogs/SettingsDialog.cpp
git commit -m "feat: handle startup-mode dropdown in Sciter SettingsDialog"
```

---

### Task 9: ClassicSettingsDialog — Handle dropdown for Classic UI

**Files:**
- Modify: `src/app/classic/ClassicSettingsDialog.cpp:442` (populate combo items) and `:771` (change handler)

- [ ] **Step 1: Add combo items for startup-mode**

In `src/app/classic/ClassicSettingsDialog.cpp`, in the dropdown creation block (after the `custom-icon-style` block, after line 447), add:

```cpp
            if (wcscmp(meta.id, L"startup-mode") == 0) {
                ComboBox_AddString(combo, L"Tiếng Việt");
                ComboBox_AddString(combo, L"Tiếng Anh");
                ComboBox_AddString(combo, L"Ghi nhớ");
            }
```

- [ ] **Step 2: Handle startup-mode CBN_SELCHANGE**

In `src/app/classic/ClassicSettingsDialog.cpp`, in the generic `CBN_SELCHANGE` handler (around line 771), after the `custom-icon-style` special case block, add a similar block for `startup-mode`:

```cpp
                if (meta->type == SettingType::Dropdown && wcscmp(meta->id, L"startup-mode") == 0
                    && code == CBN_SELCHANGE) {
                    // Startup mode change — immediate save
                    SaveSettings();
                    KillTimer(hwnd_, kTimerDeferredSave);
                    SaveToToml();
                }
```

Note: The generic `ReadControlValues()` + `PopulateControls()` already handle `SettingType::Dropdown` with `uint8_t` at offset via SettingMetadata — so load/save should work automatically for the new dropdown. The special case is only for the immediate TOML flush.

- [ ] **Step 3: Verify generic Dropdown handling covers startup-mode**

Confirm that `PopulateControls()` (line 538-548) and `ReadControlValues()` (line 603-616) already handle `SettingType::Dropdown` + `SettingOwner::System` generically via `meta.offset`. Since `startupMode` is a `uint8_t` in `SystemConfig`, the existing code at those lines reads/writes it automatically. No additional code needed.

- [ ] **Step 4: Commit**

```bash
git add src/app/classic/ClassicSettingsDialog.cpp
git commit -m "feat: handle startup-mode dropdown in Classic settings dialog"
```

---

### Task 10: Build verification

**Files:** None (verification only)

- [ ] **Step 1: Run Linux test build**

```bash
cd /home/phatmt/code/NexusKey
cmake -B build-linux -G "Unix Makefiles" -DCMAKE_BUILD_TYPE=Debug
cmake --build build-linux --target NextKeyTests
./build-linux/tests/NextKeyTests
```

Expected: All existing tests pass. No compilation errors.

- [ ] **Step 2: Verify no regressions**

```bash
./build-linux/tests/NextKeyTests --gtest_filter="*"
```

Expected: Same test count as before, all pass.

- [ ] **Step 3: Commit any fixes if needed**

If build issues arise, fix and commit with appropriate message.
