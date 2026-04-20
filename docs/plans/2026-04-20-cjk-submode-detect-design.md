# CJK Sub-mode Detection — Design

**Date:** 2026-04-20
**Status:** Design only, not implemented
**Scope:** HookEngine layout auto-disable refinement

---

## Problem

Chinese/Japanese/Korean TIPs expose two sub-modes: **native** (hanzi / kana / hangul)
and **alphanumeric** (English direct input). Current NexusKey auto-disables on
any CJK HKL regardless of sub-mode, forcing users to manually toggle V/E back on
when they switch the CJK IME to English sub-mode.

User goal: when toggle is enabled, NexusKey stays active during English
sub-mode of a CJK TIP and silently bypasses during native sub-mode.

## Why not TSF DLL

TSF TIPs are mutually exclusive per-thread. When a Chinese TIP is active,
NexusKey's TSF DLL is deactivated — it cannot observe the other TIP's state.
Detection must happen from **HookEngine** (the always-running app-side hook),
not from the TSF DLL.

## Why not system-wide injected hook

A `WH_GETMESSAGE` hook catching `WM_IME_NOTIFY / IMN_SETOPENSTATUS` gives
realtime detection but requires a DLL loaded into every process — heavy, stability
landmine, not justified by the latency gap.

## Chosen approach

Poll `ImmGetConversionStatus` on the foreground window's input context from the
**existing `focusPollTimer_`** (200 ms, main thread, `HookEngine.cpp:174`).
No new threads, no DLL injection, no subprocess.

Latency: ≤200 ms (1–2 keystrokes of lag right after a sub-mode flip — acceptable).

---

## Scope

- **Chinese TIPs** — Microsoft Pinyin, Wubi, Bopomofo: read `IME_CMODE_NATIVE` bit
- **Japanese MS-IME** — `IME_CMODE_NATIVE` set = hiragana/katakana, clear = "A" direct input
- **Korean MS-IME** — `IME_CMODE_NATIVE` set = Hangul, clear = English direct
- **Third-party TIPs** (Sogou, Google IME) — best-effort. Most bridge IMM32 so it
  works, but no guarantee. User can disable toggle if issues.

Same `IME_CMODE_NATIVE` bit mask, same code path for all — reuse the existing
`IsIncompatibleLayout()` CJK blacklist.

Important distinction — the prior memory note "Japanese A vs あ indistinguishable"
referred to `GetKeyboardLayout()`, which cannot tell sub-mode apart. It does not
apply to `ImmGetConversionStatus`, which reads the HIMC conversion flags
directly.

---

## Toggle & config (7-step checklist)

1. `FeatureFlags::DETECT_CJK_SUBMODE` bit in `SharedState.h`
   (uses one of the remaining slots in `extFeatureFlags`)
2. `TypingConfig::detectCjkSubmode` bool
3. Encode/decode in `Encode/DecodeFeatureFlags`
4. Load/save in `ConfigManager` TOML
5. Settings UI checkbox (sub-option under "Auto-disable on CJK layout")
6. `HookEngine::ApplyConfig` → copy to `detectCjkSubmode_`
7. **`SharedStateManager::Write()` field copy** (per rule 5 — missing this = Debug-only regression)

Default: **OFF**. Users who don't use CJK TIPs see no behavior change.

---

## HookEngine state

```cpp
bool detectCjkSubmode_  = false;  // toggle
bool submodeSuppressed_ = false;  // true = CJK native sub-mode (silent suppress)
// existing (semantics unchanged, only active when toggle OFF):
bool layoutSuppressed_;
bool cachedIsCompatLayout_;
bool modeBeforeCjk_;
```

Two independent suppression states:

| State | Trigger | Beep | V/E change | Commit composition |
|---|---|---|---|---|
| `layoutSuppressed_` (toggle OFF) | HKL is CJK | Yes | V → E, saved for restore | Yes |
| `submodeSuppressed_` (toggle ON) | CJK HKL + native sub-mode | **No** | **No** | Yes |

Sub-mode suppression is **silent** — no tray icon flicker, no beep. Tray V/E icon
reflects the user's deliberate mode, not their Chinese IME's Shift state.

---

## `CheckLayoutChange()` flow

