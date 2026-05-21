# Convert-Tool Hotkey: Unify Capture UI + F-Row Support

**Date:** 2026-05-20
**Status:** Design approved, ready to implement
**Scope:** Unify the convert-tool hotkey UI (Sciter + Classic) with the existing HotkeysDialog capture overlay; extend `HotkeyConfig` to support F1–F24 and other non-printable keys.

## Problem

`HotkeyConfig` (`src/core/config/TypingConfig.h:34`) stores the binding as `wchar_t key` — only printable characters. Convert-tool dialog (both Sciter and Classic) lets the user tick 4 modifier toggles and type a single character into an edit box. There is no way to bind F1–F12 or other non-character keys.

Meanwhile, the newer `HotkeysDialog` (V/E toggle, smart switch, etc.) already has a polished capture overlay (`src/app/ui/hotkeys/hotkeys.js`) that records any key — F-row, Numpad, OEM, modifier combinations — via document-level `^keydown` and produces a VK code with modifier mask.

So we have two hotkey input modalities in the same app: one fully featured, one limited. User asked us to allow F-row in convert-tool. The right answer is to share the capture UI, not patch a second modality.

**Architectural caveat:** there are also two *runtime* hotkey systems — `HotkeyManager` (legacy, `app/system/`, holds convert + V/E slots) and `HotkeyRegistry` (newer, `core/hotkey/`, holds the 3 Intents driven by HotkeysDialog). They are not unified in this design — only the UI is. Full runtime migration is a future cleanup.

## Decisions (chốt từ brainstorming session)

| Decision | Choice | Why |
|---|---|---|
| Unify or patch? | **Unify** capture UI with HotkeysDialog | "Mở rộng không phân mảnh" — one capture component, one UX. |
| Scope of unification | **UI only** (runtime untouched) | Lower risk; HotkeyRegistry migration deferred. |
| Data model | **Replace** `wchar_t key` with `uint32_t vk` | Matches `HotkeyRegistry::Trigger::vk`. One field, no parallel representation. |
| Legacy migration | **Clean rule** — A-Z/a-z/0-9 → towupper → VK; anything else → vk=0 (user reassigns) | No OEM table guesswork; solo-dev does not need downgrade-safe TOML. |
| Drop 4 modifier toggles in convert-tool? | **Yes** | Capture overlay records modifiers itself; toggle + key model has had bugs (Alt menu accelerator, Ctrl+key OS shortcuts). One mental model. |
| Classic UI capture | **Modal popup** subclassing keydown | Better isolation than in-place edit subclassing; no `IsDialogMessage` fight. |
| `HotkeyLabel` location | `core/hotkey/` | Linux-portable formatter; lives next to `HotkeyRegistry` so the eventual full unification has nothing to move. |

## Architecture

### Data model

```cpp
// src/core/config/TypingConfig.h
struct HotkeyConfig {
    bool ctrl  = false;
    bool shift = false;
    bool alt   = false;
    bool win   = false;
    uint32_t vk = 0;   // VK_* code. 0 = unassigned.

    [[nodiscard]] bool HasAny() const noexcept { return ctrl||shift||alt||win||vk!=0; }
    [[nodiscard]] bool ModifiersMatch(bool c, bool s, bool a, bool w) const noexcept;
    bool operator==(const HotkeyConfig&) const noexcept = default;
};
```

### Shared label formatter

```cpp
// src/core/hotkey/HotkeyLabel.h
namespace NextKey {
    // "Ctrl+Shift+F5", "Alt+Z", "Ctrl" (mods only), "" (empty config).
    [[nodiscard]] std::wstring FormatHotkeyLabel(uint32_t vk, uint32_t mods);

    // Modifier bitmask helper for HotkeyConfig → mods integer (kModCtrl etc.).
    [[nodiscard]] uint32_t HotkeyConfigToMods(const HotkeyConfig& cfg) noexcept;
}
```

Callers:
- **TrayIcon** binding-text rebuilder (currently formats convert-hotkey by hand).
- **`ConvertToolDialog::setHotkeyDisplay()`** (Sciter side, C++ → push label into `#hotkey-display`).
- **`ClassicConvertToolDialog`** record-button label.
- **Sciter `hotkey-capture.js`** has its own JS `vkToLabel` for hot-path preview — *not* shared with C++. The two implementations must stay in sync via the test suite (the migration test pins VK values; the label test pins label strings).

