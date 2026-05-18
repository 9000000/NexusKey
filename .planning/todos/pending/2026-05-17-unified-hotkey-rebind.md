---
created: 2026-05-17T14:40:10.000Z
title: Unified hotkey rebind — TOML array for 3 intents with capture-mode UI
area: hotkey-system
files:
  - src/core/config/TypingConfig.h:34-38 (TempOffMethod enum — replace/extend)
  - src/core/config/TypingConfig.h:74,80,81 (tempOffMethod, tempOffMacroByEsc, escRestoreRawEnabled fields — keep behavior, change wiring)
  - src/core/config/ConfigManager.cpp (load/save [[hotkey]] section)
  - src/core/config/ConfigManager.h
  - src/app/system/HookEngine.cpp:843-844,1220,1278,1285,1588-1616 (3 hotkey check sites)
  - src/app/system/HookEngine.h (atomic members)
  - src/core/hotkey/HotkeyConfig.h (NEW)
  - src/core/hotkey/HotkeyConfig.cpp (NEW)
  - src/app/dialogs/SettingsDialog.cpp (Sciter — new "Phím tắt" tab section)
  - src/app/classic/SettingsDialog.cpp (Classic mirror)
  - tests/HotkeyConfigTest.cpp (NEW)
  - tests/HookEngineHotkeyTest.cpp (NEW)
brainstorm_spec: _bmad-output/brainstorming/brainstorming-session-2026-05-17-1359.md
---

## Problem

3 hotkey hiện tại bị fragment thành 3 cơ chế độc lập trong `TypingConfig`:

| Hotkey | Field | Current behavior |
|--------|-------|------------------|
| Esc → cancel composition | `escRestoreRawEnabled` (bool) | On/off only — key hardcoded `VK_ESCAPE` |
| Esc → skip macro | `tempOffMacroByEsc` (bool) | On/off only — key hardcoded `VK_ESCAPE` |
| Ctrl alone / 2×Alt → toggle enabled | `tempOffMethod` (enum: None/DupAlt/Ctrl) | 3-value enum — user chỉ chọn Ctrl OR DupAlt, không rebind |

**User pain**: không đổi được phím cho bất kỳ chức năng nào. Yêu cầu 2026-05-17:
- Gôm 3 hotkey về 1 chỗ config
- Cho phép rebind tự do (single key / chord / double-tap)
- 1 chức năng có thể có nhiều trigger (Ctrl + 2×Alt cùng toggle-enabled)

## Solution (Approach B+ — locked from brainstorm)

### Config schema (`config.toml`, replaces current 3 fields)

```toml
[[hotkey]]
intent  = "cancel-composition"
trigger = { vk = 27, mods = 0 }              # Esc

[[hotkey]]
intent  = "skip-macro"
trigger = { vk = 27, mods = 0 }              # Esc (independent default)

[[hotkey]]
intent  = "toggle-enabled"
trigger = { vk = 17, mods = 0 }              # Ctrl alone (implicit modifier-alone)

[[hotkey]]
intent  = "toggle-enabled"
trigger = { vk = 18, mods = 0, double_tap = true }   # 2×Alt
```

Missing `[[hotkey]]` section → fallback to factory defaults above.
Unknown `intent` string → log warning, skip row.

### Data model

```cpp
// src/core/hotkey/HotkeyConfig.h
namespace NextKey {

enum class Intent : uint8_t {
    CancelComposition,
    SkipMacro,
    ToggleEnabled,
};

struct Trigger {
    uint32_t vk;          // VK code
    uint32_t mods;        // bitmask MOD_CTRL|MOD_SHIFT|MOD_ALT|MOD_WIN
    bool     doubleTap;   // require 2 taps in 350ms window
};

class HotkeyConfig {
    std::unordered_map<Intent, std::vector<Trigger>> triggers_;
public:
    [[nodiscard]] bool Matches(Intent intent, uint32_t vk, uint32_t mods,
                               bool isDoubleTap, bool keyUp) const noexcept;
    void Load(const toml::array& cfg);
    void Save(toml::array& cfg) const;
    static HotkeyConfig Defaults();   // Esc / Esc / Ctrl / 2×Alt
};

} // namespace NextKey
```