```cpp
void HookEngine::CheckLayoutChange() {
    HWND fg = GetForegroundWindow();
    if (!fg) return;
    DWORD tid = GetWindowThreadProcessId(fg, nullptr);
    HKL hkl = GetKeyboardLayout(tid);
    bool isCjk = IsIncompatibleLayout(hkl);

    if (!detectCjkSubmode_) {
        // Toggle OFF: existing HKL-level suppress (beep + V→E auto-switch)
        bool compat = !isCjk;
        if (compat != cachedIsCompatLayout_) {
            cachedIsCompatLayout_ = compat;
            OnLayoutChanged(compat);
        }
        return;
    }

    // Toggle ON: sub-mode decides suppression, HKL alone never suppresses
    if (layoutSuppressed_) {
        // User just turned the toggle on while on CJK layout —
        // clean up the HKL-level suppress set by the OFF path
        layoutSuppressed_ = false;
        vietnameseMode_ = modeBeforeCjk_;
        NotifyModeChange();
    }
    cachedIsCompatLayout_ = true;

    bool native = isCjk ? QueryCjkSubmodeIsNative(fg) : false;
    if (native != submodeSuppressed_) {
        submodeSuppressed_ = native;
        if (submodeSuppressed_ && engine_->Count() > 0) {
            CommitComposition();  // commit pending Vietnamese before silent bypass
        }
        HOOK_LOG(L"  CJK sub-mode: %s (silent)", native ? L"native" : L"English");
    }
}
```

## `QueryCjkSubmodeIsNative()`

```cpp
static bool QueryCjkSubmodeIsNative(HWND fg) {
    HIMC himc = ImmGetContext(fg);
    if (!himc) return true;  // fail-closed: treat as native, suppress
    DWORD conv = 0, sent = 0;
    BOOL ok = ImmGetConversionStatus(himc, &conv, &sent);
    ImmReleaseContext(fg, himc);
    if (!ok) return true;
    return (conv & IME_CMODE_NATIVE) != 0;
}
```

## `ProcessKeyDown` integration

Add one early-return check alongside the existing `vietnameseMode_ / layoutSuppressed_` guards:

```cpp
if (submodeSuppressed_) {
    return;  // silent passthrough, let CJK TIP or host handle the key
}
```

Placement: after the existing `layoutSuppressed_` check. Same key-forwarding
semantics (passthrough, no engine state mutation).

---

## Stability analysis

| Risk | Assessment | Mitigation |
|---|---|---|
| `ImmGetConversionStatus` SendMessage-hang if foreground app is stuck | **Low** — API reads HIMC struct directly in user32, no cross-thread message send | No worker thread needed |
| `ImmGetContext` returns NULL (console, UWP, desktop) | Common | Fail-closed → native → suppress (safer default) |
| Third-party TIP doesn't bridge IMM32 | Possible (older Sogou) | Fail-closed. User can disable toggle |
| HKL change races with `ImmGetContext` between lines | 1–2 µs window | Next 200 ms tick self-corrects |
| Beep spam on rapid Shift-toggling in Chinese IME | **Design-prevented** | Sub-mode path is silent — no beep, no mode switch, no tray update |

**Performance:** 3 Win32 calls × 5 Hz = negligible. Not measurable against the
existing timer work.

---

## Testing plan

**Unit:**
- Mock `ImmGetContext` / `ImmGetConversionStatus` — verify fail-closed on NULL
  context and on `BOOL ok = FALSE`.
- Verify state machine: toggle OFF ↔ toggle ON transitions while on CJK layout
  restore `vietnameseMode_` correctly.

**Manual:**
- Install MS Pinyin (simplified Chinese) — Shift toggles Chinese/English sub-mode:
  - Chinese sub-mode → NexusKey silent bypass, no tray change, no beep
  - English sub-mode → NexusKey processes Vietnamese normally
- Install MS-IME Japanese — Caps Lock / Alt+~ toggles hiragana vs "A" mode:
  - Same expectations
- Install MS-IME Korean — Han/Eng key toggles:
  - Same expectations

**Regression (toggle OFF = default):**
- Verify current CJK suppression behavior byte-identical. Beep still fires on
  HKL change. V → E auto-switch + restore still work.

**Edge cases:**
- Toggle the feature on while currently on CJK + native sub-mode — cleanup
  restores `vietnameseMode_` correctly.
- Toggle off while in English sub-mode of CJK TIP — re-enter the HKL-level
  suppress path and beep once.

---

## Non-goals

- Realtime sub-mode detection (< 50 ms). 200 ms is the accepted bound.
- Detecting internal sub-modes of CJK TIPs beyond native/alphanumeric
  (hiragana vs katakana, simplified vs traditional Chinese, etc.) — not useful
  for Vietnamese suppression decisions.
- Fixing third-party TIPs that ignore IMM32 bridge. Best-effort only.
