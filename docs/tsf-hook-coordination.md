# TSF + Hook Engine Coordination

> **Purpose:** Define how the TSF DLL and Hook Engine coexist without double-processing keystrokes.
> **Date:** 2026-03-07

---

## Problem

When the TSF DLL is registered, Windows loads it into **every process** with text input. The Hook Engine also runs globally. Without coordination, both engines process the same keystrokes — causing duplicated or corrupted Vietnamese output.

## Design: Mutual Exclusion via `TSF_ACTIVE` Flag

One bit in SharedState (`SharedFlags::TSF_ACTIVE = 0x0008`) controls which engine is active for the foreground app.

```
SharedState.flags
  bit 0: VIETNAMESE_MODE   (V/E toggle)
  bit 1: ENGINE_ENABLED    (EXE alive)
  bit 2: SPELL_CHECK
  bit 3: TSF_ACTIVE        ← NEW: foreground app uses TSF
```

### Why One Flag Works

Only **one app receives keystrokes at a time** — the foreground app. The EXE detects foreground changes and sets the flag before the first keystroke arrives in the new app.

---

## Data Flow

```
User switches to Notepad (in TSF apps list)
    │
    ▼
EXE: WinEventHook (EVENT_SYSTEM_FOREGROUND)
    │
    ▼
HookEngine::OnFocusChanged()
    ├── currentExe_ = "notepad.exe"
    ├── isTsfApp_ = tsfAppSet_.count("notepad.exe") → true
    ├── Hook: passes through all keys (isTsfApp_ check in ProcessKeyDown)
    └── tsfActiveCallback_(true)
            │
            ▼
        g_sharedState.SetOrClearFlag(TSF_ACTIVE, true)
            │
            ▼
DLL: EngineController::WantKey()
    ├── reads SharedState.flags (atomic, zero-copy)
    ├── TSF_ACTIVE set → processes Vietnamese input
    └── logs: [Checkpoint] TSF_ACTIVE: ON


User switches to Chrome (NOT in TSF apps list)
    │
    ▼
EXE: WinEventHook (EVENT_SYSTEM_FOREGROUND)
    │
    ▼
HookEngine::OnFocusChanged()
    ├── currentExe_ = "chrome.exe"
    ├── isTsfApp_ = false
    ├── Hook: processes keys normally
    └── tsfActiveCallback_(false)
            │
            ▼
        g_sharedState.SetOrClearFlag(TSF_ACTIVE, false)
            │
            ▼
DLL: EngineController::WantKey()
    ├── TSF_ACTIVE not set → return false (passthrough)
    └── logs: [Checkpoint] TSF_ACTIVE: OFF
```

## Invariant

**Never both active for the same app:**

| Foreground App | Hook Engine | TSF DLL | TSF_ACTIVE |
|---|---|---|---|
| In TSF list | Passthrough | Processes keys | `true` |
| Not in TSF list | Processes keys | Passthrough | `false` |
| Excluded app | Forces English | Passthrough | `false` |
| EXE not running | Dead | Passthrough (no SharedState) | N/A |
| TSF-only mode (no hook) | Not running | Always processes | `true` (set at init) |

---

## Two Build Modes

| Mode | Define | Hook | DLL | TSF_ACTIVE init |
|---|---|---|---|---|
| Hook + TSF hybrid | `NEXUSKEY_HOOK_ENGINE` | Runs | Loaded by Windows | `false` (HookEngine sets per-app) |
| TSF-only | (default) | Not running | Only engine | `true` (set at SharedState init) |

**Critical**: In TSF-only mode, `TSF_ACTIVE` must be set at init. Otherwise the DLL never processes keys.

---

## Failure Modes

| Scenario | Behavior | Risk |
|---|---|---|
| Rapid app switching | Flag flips each switch, correct engine handles keys | None |
| EXE crashes | Flag stuck at last value. Hook also dead, DLL handles everything — better than nothing | Low |
| SharedState unavailable | DLL returns false in WantKey (English passthrough) | None |
| Game fullscreen | WinEventHook fires normally | None |

## Debugging Checkpoints

When investigating crashes or freezes related to TSF/Hook coordination:

1. **DLL log**: Look for `[Checkpoint] TSF_ACTIVE: ON/OFF` — shows when DLL starts/stops processing
2. **Hook log**: Look for `TSF_ACTIVE flag: true/false` — shows when EXE flips the flag
3. **SharedState**: Read `flags` field — bit 3 shows current TSF_ACTIVE state

If logs stop at a checkpoint, the atomic read/write on SharedState is the suspect.

## Files Changed

| File | Change |
|---|---|
| `src/core/ipc/SharedState.h` | Added `TSF_ACTIVE = 0x0008` to SharedFlags |
| `src/app/system/HookEngine.h` | Added `tsfActiveCallback_` + setter |
| `src/app/system/HookEngine.cpp` | Call callback in `OnFocusChanged` on state change |
| `src/app/main.cpp` | Wire callback to `g_sharedState.SetOrClearFlag()` |
| `src/tsf/EngineController.h` | Added `tsfActive_` member for transition tracking |
| `src/tsf/EngineController.cpp` | Check `TSF_ACTIVE` in `WantKey`, log transitions |
| `src/app/dialogs/SettingsDialog.cpp` | Sync tsf-apps toggle with `IsTsfRegistered()` on load |

## Related

- [Architecture.md](./Architecture.md) — SharedState design, "Signals Not Data" principle
- `docs/CODING_RULES.md` — Naming, error handling conventions
