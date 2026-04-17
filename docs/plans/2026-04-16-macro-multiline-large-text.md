# Macro Multi-line & Large Text Support (up to 20K chars)

## Decisions

| Decision | Choice | Rationale |
|----------|--------|-----------|
| Per-macro limit | 20480 (20K) chars | Covers HTML templates, email signatures, code snippets |
| File size guard | 5MB (from 1MB) | 20K × 250 macros = 5MB worst case; toml++ parses 5MB <50ms |
| Output method threshold | 200 chars | SendInput <200 chars is fast (~400ms); clipboard paste >200 eliminates lag |
| Non-Unicode code tables | Always SendInput | Clipboard is CF_UNICODETEXT only; TCVN3/VNI needs per-char encoding |
| Clipboard restore | No | No Vietnamese IME does this reliably; timing is non-deterministic |
| Clipboard history (Win+V) | ExcludeClipboardContentFromMonitorProcessing | Prevents macro expansions from polluting Win+V; Win10 1809+ |
| Newline storage format | `\n` as 2-char literal (unchanged) | Backward compatible; toml++ round-trips correctly |
| Textarea newline handling | Enter = real newline; JS converts ↔ `\n` literal on load/save | Intuitive UX; storage format unchanged |
| Import/export format | Unchanged (OpenKey compatible, `\n` literal) | No parser changes needed |
| Dialog size (Sciter) | 420×600 (from 420×480) | Fits textarea 4-5 lines; safe for 1366×768 screens |
| List preview | First line, max 60 chars, `⏎N` badge + tooltip on hover | Compact scan + detail on demand |

---

## Changes

### 1. Config Layer

**[MODIFY] `src/core/config/ConfigManager.cpp`**
- `kMaxMacroValueLen`: 512 → 20480
- `kMaxConfigFileSizeBytes`: 1MB → 5MB
- No changes to LoadMacros/SaveMacros logic

---

### 2. Expansion Engine

**[MODIFY] `src/app/system/HookEngine.h`**
- Add `static constexpr size_t kMacroClipboardThreshold = 200;`

**[MODIFY] `src/app/system/HookEngine.cpp`**

**`TryExpandMacro()`** — add clipboard paste branch before `DispatchSendInput()` (after line 2595):

```
if (currentCodeTable_ != CodeTable::Unicode) {
    // Non-Unicode: always SendInput (encoding requires per-char conversion)
    build charEvents with emitChar() as today
    DispatchSendInput(bsEvents, charEvents)
} else if (expansion chars > kMacroClipboardThreshold) {
    // Large Unicode macro: clipboard paste
    1. Convert \n escapes → \r\n in expansion string
    2. DispatchSendInput(bsEvents, emptyCharEvents)  // backspaces only
    3. ClipboardPaste(convertedText)
} else {
    // Small Unicode macro: SendInput as today
    DispatchSendInput(bsEvents, charEvents)
}
```

Note: "expansion chars" counts the display characters, not the raw string length. Each `\n` escape (2 chars) produces 1 newline. The threshold comparison should use the converted length for accuracy, but using raw string length is acceptable since the difference is negligible at the 200-char threshold.

**`SetClipboardText()`** — add clipboard history exclusion:

```cpp
// After SetClipboardData(CF_UNICODETEXT, hMem):
static UINT cfExclude = RegisterClipboardFormat(
    L"ExcludeClipboardContentFromMonitorProcessing");
if (cfExclude) {
    HGLOBAL hExclude = GlobalAlloc(GMEM_MOVEABLE, sizeof(DWORD));
    if (hExclude) {
        auto* p = static_cast<DWORD*>(GlobalLock(hExclude));
        if (p) { *p = 0; GlobalUnlock(hExclude); }
        SetClipboardData(cfExclude, hExclude);
    }
}
```

This also fixes the existing VB6 clipboard paste path (same function).

**`ClipboardPaste()`** — release held modifier keys before Ctrl+V:

