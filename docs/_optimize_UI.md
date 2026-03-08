---
description: Rollback documentation for Sciter UI optimizations
date: 2026-03-08
version: v1.0.0 (Pre-Release / UI Optimization Phase)
---

# UI Optimization Log

Records all changes made to optimize the Sciter UI rendering. Use this to trace issues and rollback if needed.

## 1. Sciter Backend Switch (Skia -> Direct2D)
- **File:** `tools/update_sciter.sh`
- **Change:** `bin/windows/x64/sciter.dll` → `bin/windows.d2d/x64/sciter.dll`

## 2. Window Instantiation Fixes
- **Files:** `SettingsDialog.cpp`, `SciterSubDialog.cpp`
- **Change:** Reverted `SW_ALPHA` → `SW_POPUP` in `sciter::window` constructor.
- **Files:** All UI `.html` files
- **Change:** Added `window-frame="transparent"` to `<html lang="vi">` tag.

## 3. Flexbox to Flow Conversion
- **File:** `settings.css`
- **Change:** Replaced `display: flex` with `flow: horizontal/vertical` in: `.setting-row`, `.switch-key-container`, `.switch-key-row`, `.switch-key-item`, `.color-picker-group`, `.slider-container`, `#check-update`.
- Replaced `flex: 1` with `width: *` or `height: *`.
- **File:** `toggle.css` — Removed unused `flex-shrink: 0`.

> [!IMPORTANT]
> **Kept flexbox on `.tab-header`** (`display: flex; justify-content: center;`).
> Sciter's `flow:` layout does not support `margin: 0 auto` or `text-align: center` for centering child elements — flexbox is the only reliable method for this element.

## 4. Text Anti-Aliasing Removal
- **File:** `theme.css`
- **Change:** Removed `-webkit-font-smoothing: antialiased;` and `text-rendering: optimizeLegibility;` from `body` to prevent blurry text on glass backgrounds.

## 5. Border to Inset Box-Shadow Conversion
- **Files:** `theme.css`, `settings.css`, `subdialog.css`, `dropdown.css`, `convert-tool.css`, `about.css`, `appoverrides.css`
- **Change:** Replaced 23 instances of `border: 1px solid <color>` with `box-shadow: inset 0 0 0 1px <color>` for smoother rendering on rounded corners.

## 6. C++ Initial Window Sizing
- **File:** `SettingsDialog.cpp`
- **Change:** Removed the initial `SetWindowPos` auto-fit block (step 5 in constructor). Window now auto-sizes via CSS + Sciter transparent mode at creation.
- `recalcWindowSize()` is **kept** — required for expand/collapse because Sciter transparent windows only auto-size at initial render, not on dynamic DOM changes.
- **File:** `SciterSubDialog.cpp`
- **Change:** Removed `SetWindowPos` size calculation based on `config_.baseWidth * dpiScale`. Window sizing delegated to CSS.
- **File:** `subdialog.css`
- **Change:** Added `html, body { width: max-content; height: max-content; }` and `.container { width: max-content; height: max-content; }`.

> [!WARNING]
> **`max-content` does NOT work on `settings.css`** because it uses a 2-column `float: left` layout.
> The settings container keeps its original `width: 350px; height: auto;` with C++ `recalcWindowSize()` managing expand/collapse.

## Lessons Learned

| Approach | Result | Notes |
|---|---|---|
| `flow: horizontal` + `horizontal-align: center` | ❌ Not supported | Sciter flow doesn't support this |
| `flow: horizontal` + `margin: 0 auto` | ❌ Conflicts | `flow:` overrides block-level margin |
| `text-align: center` + `inline-block` + `font-size: 0` | ❌ Gap artifact | Creates vertical spacing gaps |
| `display: flex` + `justify-content: center` | ✅ Works | Only reliable centering in Sciter |
| `width: max-content` on body/container | ⚠️ Partial | Works for single-column. Breaks float layouts |

## Rollback
Revert to the commit directly before this optimization session. All changes are CSS/C++ only — no data or config format changes.