### Sciter shared capture component

```js
// src/app/ui/shared/hotkey-capture.js
// Loaded by any dialog needing keystroke capture.
window.NextKeyHotkeyCapture = {
    create({ onCommit, onCancel }) {
        // - Injects capture overlay DOM once.
        // - Subscribes document-level ^keydown when open() is called,
        //   unsubscribes on close()/commit.
        // - Decodes event.code → VK (DOM Level 3 names: "KeyA", "F12", "Numpad7").
        // - Owns vkToLabel + decode tables.
        return { open(preVk, preMods), close(), isOpen() };
    }
};
```

`hotkeys.js` (existing HotkeysDialog) refactored to call this — behavior preserved. `convert-tool.js` (new wiring) also calls it.

### Sciter convert-tool UI

Drop 4 modifier toggles + 1 char edit box. Replace with:

```html
<div class="hotkey-section">
    <span class="hotkey-label">Phím chuyển mã nhanh:</span>
    <button class="btn-capture-hotkey" id="btn-record-hotkey">
        <span id="hotkey-display">— Chưa đặt —</span>
        <span class="hint">Bấm để ghi</span>
    </button>
</div>
<input type="hidden" id="val-hotkey-vk" value="0">
<input type="hidden" id="val-hotkey-mods" value="0">
```

### Classic UI capture

```cpp
// src/app/classic/ClassicHotkeyCapture.h
namespace NextKey {
    struct CapturedHotkey { uint32_t vk; uint32_t mods; };
    // Returns nullopt if user cancels.
    std::optional<CapturedHotkey> ShowHotkeyCaptureDialog(
        HWND parent, const ClassicTheme& theme,
        uint32_t initialVk, uint32_t initialMods);
}
```

Modal dialog (in-memory template). Subclassed window-proc intercepts `WM_KEYDOWN`/`WM_SYSKEYDOWN` directly. Reads VK from `wParam` + `GetKeyState()` for modifier flags. Save/Cancel buttons. Bare modifier → Save disabled (convert-tool runtime does not support modifier-alone).

`ClassicConvertToolDialog`:
- Remove `checkHkCtrl_/Alt_/Shift_/Win_` and `editHkKey_`.
- Add `btnRecordHotkey_` (button labeled via `FormatHotkeyLabel`).
- `WM_COMMAND` on button click → `ShowHotkeyCaptureDialog` → if Some, write to `config_.hotkey`, rebuild button text.
- Layout grid reflows (existing `DeferWindowPos` sequence — remove 5 controls, add 1).

`theme_.DrawHotkeyEditBorder` (line 627) stays — Classic V/E toggle settings may still use it. Verify at implement time; do not delete preemptively.

### Runtime

```cpp
// src/app/system/HotkeyManager.cpp:143  (matchCombo lambda)
auto matchCombo = [&](const HotkeyConfig& cfg) noexcept {
    if (cfg.vk == 0) return false;
    if (static_cast<uint32_t>(wParam) != cfg.vk) return false;
    return cfg.ModifiersMatch(ctrlDown, shiftDown, altDown, winDown);
};
```

The legacy `(WCHAR)wParam` cast disappears. Hot-path cost unchanged.

### Migration

```cpp
// src/core/config/ConfigManager.cpp — anonymous namespace
namespace {
uint32_t LegacyKeyToVk(const std::wstring& s) noexcept {
    if (s.size() != 1) return 0;
    wchar_t c = static_cast<wchar_t>(towupper(s[0]));
    // A-Z and 0-9 happen to share code points with VK_A..VK_Z (0x41..0x5A)
    // and VK_0..VK_9 (0x30..0x39). Everything else → 0 (user must reassign).
    if ((c >= L'A' && c <= L'Z') || (c >= L'0' && c <= L'9'))
        return static_cast<uint32_t>(c);
    return 0;
}
}

// Inside LoadHotkeyConfig:
if (auto vkNode = tbl["vk"]; vkNode.is_integer()) {
    config.vk = static_cast<uint32_t>(vkNode.value_or<int64_t>(0));
} else if (auto keyNode = tbl["key"]; keyNode.is_string()) {
    config.vk = LegacyKeyToVk(keyNode.value_or(std::wstring{}));
}

// Save always writes only `vk`. The next save overwrites the legacy `key`.
```