If the user triggers a macro with a shifted character (e.g. `!` = Shift+1), Shift is
still physically held when `ClipboardPaste()` fires. The target app would receive
`Ctrl+Shift+V` instead of `Ctrl+V` (often "Paste without formatting" or no-op).

Fix: before sending Ctrl+V, inject key-up events for any physically-held modifiers
(Shift, Alt, Win). After Ctrl+V, reinject key-down to restore their state:

```cpp
void HookEngine::ClipboardPaste(const std::wstring& text) {
    if (text.empty()) return;
    if (!SetClipboardText(text)) {
        SendCharEvents(text);
        return;
    }

    // Release held modifiers to prevent Ctrl+Shift+V / Ctrl+Alt+V
    struct ModRelease { WORD vk; WORD scan; bool wasDown; };
    ModRelease mods[] = {
        { VK_SHIFT,   (WORD)MapVirtualKeyW(VK_SHIFT, MAPVK_VK_TO_VSC),   (GetKeyState(VK_SHIFT) & 0x8000) != 0 },
        { VK_MENU,    (WORD)MapVirtualKeyW(VK_MENU, MAPVK_VK_TO_VSC),    (GetKeyState(VK_MENU) & 0x8000) != 0 },
        { VK_LWIN,    (WORD)MapVirtualKeyW(VK_LWIN, MAPVK_VK_TO_VSC),    (GetKeyState(VK_LWIN) & 0x8000) != 0 },
    };
    std::vector<INPUT> preEvents, postEvents;
    for (auto& m : mods) {
        if (m.wasDown) {
            INPUT up{}; up.type = INPUT_KEYBOARD;
            up.ki.wVk = m.vk; up.ki.wScan = m.scan;
            up.ki.dwFlags = KEYEVENTF_KEYUP; up.ki.dwExtraInfo = NEXUSKEY_EXTRA_INFO;
            preEvents.push_back(up);

            INPUT down{}; down.type = INPUT_KEYBOARD;
            down.ki.wVk = m.vk; down.ki.wScan = m.scan;
            down.ki.dwExtraInfo = NEXUSKEY_EXTRA_INFO;
            postEvents.push_back(down);
        }
    }

    sending_ = true;
    if (!preEvents.empty())
        TrackedSendInput(preEvents.data(), (UINT)preEvents.size());

    // Ctrl+V
    INPUT inputs[4] = { /* ... existing code ... */ };
    TrackedSendInput(inputs, 4);

    if (!postEvents.empty())
        TrackedSendInput(postEvents.data(), (UINT)postEvents.size());
    sending_ = false;
    RecordSynthDispatch();
}
```

This fixes a pre-existing bug in the VB6 clipboard paste path as well.

---

### 3a. Sciter Macro Dialog UI

**[MODIFY] `src/app/ui/macro/macro.html`**
- Replace `<input type="text" id="macro-content">` with `<textarea id="macro-content" maxlength="20000">`
- Add `<span id="char-counter">0 / 20000</span>` below textarea
- Update hint text: "Nhấn Enter để xuống dòng"

**[MODIFY] `src/app/ui/macro/macro.css`**
- Textarea: height ~100px, `font-family: monospace`, vertical scroll, `resize: none` (prevent drag-resize over other controls)
- Counter: font-size 11px, align right, color red when >18000
- Macro item tooltip: `white-space: pre-wrap` for multi-line preview

**[MODIFY] `src/app/ui/macro/macro.js`**
- Load (C++ → textarea): `content.replace(/\\n/g, "\n")` — 2-char escape → real newline
- Save (textarea → C++): `content.replace(/\n/g, "\\n")` — real newline → 2-char escape
- Counter: update on `input` event
- `addMacroToList()`: first line only, max 60 chars + `...`, `⏎N` badge if multi-line
- Set `title` attribute on macro-item with first 500 chars (tooltip on hover)

**[MODIFY] `src/app/dialogs/MacroTableDialog.cpp`**
- Dialog dimensions: 420×480 → 420×600

---

### 3b. Classic (Win32) Macro Dialog UI

**[MODIFY] `src/app/classic/ClassicMacroTableDialog.cpp`**