`Matches()` implements **implicit modifier-alone**: if a `Trigger` has `vk ∈ {VK_CONTROL, VK_SHIFT, VK_MENU, VK_LWIN, VK_RWIN}` and `mods == 0`, fire only on UP with the rule "no other key was pressed since modifier DOWN within 250ms window."

### Implementation checklist

- [ ] **Step 1** — Add `src/core/hotkey/HotkeyConfig.{h,cpp}` with the types above. Plug into CMake (likely `src/core/CMakeLists.txt` or core lib target — verify before edit).
- [ ] **Step 2** — `ConfigManager.cpp`: add `LoadHotkeySection(toml::table&)` + `SaveHotkeySection(toml::table&)`. On Load: if `[[hotkey]]` array missing, call `HotkeyConfig::Defaults()`. Store as `HotkeyConfig hotkeys_` member of ConfigManager (or pass through to engine via existing config-push path).
- [ ] **Step 3** — `HookEngine.h/cpp`: replace existing atomics `escRestoreRawEnabled_`, `tempOffMacroByEsc_`, `tempOffMethod_` with a single `std::shared_ptr<const HotkeyConfig> hotkeys_` (RCU swap on reload). Update `OnConfigUpdated()` to swap atomically.
- [ ] **Step 4** — `HookEngine.cpp:1278` (escRestoreRaw + VK_ESCAPE check) → replace with `hotkeys_->Matches(Intent::CancelComposition, vkCode, currentMods, isDt, keyUp)`. Preserve surrounding `hasComposition_` gate logic.
- [ ] **Step 5** — `HookEngine.cpp:1220,1285` (tempOffMacroEsc + VK_ESCAPE) → replace with `hotkeys_->Matches(Intent::SkipMacro, ...)`. Preserve `macroOn && !macroTable_.empty()` gates.
- [ ] **Step 6** — `HookEngine.cpp:1588-1616` (TempOffMethod::Ctrl / TempOffMethod::DupAlt branches) → replace both with single `hotkeys_->Matches(Intent::ToggleEnabled, ...)` call. Move "double-tap timing tracker" and "ctrl-alone window tracker" state into HotkeyConfig::Matches (or pass tracker state through). KEEP existing tracker logic — just relocate.
- [ ] **Step 7** — Backwards-compat migration: when ConfigManager loads, if `[[hotkey]]` array missing AND old fields (`escRestoreRawEnabled`, `tempOffMacroByEsc`, `tempOffMethod`) are present → translate to `[[hotkey]]` entries based on user's prior on/off settings + `tempOffMethod` selection. Persist on next save. Log "Migrated v2 hotkey config".
- [ ] **Step 8** — Settings UI (Sciter `SettingsDialog`): new section "Phím tắt" with 3 rows:
  - Row 1: "Hủy từ đang gõ" — 1 trigger slot, `[Đổi]` button → capture mode
  - Row 2: "Bỏ qua gõ tắt" — 1 trigger slot, `[Đổi]` button
  - Row 3: "Tạm tắt / bật bộ gõ" — N trigger chips, `[Đổi]` per chip, `[+ Thêm phím khác]` button
  - Bottom: `[↻ Khôi phục mặc định]` button — calls `HotkeyConfig::Defaults()`
  Capture mode: hidden input grabs next KeyDown; records `(vk, mods, doubleTap)`. Double-tap detected by 2 same-vk DOWN events within 350ms. Modifier-alone detected by modifier DOWN→UP within 250ms with no other key.
