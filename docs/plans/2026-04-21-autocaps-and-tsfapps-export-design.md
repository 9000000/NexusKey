# Auto-caps `.com` Fix + TSF Apps Export/Import

**Date:** 2026-04-21
**Status:** Approved
**Impact:**
- Bug fix — stop force-capping domains/extensions glued to `.` (`.com` → `.Com`)
- Bug fix — multi-space after `.` preserves auto-cap intent
- Feature — export/import for TSF apps list (parity with other list dialogs)

---

## Part A — Auto-caps after `.` (Bug #2)

### Problem

User report (v2.1.19, Hook-hybrid-TSF mode):

1. Typing `.com`, `.vn`, `.exe` — first letter of extension force-capped to `C`, `V`, `E`.
2. Typing `. ` + letter works. But `.  ` + letter (multi-space) does NOT cap.
3. Backspace deleting past a period into new sentence loses auto-cap state.

Expected:
- No whitespace between `.` and next char → stay lowercase (domains/extensions).
- At least one whitespace between `.` and next char → cap, regardless of space count.
- Durability through edits (if document truth says "after `.` + space", cap).

### Root causes

**Cause A — Anchor (TSF-hybrid path):** `src/core/ipc/SharedState.h:93-117` `DeriveAnchorFromPreceding` walks back skipping spaces/tabs, then checks if the preceding non-space char is `.?!`. For `"chrome."` + caret glued (no trailing space), the scan sets `isSentenceStart=1` because it finds `.` immediately. The "skipped whitespace" precondition is missing.

**Cause B — Hook state machine:** `src/app/system/HookEngine.cpp:834-844` only promotes `AfterPunct → ReadyToCapitalize` on the **first** SPACE. A second SPACE falls into the generic `else` branch and resets state to `Idle`.

### Fix

**A1. Anchor** — `DeriveAnchorFromPreceding`:

```cpp
size_t i = len;
bool skippedWhitespace = false;
while (i > 0) {
    uint16_t c = preceding[i - 1];
    if (c == u' ' || c == u'\t') { --i; skippedWhitespace = true; continue; }
    break;
}

if (i == 0) {
    // all-whitespace buffer → treat as fresh start
    out.isSentenceStart = 1;
    out.isLineStart     = 1;
} else {
    uint16_t prev = preceding[i - 1];
    if (prev == u'\n' || prev == u'\r') {
        out.isSentenceStart = 0;
        out.isLineStart     = 1;
    } else if ((prev == u'.' || prev == u'?' || prev == u'!') && skippedWhitespace) {
        out.isSentenceStart = 1;  // require whitespace between punct and cursor
        out.isLineStart     = 0;
    } else {
        out.isSentenceStart = 0;
        out.isLineStart     = 0;
    }
}
```

**A2. Hook state machine** — `HookEngine.cpp` ProcessKeyDown auto-caps branch:

```cpp
if (autoCaps_) {
    if (vkCode == VK_OEM_PERIOD || (vkCode == 0xBF && cachedShift) || (vkCode == '1' && cachedShift)) {
        autoCapState_ = AutoCapState::AfterPunct;
    } else if (vkCode == VK_SPACE &&
               (autoCapState_ == AutoCapState::AfterPunct ||
                autoCapState_ == AutoCapState::ReadyToCapitalize)) {
        autoCapState_ = AutoCapState::ReadyToCapitalize;  // multi-space survival
    } else if (vkCode == VK_RETURN) {
        autoCapState_ = AutoCapState::ReadyToCapitalize;
    } else if (vkCode >= 0x41 && vkCode <= 0x5A) {
        // letter key — consumed by HandleAlphaKey
    } else {
        autoCapState_ = AutoCapState::Idle;
    }
}
```

### Scope limit (accepted)

Durability after Backspace in pure-hook apps (no TSF-hybrid) is **not** covered — the state machine has no text oracle. TSF-hybrid apps get durability via anchor (reads live document state). Acceptable because pure-hook apps are shrinking surface.

### Tests

Add to `tests/SharedStateTest.cpp`:

| Input buffer | Expected `isSentenceStart` | Expected `isLineStart` |
|---|---|---|
| `""` (empty) | 1 | 1 |
| `"chrome."` | **0** (new behavior) | 0 |
| `"chrome.com"` | 0 | 0 |
| `"Hi."` | **0** (new behavior) | 0 |
| `"Hi. "` | 1 | 0 |
| `"Hi.   "` (multi-space) | 1 | 0 |
| `"\n"` | 0 | 1 |
| `"Hi.\n"` | 0 | 1 |

### Files touched

| File | Change |
|---|---|
| `src/core/ipc/SharedState.h` | `DeriveAnchorFromPreceding` — add `skippedWhitespace` gate |
| `src/app/system/HookEngine.cpp` (~line 836) | SPACE preserves `ReadyToCapitalize` |
| `tests/SharedStateTest.cpp` | 8 new cases |

---

## Part B — TSF Apps Export/Import (Feature #3)

### Problem

`ExcludedAppsDialog`, `MacroTableDialog`, `SpellExclusionsDialog` already have Export/Import buttons. `TsfAppsDialog` does not. Users who tune their TSF app list across machines have no easy way to sync.

### Scope

Clone the existing pattern from `ExcludedAppsDialog` to `TsfAppsDialog`. Plain-text format (1 app per line, `#` comment), same filename filter, same dialog UX. Full-settings export is **out of scope** — user can copy `settings.toml` directly.

### Files touched

| File | Change |
|---|---|
| `src/app/dialogs/TsfAppsDialog.h` | Declare `importApps()`, `exportApps()` |
| `src/app/dialogs/TsfAppsDialog.cpp` | Implement methods (copy from `ExcludedAppsDialog.cpp`, swap data source to `config.tsfApps`). Route action strings `"import"`/`"export"` |
| `src/app/ui/tsfapps/tsfapps.html` | Add 2 buttons matching `excludedapps.html` layout |
| `src/app/ui/tsfapps/tsfapps.css` | Add styles only if not covered by `subdialog.css` |
| `src/app/ui/shared/strings.js` | Add/reuse i18n keys for import/export labels |

### UX

Same as `ExcludedAppsDialog`: two buttons in the footer, "Export" opens Save dialog (default name `tsf-apps.txt`), "Import" opens Open dialog and appends/merges (matching existing behavior — confirm by reading Excluded impl during implementation).

### Tests

No core-logic changes. Manual smoke test on Windows: export list, edit file, re-import, verify list updated. No Linux unit tests needed.

---

## Implementation order

1. **Part A first** — bug fix, smaller blast radius, Linux-testable.
2. **Part B second** — UI-only, depends on Windows build.

Commit granularity: one commit per part (Part A bundles anchor fix + hook fix + tests since they're tightly coupled to the user-visible behavior).