- **`CreateControls()`**: `editValue_` EDIT control
  - Add `ES_MULTILINE | ES_AUTOVSCROLL | ES_WANTRETURN | WS_VSCROLL`, remove `ES_AUTOHSCROLL`
  - `WS_VSCROLL` required for visible scrollbar (without it, `ES_AUTOVSCROLL` scrolls but no scrollbar renders)
  - `EM_SETLIMITTEXT` = 20480
  - Height: ~80px (4 lines) instead of single-line `editH`
  - Layout: move below key input (full width) instead of inline
  - Update Tab order (`WS_TABSTOP`): Key → Value textarea → Add button (logical keyboard nav)

- **`AddMacro()`**: replace `wchar_t value[1024]` stack buffer
  - Use `GetWindowTextLengthW` + `std::wstring` dynamic buffer

- **Newline conversion** (same logic as Sciter, but `\r\n`):
  - Load to edit: `\n` (2 chars) → `\r\n` (real newline in Win32 EDIT)
  - Save from edit: `\r\n` → `\n` (2 chars)

- **ListView preview**: truncate content column, show `⏎N` for multi-line

- **Dialog height**: increase proportionally to fit multi-line edit

---

### 4. Import/Export

No code changes needed. Format unchanged — `\n` stored as literal 2-char escape, one line per macro in file.

---

## Edge Cases

| Case | Behavior | Notes |
|------|----------|-------|
| Clipboard locked by another app | Fallback to SendInput | Existing fallback at line 1497-1500; slow for large macros but functional |
| User types during paste | `sending_` flag + `RecordSynthDispatch()` guard | Same pattern as VB6 paste, proven stable |
| Auto-capitalize + clipboard | Works | `towupper()` runs before paste path selection |
| Macro is all newlines | Clipboard paste sends `\r\n` = Enter keys | Valid behavior |
| `ExcludeClipboardContentFromMonitorProcessing` on old Windows | No-op | `RegisterClipboardFormat` returns valid ID; clipboard history doesn't exist pre-1809 |
| Non-Unicode + macro >200 chars | SendInput (slow but correct encoding) | Acceptable: TCVN3/VNI + large macros is extremely rare |
| 50 macros × 20K = 1MB+ config | OK with 5MB guard | toml++ handles it; RAM ~5MB is negligible |
| Shift held during macro trigger (e.g. `!`) | Release Shift before Ctrl+V, restore after | Prevents Ctrl+Shift+V (paste-plain) misfire |
| Clipboard overwrite for >200 char macros | User loses current clipboard content | Known tradeoff; no Vietnamese IME restores clipboard reliably |
| `\n` in paths (e.g. `path\name`) | Interpreted as newline | Pre-existing limitation; not changed by this plan |

### UI Hint for Clipboard Overwrite

In the Sciter macro dialog, when a macro exceeds 200 characters, show an info hint below
the character counter: "Macro dài sẽ dùng Clipboard để dán (nội dung Clipboard hiện tại sẽ bị ghi đè)."
Same hint in Classic UI via static text control. Only visible when content length > 200.

## Verification

### Automated
- Build: `cmake --build . --target NexusKey --config Debug`
- ConfigManager: verify load/save 20K macro with `\n` content round-trips correctly

### Manual
1. Sciter dialog: add multi-line macro via textarea, verify character counter
2. Classic dialog: add multi-line macro via multi-line EDIT, verify conversion
3. Type shortcut in Notepad → verify multi-line output (small macro: SendInput)
4. Type shortcut for >200 char macro → verify clipboard paste, instant output
5. Check Win+V → macro expansion should NOT appear in history
6. Test small macros (<200 chars) → verify SendInput behavior unchanged
7. Import/export multi-line macros → verify `\n` preserved correctly
8. Test with TCVN3 encoding + large macro → verify SendInput fallback
9. Type macro triggered by `!` (Shift+1) → verify Ctrl+V not Ctrl+Shift+V
10. After >200 char macro expands → verify Win+V does NOT show expansion entry
11. Check clipboard hint appears/disappears as content crosses 200 char threshold
