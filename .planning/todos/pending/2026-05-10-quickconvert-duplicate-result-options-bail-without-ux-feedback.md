---
created: 2026-05-10T11:41:28.182Z
title: QuickConvert duplicate-result options bail without UX feedback
area: general
files:
  - src/app/system/QuickConvert.cpp:253-258
  - src/app/system/QuickConvert.cpp:290-296
  - src/app/system/QuickConvert.cpp:613-636
---

## Problem

QuickConvert sequential mode silently bails when the next cycle option produces a result identical to the current clipboard text. This happens for short inputs where two options (e.g. `kCapsFirst` "Sentence Case" and `kCapsEach` "Title Case") yield the same output — for a single word `"hello"`, both `ToSentenceCase()` and `ToTitleCase()` return `"Hello"`.

The bail at `QuickConvert.cpp:254` (`if (result == clipText)`) returns BEFORE the toast at line 290, so:
- No paste happens (correct — nothing changed)
- No toast appears (problem — user has zero feedback that the hotkey was processed)
- Previous toast (1500ms) is still on screen → user perceives the previous option as "running again"

**Verified via log session 2026-05-10** with input length 5 chars on `enabledOptions = [kToUpper, kToLower, kCapsFirst, kCapsEach, kRemoveDiacritics]`:

```
[24.834] Applied conversion option 3   ← Hoa đầu câu, paste, toast
[25.649] Applied conversion option 4   ← Hoa Từng Chữ, "Result same as clip text", BAIL — no toast
[26.635] Applied conversion option 5   ← Bỏ dấu, paste, toast
```

User reported: "Hoa đầu câu chạy 2 lần, rồi qua cái khác". Reality: option 4 bailed silently while option 3's toast was still fading.

This is a pre-existing bug, not related to the clipboard race-condition patch (PR pending). The `restoreSavedClipboardIfSafe()` introduced by that patch sits inside the bail and behaves identically to the prior `WriteClipboard(savedClipboard)` for the no-3rd-party-touch case.

## Solution

Two candidate fixes (UX direction not yet decided by anh — pick one or hybrid):

**(A) Auto-advance on no-change** — UX-natural, every press = one visible change.
At `QuickConvert.cpp:253-258`, instead of `return`, increment `currentIndex` and re-apply, capped at `enabledOptions.size()` iterations to prevent infinite loops if every remaining option is a no-op for the input. Update `seqState_.contentHash` only after the final applied result.

**(B) Show "no change" toast** — minimal patch (~3 lines), but user still has to press extra times to skip duplicates.
```cpp
if (result == clipText) {
    if (config_.alertDone && toastMsg) {
        ToastPopup::Show(std::wstring(toastMsg) + L" (không đổi)", 1000);
    }
    restoreSavedClipboardIfSafe();
    return;
}
```

**Repro**: select `"hello"` (5 chars), enable `capsFirst + capsEach + autoPaste + sequential`, press hotkey 5+ times. Watch toast — option 4 will silently skip.

**Decision needed**: anh decides UX direction (A vs B vs hybrid) before implementation.
