---
name: CJK layout detection architecture
description: How NexusKey handles Japanese/Chinese/Korean keyboard layouts — auto-toggle approach, detection channels, known constraints, and how to extend for new IMEs
type: project
---

## CJK Layout Auto-Toggle (implemented 2026-04-09)

When user switches to a CJK keyboard layout (Japanese/Chinese/Korean), NexusKey auto-switches to E mode. When returning to English, restores saved mode. User can Ctrl+Shift override at any time.

**Why:** Vietnamese engine interferes with CJK IME composing. But CJK "A" sub-mode (alphabet/romaji) is compatible with Vietnamese — user should be able to Ctrl+Shift back to V.

**How to apply:** Any future CJK-related changes should preserve this architecture: auto-toggle mode (not suppress engine), allow hotkey override, detect via poll timer + focus events (not per-keystroke).

## Key Design Decisions

1. **Auto-toggle, NOT engine suppress** — `OnLayoutChanged()` changes `vietnameseMode_` directly. Line 804 only checks `vietnameseMode_`, not `layoutSuppressed_`. This lets user override with Ctrl+Shift and type Vietnamese on CJK "A" sub-mode.

2. **`ImmGetOpenStatus()` doesn't work from WH_KEYBOARD_LL** — returns NULL cross-thread. Empirically tested 2026-04-09. Don't try again.

3. **Raw keystrokes identical in both CJK sub-modes** — WH_KEYBOARD_LL sees VK_A for both "A" and "あ" mode. Can't distinguish by key data.

4. **VK_DBE signals exist but unreliable** — `VK_DBE_HIRAGANA (0xF2)` and `VK_DBE_ALPHANUMERIC (0xF0)` appear on A↔あ toggle (Shift+CapsLock). 0xF2 only appears as UP event. Pattern varies by IME vendor. Decided NOT to use — too fragile, manual override is simpler.

5. **16-keystroke throttle was the original bug** — caused delayed detection when switching layout via mouse click. Removed entirely. Detection now via 200ms poll timer + focus events + modifier key-up.

## Detection Channels

| Channel | Trigger | Latency |
|---------|---------|---------|
| Focus change event | Win+Space, Alt+Tab | ~0ms |
| Poll timer (200ms) | Mouse-click language bar | ≤200ms |
| Modifier key-up | Ctrl+Shift, Alt+Shift | ~0ms |

## Files Changed

- `HookEngine.cpp:ToggleVietnameseMode()` — removed `layoutSuppressed_` block
- `HookEngine.cpp:OnLayoutChanged()` — auto-toggle mode + save/restore via `modeBeforeCjk_`
- `HookEngine.cpp:ProcessKeyDown()` line 804 — only checks `vietnameseMode_`
- `HookEngine.cpp:FocusPollTimerProc()` — added `CheckLayoutChange()` every 200ms
- `HookEngine.h` — added `modeBeforeCjk_`, removed `layoutCheckCounter_`/`kLayoutCheckInterval`

## If Adding Support for New CJK IMEs

1. Check `IsIncompatibleLayout()` — add new `LANG_*` if needed
2. Test that auto-E fires on layout switch and auto-restore works on return
3. Test Ctrl+Shift override on the IME's alphabet sub-mode
4. Do NOT try to detect sub-modes automatically — `ImmGetOpenStatus` is dead from hook thread, VK_DBE signals vary by vendor
