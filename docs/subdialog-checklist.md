# SubDialog Implementation Checklist

Reference implementation: `ExcludedAppsDialog` / `MacroTableDialog`

## HTML (`src/app/ui/{name}/{name}.html`)

- [ ] Load theme via `<link>` tag, **NOT** CSS `@import url()` (unreliable in Sciter)
  ```html
  <link rel="stylesheet" href="../shared/theme.css">
  <link rel="stylesheet" href="{name}.css">
  ```
- [ ] Load `utils.js` **before** dialog JS
  ```html
  <script src="../shared/utils.js"></script>
  <script src="{name}.js"></script>
  ```
- [ ] **No** `type="module"` on `<script>` tags (Sciter has its own module system)
- [ ] Root container has `class="container"` and `id="main-container"` (required by `setBackgroundOpacity`)
- [ ] Hidden inputs for C++ communication: `#val-action` + any data inputs
- [ ] Close button with `id="btn-close"`

## CSS (`src/app/ui/{name}/{name}.css`)

- [ ] **No** `@import url()` — all external CSS loaded via HTML `<link>`
- [ ] `html, body { background: transparent; }` — required for blur/glass effect
- [ ] `.container { background: var(--bg-glass); }` — uses theme variable
- [ ] Cards use theme variables (`--bg-card`, `--bg-card-solid`, `--border-card`)
- [ ] All colors use `var(--...)` theme tokens — never hardcode colors
- [ ] `input[type="hidden"] { display: none; }`

## JavaScript (`src/app/ui/{name}/{name}.js`)

- [ ] Call `initSubDialog()` in `document.ready` (from `utils.js`):
  ```javascript
  document.ready = function () {
      initSubDialog(".scrollable-list-selector");  // blur + dark + scrollbar
      initMyDialog();
  };
  ```
  This does 3 things: `blurBehind`, `body.dark` class, scrollbar resize.
- [ ] **Do NOT** define local `setBackgroundOpacity()` — provided by `utils.js`
- [ ] Use `triggerAction(action)` pattern to communicate with C++
- [ ] Provide functions for C++ to call: `clearList()`, `addItemToList()`, etc.

## C++ Header (`src/app/{Name}Dialog.h`)

- [ ] Inherit `SciterSubDialog`
- [ ] Override `handle_event()` (not `on_event()`)
- [ ] Override `onBeforeClose()` for save/cleanup
- [ ] Include `"SciterSubDialog.h"`

## C++ Implementation (`src/app/{Name}Dialog.cpp`)

- [ ] Constructor calls `SciterSubDialog({...})` with `SubDialogConfig`:
  ```cpp
  : SciterSubDialog({
      L"this://app/{name}/{name}.html",  // this://app/ URL scheme
      L"NexusKey - Dialog Title",         // window title (used by FindWindowW)
      width, height, parent,              // dimensions + parent HWND
      true,                               // topmost
      36,                                 // title bar height (drag zone)
      40,                                 // buttons width (exclude from drag)
      true                                // applyBackgroundOpacity
  })
  ```
- [ ] Load data from `ConfigManager` in constructor
- [ ] Call `populateList()` after loading data
- [ ] `handle_event()`: handle `BUTTON_CLICK` (btn-close) + `VALUE_CHANGED` (#val-action)
- [ ] Clear action value after handling: `el.set_value(sciter::value(L""))`
- [ ] Call base: `return sciter::window::handle_event(he, params)`
- [ ] `onBeforeClose()`: save via ConfigManager + `ConfigEvent::Signal()`

## Integration

### SharedConstants.h
- [ ] Add `WM_NEXUSKEY_OPEN_{NAME} = WM_USER + N`

### SettingsDialog.cpp
- [ ] `handleButtonClick()`: `PostMessage(get_hwnd(), WM_NEXUSKEY_OPEN_{NAME}, 0, 0)`
- [ ] `SubclassProc`: handle the message → `SpawnSubprocess(title, "--flag")`
- [ ] `handleToggleChange()`: wire any related toggle IDs
- [ ] `initializeUI()`: set toggle states for related settings

### main.cpp
- [ ] `#include "{Name}Dialog.h"`
- [ ] Forward declare `[[noreturn]] void Run{Name}Subprocess()`
- [ ] Add `--flag` command-line check before main process section
- [ ] Implement `Run{Name}Subprocess()`:
  ```cpp
  [[noreturn]] void Run{Name}Subprocess() {
      InitSciterSubprocess();
      HWND parent = FindWindowW(nullptr, L"NexusKey Settings");
      {Name}Dialog dialog(parent);
      dialog.Show();
      ExitProcess(0);
  }
  ```

### CMakeLists.txt
- [ ] Add `.h` and `.cpp` to `NextKeyApp` sources
- [ ] Add UI files (`.html`, `.css`, `.js`) to packfolder `DEPENDS`

## Common Pitfalls

1. **CSS `@import`** → Sciter path resolution is unreliable. Always use HTML `<link>`.
2. **Missing `initSubDialog()`** → No blur, no dark theme, light mode fallback.
3. **Local `setBackgroundOpacity`** → Overwrites the dark-aware version from `utils.js`.
4. **`on_event()` instead of `handle_event()`** → `sciter::window` base class skips `on_event()`.
5. **`type="module"` on scripts** → Sciter module system differs from browser ES modules.
6. **`call_function()` in constructor** → JS may not be loaded yet. OK after `populateList()` since HTML is loaded by then.
