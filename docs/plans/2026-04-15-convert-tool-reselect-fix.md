# Fix: Convert Tool Re-select + Sequential Recovery

**Issue:** [#85](https://github.com/phatMT97/NexusKey/issues/85)  
**Date:** 2026-04-15

## Problem

Two linked bugs in QuickConvert hotkey flow:

1. **Re-select fails** — after paste, `TryReselect()` doesn't always re-highlight the converted text. TIER 2 (Shift+Left for generic apps) uses wrong length and insufficient post-paste delay.
2. **Sequential cycle breaks** — when re-select fails, next hotkey press copies empty clipboard, causing `Execute()` to return early or `IsNewSelection()` to reset the cycle with wrong text.

Bug 2 is a direct consequence of Bug 1.

## Root Cause Analysis

### TIER 2 length bug (`TryReselect` line 493)

```
After paste:  cursor = anchor.start + pastedLength
To re-select: Shift+Left x pastedLength
Current code: Shift+Left x originalSelLength  <-- WRONG when lengths differ
```

Lengths differ for encoding conversions (VNI->Unicode, TCVN3->Unicode).

### Post-paste delay

`Sleep(50)` before TIER 2 Shift+Left — insufficient for Electron apps (VS Code, Slack) that need 50-200ms to process paste.

### Sequential cascade failure

```
Press 1: select "xin chao" -> convert "XIN CHAO" -> paste -> re-select FAIL
Press 2: Ctrl+C -> clipboard empty -> WaitForClipboardUnicode timeout -> return early
         Sequential state never updated -> cycle stale/broken
```

## Changes

### A. Re-select reliability

| Location | Before | After |
|----------|--------|-------|
| `TryReselect` TIER 1 poll | 8 x 20ms = 160ms | 12 x 25ms = 300ms |
| `TryReselect` TIER 2 length | `originalSelLength` | `pastedLength` |
| `Execute` post-paste delay | `Sleep(50)` | `Sleep(100)` |

### B. Sequential recovery

**New field:** `int lastPastedLength = 0` in `SequentialState`

**New methods:**
- `SimulateShiftLeftSelect(int length)` — extracted from TIER 2 Shift+Left batch logic, reused for recovery
- `IsStillInCycle(HWND window) const` — checks timeout (<5s) + same window + anchor validation

**Recovery path in `Execute()`:**

After `SimulateCopy()` + `WaitForClipboardUnicode()`, if clipboard is empty:

```
if (clipText.empty() && sequential && autoPaste && IsStillInCycle(currentWindow)) {
    // Re-select failed last time -> recover
    SimulateShiftLeftSelect(seqState_.lastPastedLength)   // re-select old text
    Sleep(30)
    advance cycle index
    result = ApplyConversion(seqState_.originText, nextOption)
    WriteClipboard(result) -> SimulatePaste() -> Sleep(100) -> TryReselect()
    update seqState_ (lastPastedLength, contentHash, lastConvertTime)
} else if (clipText.empty()) {
    restore clipboard, return
}
```

**Anchor validation (Edit controls only):**

When `anchor.valid == true`, `IsStillInCycle` verifies cursor is at expected position (`seqState_.anchor.start + lastPastedLength`). Mismatch means user moved cursor -> abort recovery, reset cycle.

For generic apps (`anchor.valid == false`), trust the sequential cycle. Risk mitigated by 5s timeout.

### C. Normal flow update

After successful paste in normal sequential flow, store:
```cpp
seqState_.lastPastedLength = static_cast<int>(result.size());
```

## Files Changed

- `src/app/system/QuickConvert.h` — add `lastPastedLength`, declare new methods
- `src/app/system/QuickConvert.cpp` — all implementation changes

## Not Changed

- `HookEngine.cpp` — hotkey detection unchanged
- `ConvertToolDialog.cpp` — UI dialog unchanged
- `IsNewSelection()` — logic unchanged (recovery is in caller)

## Edge Cases

| Case | Handling |
|------|----------|
| App copies whole line on empty Ctrl+C (VS Code) | clipText not empty -> normal flow -> `IsNewSelection` resets cycle. Safer than trying to recover with wrong text. |
| User moves cursor between presses | Edit controls: anchor validation catches it. Generic apps: trust 5s timeout. Worst case: Ctrl+Z. |
| Surrogate pairs (emoji) | `result.size()` (wchar count) != cursor positions. Known limitation, rare in Vietnamese conversion. |
| Concurrent hotkey presses | `isRunning_` atomic prevents overlap. |
