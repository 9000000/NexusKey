# Universal SendInput Output Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace PostMessage whitelist with universal SendInput KEYEVENTF_UNICODE for all ReplaceComposition output, eliminating case-by-case workarounds for subclassed controls.

**Architecture:** Remove `UsePostMessage()` entirely. All ReplaceComposition output uses SendInput with batch strategy (Win32 native) or split+delay strategy (Electron/Console). Passthrough optimization (physical key pass-through for plain appends) is preserved unchanged. Add synthEventsPending_ watchdog timer (500ms) to auto-reset stuck counters. Relax `hadSynthInWord_` guard to only block passthrough on Electron apps, preserving passthrough for Win32 native apps after diacritical marks.

**Tech Stack:** C++20, Win32 SendInput API, KEYEVENTF_UNICODE, low-level keyboard hook

---

## File Map

| File | Action | Responsibility |
|---|---|---|
| `src/app/system/HookEngine.cpp` | Modify | Remove UsePostMessage, remove PostMessage paths, add watchdog, relax guard |
| `src/app/system/HookEngine.h` | Modify | Add `lastSynthSendTime_` field for watchdog |
| `tests/TelexEngineTest.cpp` | No change | Engine tests unaffected (engine doesn't know about output method) |
| `docs/plans/2026-03-28-manual-test-matrix.md` | Create | Manual test matrix for Windows testing |

> **Note:** Engine tests (TelexEngineTest, VniEngineTest, SpellCheckerTest) are unaffected — they test engine logic, not output delivery. Output method changes are Windows-only (HookEngine) and require manual testing.

---

### Task 1: Remove UsePostMessage and PostMessage output paths

**Files:**
- Modify: `src/app/system/HookEngine.cpp:1053-1073` (delete UsePostMessage)
- Modify: `src/app/system/HookEngine.cpp:1412` (remove usePost variable)
- Modify: `src/app/system/HookEngine.cpp:1514-1521` (remove PostMessage branch in Unicode path)
- Modify: `src/app/system/HookEngine.cpp:1109-1131` (simplify SendBackspaceEvents)
- Modify: `src/app/system/HookEngine.cpp:1133-1150` (simplify SendCharEvents)
- Modify: `src/app/system/HookEngine.cpp:1456-1489` (remove PostMessage branch in encoded path)

- [ ] **Step 1: Delete UsePostMessage function**

Remove the entire function at line 1053-1073:

```cpp
// DELETE this entire function:
static bool UsePostMessage(HWND hwnd) {
    // ... all contents ...
}
```

- [ ] **Step 2: Remove `usePost` variable and PostMessage branch from Unicode ReplaceComposition path**

In `ReplaceComposition()`, replace the dual-path dispatch with SendInput-only:

```cpp
    // ── Unicode path (fast path, zero overhead) ──
    size_t backspaceCount = previousComposition_.size() - commonLen;
    std::wstring toSend = newText.substr(commonLen);

    HOOK_LOG(L"  ReplaceComposition: prev='%s' new='%s' common=%zu BS=%zu send='%s'",
             previousComposition_.c_str(), newText.c_str(), commonLen, backspaceCount,
             toSend.c_str());

    // SendInput path: all windows use SendInput with KEYEVENTF_UNICODE.
    // Batch for Win32 native, split+delay for Electron/Console.
    bool needEmpty = (backspaceCount > 0 && !skipEmptyChar_);
    if (needEmpty) backspaceCount++;  // +1 to also delete the U+202F

    if (backspaceCount > 0 || !toSend.empty()) {
        std::vector<INPUT> bsEvents;
        std::vector<INPUT> charEvents;
        WORD bsScan = static_cast<WORD>(MapVirtualKeyW(VK_BACK, MAPVK_VK_TO_VSC));

        if (backspaceCount > 0) {
            bsEvents.reserve((needEmpty ? 2 : 0) + backspaceCount * 2);
            if (needEmpty) {
                AppendUnicodeEvent(bsEvents, 0x202F);
            }
            for (size_t i = 0; i < backspaceCount; ++i) {
                AppendVkEvent(bsEvents, VK_BACK, bsScan);
            }
        }

        if (!toSend.empty()) {
            charEvents.reserve(toSend.size() * 2);
            for (wchar_t ch : toSend) {
                AppendUnicodeEvent(charEvents, ch);
            }
        }

        DispatchSendInput(bsEvents, charEvents);
    }

    previousComposition_ = newText;
```

- [ ] **Step 3: Remove `usePost` from Non-Unicode (encoded) ReplaceComposition path**

Same change for the encoded path (~line 1456-1489): remove `if (usePost)` branch, keep only SendInput path. Replace the PostMessage calls with the same DispatchSendInput pattern used in Unicode path.

- [ ] **Step 4: Remove `usePost` parameter from SendBackspaceEvents and SendCharEvents**

These functions are now only called from `SendBackspaces()` (HandleBackspace empty-engine path). Simplify:

```cpp
void HookEngine::SendBackspaceEvents(HWND target, size_t count) {
    // Always use SendInput
    WORD bsScan = static_cast<WORD>(MapVirtualKeyW(VK_BACK, MAPVK_VK_TO_VSC));
    std::vector<INPUT> events;
    events.reserve(count * 2);
    for (size_t i = 0; i < count; ++i) {
        AppendVkEvent(events, VK_BACK, bsScan);
    }
    sending_ = true;
    UINT sent = SendInput(static_cast<UINT>(events.size()), events.data(), sizeof(INPUT));
    synthEventsPending_ += static_cast<int>(sent);
    sending_ = false;
}

void HookEngine::SendCharEvents(HWND target, const std::wstring& text) {
    // Always use SendInput
    std::vector<INPUT> events;
    events.reserve(text.size() * 2);
    for (wchar_t ch : text) {
        AppendUnicodeEvent(events, ch);
    }
    sending_ = true;
    UINT sent = SendInput(static_cast<UINT>(events.size()), events.data(), sizeof(INPUT));
    synthEventsPending_ += static_cast<int>(sent);
    sending_ = false;
}
```

Update the declarations in `HookEngine.h` to remove the `bool usePost` parameter.

- [ ] **Step 5: Remove debug log block with GetClassName/GetParent**

Remove the temporary debug log block added earlier (the `{ wchar_t cls[64]... }` block in ReplaceComposition). It was for diagnosing the ComboBox issue which is now moot.

- [ ] **Step 6: Build and verify compilation**

Run (from WSL):
```bash
powershell.exe -Command "cd '\\wsl.localhost\Ubuntu-24.04\home\phatmt\code\NexusKey\build'; cmake --build . --target NexusKey --config Debug 2>&1"
```
Expected: compiles with 0 errors.

Also verify Linux test build still compiles (engine tests don't touch HookEngine):
```bash
cmake --build build-linux --target NextKeyTests && ./build-linux/tests/NextKeyTests
```
Expected: 771 tests pass.

- [ ] **Step 7: Commit**

```bash
git add src/app/system/HookEngine.cpp src/app/system/HookEngine.h
git commit -m "refactor: replace PostMessage whitelist with universal SendInput output"
```

---

### Task 2: Add synthEventsPending_ watchdog timer

**Files:**
- Modify: `src/app/system/HookEngine.h` (add `lastSynthSendTime_` field)
- Modify: `src/app/system/HookEngine.cpp` (add watchdog check, record send time)

- [ ] **Step 1: Add lastSynthSendTime_ field to HookEngine.h**

After `synthEventsPending_` declaration (~line 148):

```cpp
    int synthEventsPending_ = 0;
    DWORD lastSynthSendTime_ = 0;  // GetTickCount() of last SendInput call (for watchdog)
```

- [ ] **Step 2: Record send time in DispatchSendInput**

At the end of `DispatchSendInput()`, after the SendInput calls:

```cpp
    sending_ = false;
    lastSynthSendTime_ = GetTickCount();
```

Also in `SendBackspaceEvents` and `SendCharEvents` (the non-usePost path), add same line after `sending_ = false`.

Also in `InjectKey()` after `sending_ = false`:

```cpp
    sending_ = false;
    lastSynthSendTime_ = GetTickCount();
```

- [ ] **Step 3: Add watchdog check at top of ProcessKeyDown**

In `ProcessKeyDown()`, early in the function (after the `sending_` check, before the modifier check), add:

```cpp
    // Watchdog: reset synthEventsPending_ if stuck for > 500ms.
    // Covers event loss in Electron multi-process apps where synthetic
    // events can be dropped under heavy CPU load, causing cascading
    // re-injection and ghost characters.
    if (synthEventsPending_ > 0) {
        DWORD elapsed = GetTickCount() - lastSynthSendTime_;
        if (elapsed > 500) {
            HOOK_LOG(L"  watchdog: synthEventsPending_ reset from %d (stuck %ums)",
                     synthEventsPending_, elapsed);
            synthEventsPending_ = 0;
        }
    }
```

- [ ] **Step 4: Build and verify**

```bash
powershell.exe -Command "cd '\\wsl.localhost\Ubuntu-24.04\home\phatmt\code\NexusKey\build'; cmake --build . --target NexusKey --config Debug 2>&1"
```

- [ ] **Step 5: Commit**

```bash
git add src/app/system/HookEngine.cpp src/app/system/HookEngine.h
git commit -m "fix: add synthEventsPending_ watchdog timer to prevent ghost chars"
```

---

### Task 3: Relax hadSynthInWord_ passthrough guard

**Files:**
- Modify: `src/app/system/HookEngine.cpp:894-895` (passthrough condition)

- [ ] **Step 1: Change passthrough guard to only block on Electron**

Current code (line 894-895):
```cpp
    if (!autoCapped && currentCodeTable_ == CodeTable::Unicode &&
        !hadSynthInWord_ && !isElectronApp_ &&
```

Change to:
```cpp
    if (!autoCapped && currentCodeTable_ == CodeTable::Unicode &&
        !(hadSynthInWord_ && isElectronApp_) &&
```

Logic change:
- **Before:** block passthrough if hadSynthInWord_ OR isElectronApp_ (either condition blocks)
- **After:** block passthrough only if hadSynthInWord_ AND isElectronApp_ (both required)
- **Effect:** Win32 native apps keep passthrough after diacritical marks. Electron still blocks (prevents out-of-order).

- [ ] **Step 2: Update the comment block above**

Replace the comment block (lines 886-893):
```cpp
    // Passthrough: let physical key reach app directly (zero overhead, no SendInput).
    // Blocked when BOTH conditions are true:
    //   - hadSynthInWord_: synth already sent in this word, AND
    //   - isElectronApp_: Electron/Qt multi-process architecture where physical
    //     WM_KEYDOWN and synthetic VK_PACKET can arrive out of order.
    // Win32 native apps have single FIFO message queue — mixing is safe.
    // Console apps are NOT Electron (isElectronApp_=false) so passthrough is safe.
```

- [ ] **Step 3: Build and verify**

```bash
powershell.exe -Command "cd '\\wsl.localhost\Ubuntu-24.04\home\phatmt\code\NexusKey\build'; cmake --build . --target NexusKey --config Debug 2>&1"
```

- [ ] **Step 4: Commit**

```bash
git add src/app/system/HookEngine.cpp
git commit -m "perf: relax hadSynthInWord_ guard — passthrough preserved for Win32 native apps"
```

---

### Task 4: Remove NEXTKEY_DEBUG from CMakeLists.txt

**Files:**
- Modify: `CMakeLists.txt:160`

- [ ] **Step 1: Remove NEXTKEY_DEBUG**

Change line 160 from:
```cmake
target_compile_definitions(NextKeyApp PRIVATE SKIP_MAIN NEXUSKEY_HOOK_ENGINE NEXTKEY_DEBUG)
```
Back to:
```cmake
target_compile_definitions(NextKeyApp PRIVATE SKIP_MAIN NEXUSKEY_HOOK_ENGINE)
```

- [ ] **Step 2: Commit**

```bash
git add CMakeLists.txt
git commit -m "chore: remove NEXTKEY_DEBUG from Release build"
```

---

### Task 5: Create manual test matrix

**Files:**
- Create: `docs/plans/2026-03-28-manual-test-matrix.md`

- [ ] **Step 1: Write the test matrix**

```markdown
# Manual Test Matrix — Universal SendInput Output

## Test Words
- "thương" (t-h-u-o-w-n-g) — modifier + coda consonants after
- "đã" (d-d-a-a-j) — stroke modifier + tone
- "người" (n-g-u-o-w-i-r) — horn modifier + tone
- "tế" (t-e-e-s) — circumflex + tone
- "xuyên" + 'e' (x-u-y-e-e-n-e) — circumflex escape after coda
- "test" (t-e-s-s-t) — English word via tone escape
- YouTube hotkeys: F (fullscreen), M (mute), K (play/pause)
- Ctrl+Z after typing Vietnamese word (undo test)

## Test Matrix

| App + Control | thương | đã | người | tế | xuyên+e | test | Hotkeys | Ctrl+Z | Result |
|---|---|---|---|---|---|---|---|---|---|
| Notepad++ Scintilla (editor) | | | | | | | N/A | | |
| Notepad++ Find (ComboBox Edit) | | | | | | | N/A | | |
| Notepad++ Replace (ComboBox Edit) | | | | | | | N/A | | |
| Windows Notepad | | | | | | | N/A | | |
| WordPad (RichEdit) | | | | | | | N/A | | |
| Chrome address bar | | | | | | | N/A | | |
| Chrome text input (web page) | | | | | | | N/A | | |
| YouTube (video playing, no text) | N/A | N/A | N/A | N/A | N/A | N/A | | N/A | |
| VS Code editor | | | | | | | N/A | | |
| Telegram chat | | | | | | | N/A | | |
| Windows Terminal (cmd) | | | | | | | N/A | | |
| Microsoft Word | | | | | | | N/A | | |
| Run dialog (Win+R) | | | | | | | N/A | | |

## Pass Criteria
- All Vietnamese words display correctly with proper diacritics
- "test" (tone escape) produces "test" not "tést"
- "xuyên"+e produces "xuyene" (circumflex escape works)
- YouTube hotkeys work when video is focused
- Ctrl+Z undoes the last typed word correctly
- No ghost characters after deleting all text and retyping
```

- [ ] **Step 2: Commit**

```bash
git add docs/plans/2026-03-28-manual-test-matrix.md
git commit -m "docs: add manual test matrix for universal SendInput output"
```

---

## Self-Review Checklist

1. **Spec coverage:** UsePostMessage removal (Task 1), watchdog (Task 2), passthrough guard (Task 3), debug cleanup (Task 4), manual tests (Task 5) — all covered.
2. **Placeholder scan:** All code blocks contain actual implementation code. No TBD/TODO.
3. **Type consistency:** `synthEventsPending_` (int), `lastSynthSendTime_` (DWORD), `hadSynthInWord_` (bool) — consistent across all tasks.
4. **Regression check:** Linux engine tests (771) unaffected — HookEngine is Windows-only. Manual test matrix covers 13 app/control combinations.