## Tests (Linux portable, gtest)

```
tests/core/HotkeyConfigMigrationTest.cpp
    - LegacyKeyToVk maps A-Z, a-z (case-fold), 0-9.
    - Empty string → 0.
    - "~", multi-char, OEM-style → 0 (user reassigns).
    - ModifiersMatch unchanged after rename.

tests/core/HotkeyLabelTest.cpp
    - "Ctrl+Shift+F1" for vk=0x70, mods=Ctrl|Shift.
    - "Alt+Z" for vk=0x5A, mods=Alt.
    - "Ctrl" for vk=0, mods=Ctrl.
    - "" for vk=0, mods=0.
```

Manual (Windows, anh verify):
1. Bản cũ có `key = "Z"` → load → display "Alt+Z".
2. Record Ctrl+Shift+F5 → Save → reload app → trigger in another app → quick-convert fires.
3. Config cũ có `key = "~"` → load → "— Chưa đặt —" (vk=0).
4. Lite mode (`-DVKEY_LITE_MODE=ON`) → repeat (2) with modal popup.

## File changes

**New (5):**
- `src/core/hotkey/HotkeyLabel.h`
- `src/core/hotkey/HotkeyLabel.cpp`
- `src/app/ui/shared/hotkey-capture.js`
- `src/app/classic/ClassicHotkeyCapture.h`
- `src/app/classic/ClassicHotkeyCapture.cpp`
- `tests/core/HotkeyConfigMigrationTest.cpp`
- `tests/core/HotkeyLabelTest.cpp`

**Modified (9):**
- `src/core/config/TypingConfig.h` — `wchar_t key` → `uint32_t vk`.
- `src/core/config/ConfigManager.cpp` — load (vk-first, legacy key fallback), save (vk only), `LegacyKeyToVk`.
- `src/app/system/HotkeyManager.cpp` — `matchCombo` WCHAR → VK.
- `src/app/system/TrayIcon*` — convert-hotkey label builder calls `FormatHotkeyLabel`.
- `src/app/ui/convert-tool/convert-tool.html` — drop 4 toggle blocks + char input; add record button.
- `src/app/ui/convert-tool/convert-tool.js` — wire capture component.
- `src/app/dialogs/ConvertToolDialog.cpp/h` — simplify; remove `setHotkeyCharUI`, add `setHotkeyDisplay(vk,mods)` + `on_hotkey_change` xcall.
- `src/app/ui/hotkeys/hotkeys.js` — call shared component, drop in-place capture logic.
- `src/app/classic/ClassicConvertToolDialog.cpp/h` — drop checkboxes + edit box; add record button; call modal.

## Risk

- **`hotkeys.js` refactor** — break risk for already-shipped HotkeysDialog. Mitigation: step 4 ships independently; smoke test HotkeysDialog (record + commit + persistence) before touching convert-tool.
- **`matchCombo` WCHAR→VK cast switch** — any other caller passing WCHAR? Grep `wParam.*WCHAR` repo-wide before merge.
- **TOML legacy `key` orphaned** — after first save, the old field is gone; users on bleeding-edge who roll back will lose convert hotkey. Acceptable (solo-dev, no versioned schema).

## Rollout (1 PR, ordered commits — Linux green at each step)

1. `HotkeyLabel.h/.cpp` + label test. (no callers yet)
2. `HotkeyConfig.key → vk` + migration helper + migration test.
3. `HotkeyManager.matchCombo` + `TrayIcon` label builder.
4. `shared/hotkey-capture.js` + refactor `hotkeys.js` (smoke-test HotkeysDialog).
5. Convert-tool Sciter UI + `ConvertToolDialog`.
6. Classic capture modal + `ClassicConvertToolDialog`.

Each step ships a working binary on its own — Linux tests pass at every commit.
