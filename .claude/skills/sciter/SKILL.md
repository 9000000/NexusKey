---
name: sciter
description: |
  Sciter.JS development patterns and quirks for building desktop applications with HTML/CSS/JS UI.
  Use when working with Sciter SDK, creating transparent windows, blur effects, resource loading,
  or debugging Sciter-specific CSS/JS issues. Covers: initialization order (CRITICAL), window blur,
  resource loading (on_load_data with this://app/ URLs), script tags (no type="module"),
  CSS quirks (@import issues), window sizing, and DLL deployment.
---

# Sciter.JS Development Patterns

## Initialization Order (CRITICAL)

Sciter requires specific initialization order. Failure causes silent crashes.

### Required Setup

```cpp
#include "sciter-x.h"
#include "sciter-x-window.hpp"
#include <ole2.h>
#pragma comment(lib, "ole32.lib")

void InitSciter() {
    // 1. OLE initialization (required for drag-n-drop)
    OleInitialize(nullptr);

    // 2. Initialize Sciter application (MUST be first Sciter call)
    sciter::application::start();

    // 3. GLOBAL options (NULL window handle) - before window creation
    SciterSetOption(nullptr, SCITER_SET_SCRIPT_RUNTIME_FEATURES,
        ALLOW_FILE_IO | ALLOW_SOCKET_IO | ALLOW_EVAL | ALLOW_SYSINFO);

    // 4. Bind packed resources (Release builds)
    sciter::archive::instance().open(aux::elements_of(resources));

    // 5. Create window (sciter::window constructor)
    // 6. PER-WINDOW options (with window handle) - AFTER window creation
    SciterSetOption(hwnd, SCITER_SET_DEBUG_MODE, TRUE);  // Debug inspector
}
```

### Common Mistakes

| Mistake | Symptom | Fix |
|---------|---------|-----|
| Missing `sciter::application::start()` | Silent crash on first API call | Call before any SciterSetOption |
| `SCITER_SET_DEBUG_MODE` with NULL | Crash | Use window handle, call after window created |
| Missing `OleInitialize()` | Drag-n-drop fails | Call before sciter::application::start() |

## DLL Deployment (CRITICAL)

**sciter.dll must be in the same directory as your exe.**

### CMake Setup

```cmake
# Copy sciter.dll to output directory
add_custom_command(TARGET MyApp POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/extern/sciter/bin/sciter.dll"
        "$<TARGET_FILE_DIR:MyApp>/sciter.dll"
    COMMENT "Copying sciter.dll"
)
```

### Custom Entry Point (wWinMain)

If using `wWinMain` instead of Sciter's default `wmain`:

```cmake
# Skip sciter-main.cpp's wmain entry point
target_compile_definitions(MyApp PRIVATE SKIP_MAIN)
```

## Resource Loading

Always use `this://app/` URL scheme for consistency between Debug and Release.

### Override `on_load_data()` in C++

```cpp
LRESULT on_load_data(LPSCN_LOAD_DATA pnmld) override {
    if (!aux::chars_of(pnmld->uri).like(WSTR("this://app/*")))
        return LOAD_OK;  // Let Sciter handle other URLs

    const wchar_t* relativePath = pnmld->uri + 11;  // Skip "this://app/"

#ifdef SCITER_USE_PACKFOLDER
    // Release: Load from packed archive
    aux::bytes data = sciter::archive::instance().get(relativePath);
    if (data.length) {
        ::SciterDataReady(pnmld->hwnd, pnmld->uri, data.start, UINT(data.length));
        return LOAD_OK;
    }
#else
    // Debug: Load from file system (uiBasePath_ + relativePath)
    std::wstring filePath = uiBasePath_ + relativePath;
    for (wchar_t& c : filePath) {
        if (c == L'/') c = L'\\';
    }
    // Read file and call SciterDataReady()
#endif
    return LOAD_DISCARD;
}
```

### CMake Resource Setup

```cmake
# Packfolder generates resources.cpp from UI folder
set(PACKFOLDER_EXE "${CMAKE_CURRENT_SOURCE_DIR}/extern/sciter/bin/packfolder.exe")

add_custom_command(
    OUTPUT "${CMAKE_CURRENT_SOURCE_DIR}/src/app/resources.cpp"
    COMMAND "${PACKFOLDER_EXE}" "${UI_SOURCE_DIR}" "${RESOURCES_CPP}" -v resources
    DEPENDS ${UI_FILES}
    COMMENT "Packing UI resources"
)

# Release: embedded resources, Debug: file copy
target_compile_definitions(MyApp PRIVATE
    $<$<CONFIG:Release>:SCITER_USE_PACKFOLDER>
)

# Debug: copy UI files for live editing
add_custom_command(TARGET MyApp POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E $<IF:$<CONFIG:Debug>,copy_directory,true>
        "${UI_SOURCE_DIR}" "$<TARGET_FILE_DIR:MyApp>/ui"
)
```

## Window Blur Effect

Use Sciter's native `Window.this.blurBehind` instead of Windows API.

### JavaScript

```javascript
document.on("ready", function() {
    Window.this.blurBehind = "dark source-auto";
});
```

### C++ Requirement

Set transparent BEFORE load():

```cpp
SciterSetOption(get_hwnd(), SCITER_TRANSPARENT_WINDOW, 1);
load(WSTR("this://app/index.html"));
```

## Script Tags

**Do NOT use** `type="module"` - Sciter has its own module system.

```html
<!-- Correct -->
<script src="settings.js"></script>

<!-- Wrong -->
<script type="module" src="settings.js"></script>
```

## CSS Quirks

Avoid `@import url()` - unreliable path resolution. Use `<link>` tags:

```html
<link rel="stylesheet" href="../shared/theme.css">
<link rel="stylesheet" href="settings.css">
```

## Window Sizing

Don't double-scale. Let Sciter measure DOM at native DPI:

```cpp
// Constructor: unscaled base size
: sciter::window(SW_POPUP, RECT{0, 0, 350, 460})

// After load(): measure and resize
sciter::dom::element container = rootEl.find_first(".container");
RECT contentRect = container.get_location(CONTENT_BOX);
SetWindowPos(hwnd, NULL, 0, 0, contentWidth, contentHeight, SWP_NOMOVE | SWP_NOZORDER);
```

## Deferred Updates After load()

`load()` runs JS synchronously, but C++ may set DOM attributes (e.g., `lang="en"`) AFTER load returns. Use `requestAnimationFrame` to defer JS that depends on C++-set attributes:

```javascript
// In initSubDialog() or similar init function:
requestAnimationFrame(function() {
    // C++ has finished setting attributes by now
    if (typeof applyTranslations === "function") applyTranslations();
});
```

**Do NOT use `call_function()` for initialization** — use DOM attributes + deferred JS instead.

## Layout on Transparent Windows (ClearType Pitfall)

**NEVER use `float: left/right` for multi-column layout on transparent windows.**

`float` pulls elements out of normal document flow → creates separate composite layers → Windows disables ClearType (sub-pixel anti-aliasing) on those layers → text appears washed out / blurry.

Use Sciter's native `flow: horizontal` (equivalent to CSS `display: flex`) instead:

```html
<!-- Wrap panels in a flow container -->
<div class="content-wrapper">
    <div class="left-panel">...</div>
    <div class="right-panel">...</div>
</div>
```

```css
.content-wrapper {
    flow: horizontal;
    width: *;
    height: *;
}
```

This keeps elements in normal document flow → ClearType preserved → text stays crisp.

### Force Repaint on Transparent Windows

Transparent windows (`WS_EX_LAYERED`) ignore `InvalidateRect` / `WM_PAINT`. To force Sciter to render a new frame after `SetWindowPos`, trigger a DOM mutation:

```cpp
// After SetWindowPos() on transparent window:
rootEl.set_attribute("force-paint", L"1");
rootEl.update(false);
rootEl.remove_attribute("force-paint");
rootEl.update(false);
```

## Prevent User Resize on Fixed-Layout Windows

Sciter transparent windows with fixed layouts must block user resizing at BOTH levels:

### HTML attribute

```html
<html window-resizable="false">
```

### C++ WM_NCHITTEST guard

The HTML attribute alone is not enough — Windows still shows resize cursors at edges. Block sizing hit-tests in `SubclassProc`:

```cpp
if (msg == WM_NCHITTEST) {
    LRESULT result = DefSubclassProc(hwnd, msg, wParam, lParam);
    // HTLEFT (10) to HTBOTTOMRIGHT (17) are sizing borders
    if (result >= HTLEFT && result <= HTBOTTOMRIGHT) {
        return HTBORDER;  // Non-resizable border
    }
    // ... handle HTCLIENT for drag zone
}
```

Apply to ALL dialog classes (SettingsDialog + SciterSubDialog) for consistent behavior across Win10/Win11.

## Light Mode Text on Translucent Windows

On layered windows, ClearType (sub-pixel AA) is replaced by grayscale AA. Dark-on-light text appears washed out because grayscale AA lacks the sharpness of ClearType.

**Fix**: maximize text contrast + use optical-sizing font:

```css
:root {
    /* Pure black instead of charcoal — compensates for grayscale AA blur */
    --text-primary: #000000;
    --text-secondary: #222222;
    --text-muted: #555555;

    /* Segoe UI Variable Text has optical sizing for better rendering without ClearType */
    --font-family: 'Segoe UI Variable Text', 'Segoe UI', sans-serif;
}
```

Dark mode doesn't need this fix — white-on-dark already has maximum contrast.

## Common Issues

| Issue | Cause | Fix |
|-------|-------|-----|
| Silent crash on startup | Missing `sciter::application::start()` | Call before any Sciter API |
| Crash on SciterSetOption | sciter.dll not found | Copy DLL to exe directory |
| CSS not loading | `@import` path resolution | Use `<link>` tags |
| White/black window | Wrong resource path | Use `this://app/` + `on_load_data()` |
| No blur effect | Using Windows API | Use `Window.this.blurBehind` in JS |
| Scripts fail | `type="module"` | Remove module attribute |
| Text blurry/washed out | `float` on transparent window kills ClearType | Use `flow: horizontal` |
| Light mode text faded | Grayscale AA on translucent window | `#000000` text + `Segoe UI Variable Text` |
| Window resize no repaint | Transparent window ignores `InvalidateRect` | DOM mutation trick (see above) |
| User can resize fixed window | HTML attr alone not enough | Block `HTLEFT..HTBOTTOMRIGHT` in `WM_NCHITTEST` |