- [ ] **Step 9** — Classic Win32 `SettingsDialog` mirror: same 3 rows via ListView or custom controls. Capture dialog reuses Win32 RawInput.
- [ ] **Step 10** — Remove dead fields from `TypingConfig`: `escRestoreRawEnabled`, `tempOffMacroByEsc`, `tempOffMethod`, `TempOffMethod` enum (or mark deprecated for 1 release if SharedState ABI concern — check `docs/CODING_RULES/5-struct-versioning.md`).

### Test surface

- [ ] `HotkeyConfigTest` (new file):
  - `Defaults_ProducesExpected4Bindings` — verify 4 default entries match spec
  - `Load_MissingSection_UsesDefaults`
  - `Load_UnknownIntent_SkipsAndWarns`
  - `Load_Save_RoundTrip`
  - `Matches_SingleKey_ExactVkMods`
  - `Matches_DoubleTap_FiresOnSecondTapWithinWindow`
  - `Matches_DoubleTap_FailsOutsideWindow`
  - `Matches_Chord_RequiresAllMods`
  - `Matches_ModifierAlone_FiresOnUpWhenNoOtherKey`
  - `Matches_ModifierAlone_DoesNotFireWhenOtherKeyPressed` (Ctrl+C scenario)
- [ ] `HookEngineHotkeyTest` (new file):
  - `RebindEscToF1_F1CancelsComposition`
  - `RebindToggleToShiftSpace_ShiftSpaceTogglesEnabled`
  - `DefaultCtrl_CtrlAloneTogglesButCtrlCPasses`
  - `MigrationFromV2_GeneratesCorrectBindings` (with `tempOffMethod = Ctrl` + `escRestoreRawEnabled = true` + `tempOffMacroByEsc = true`)
- [ ] Re-run existing `HookEngineTest` suite to ensure no regression in Vietnamese typing path.

### Risk gates before merge

- [ ] Chrome/Electron: gate eval (`hasComposition_`) race-safe with new dispatcher placement
- [ ] LoL test (per `feedback_game_bug_baseline`): Esc passthrough correct when no composition
- [ ] 2×Alt double-tap does NOT fire on legit Alt+Tab → Alt+Tab → Alt+Tab window switching
- [ ] Ctrl alone does NOT fire when user does Ctrl+C / Ctrl+V / Ctrl+Z fast
- [ ] Chaos.toml v2.1 baseline: 11/11 still passes after refactor
- [ ] Migration test: take 5 real `config.toml` files (v2 schema) → assert migrated v3 produces identical behavior

### Sequencing

Per `MEMORY.md` rule (anh 2026-05-07): HookEngine refactor backlog before TypingEngine. Recommend scheduling **after H5 Macro extract** because `Intent::SkipMacro` semantically belongs to the Macro module — cleaner if Macro is already extracted.

If H5 not ready, this todo is **still self-contained** — `tempOffMacroByEsc` flag stays where it is during this refactor, just gets driven by new HotkeyConfig instead of TypingConfig field.

### Estimate

~2-2.5 ngày (per brainstorm):
- HotkeyConfig.{h,cpp} + 10 unit tests: 0.5d
- HookEngine wire 3 sites + integration tests: 0.5d
- Sciter UI + capture mode: 0.5-1d
- Classic UI mirror: 0.5d

## Decision needed

Anh decide trước khi implement:
1. **Migration aggressiveness**: hard-delete old fields (Step 10 as written) vs deprecate-for-1-release? Hard-delete is cleaner — config v2 reads automatic-migrate in ConfigManager, downgrade scenario documented as "loses custom rebind" per brainstorm.
2. **UI capture mode key**: keep `[Đổi]` button per row (clean) vs single global capture mode + drag-drop assign? Brainstorm chốt `[Đổi]` per row — confirm.

## Pointer

Full spec, alternatives considered (Approach A vs B vs B+), 16 risk catalog, and rationale: see `_bmad-output/brainstorming/brainstorming-session-2026-05-17-1359.md`.
