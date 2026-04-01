---
type: bmad-distillate
sources:
  - "tray-icon-sync-analysis-v2.md"
downstream_consumer: "NexusKey debugging — tray icon and SharedState synchronization issues"
created: "2026-04-01"
token_estimate: 1350
parts: 1
---

## Current Status
- Case 1 (Hotkey Ctrl+Shift): WORKS — TSF Deactivate+Activate fire correctly; PostMessage reaches EXE
- Case 2 (Icon click): PARTIAL — soft toggle updates icon+SharedState; `WM_INPUTLANGCHANGEREQUEST` fails to change Windows language bar
- Case 3 (KL picker → English): WORKS — `Deactivate()` fires; `PostMessage(WM_MODE_CHANGED, 0)` reaches EXE
- Case 4 (KL picker → Vietnamese): BROKEN — icon stays E; `PostMessage(WM_MODE_CHANGED, 1)` from `Activate()` never arrives at EXE

## Critical Bug: SharedState READ-ONLY from TSF DLL
- `SharedStateManager::Open()` uses `FILE_MAP_READ`; `Write()` has `if (!isOwner) return;` guard → DLL writes are silent no-ops
- `EngineController::SetVietnameseMode()` calls `sharedState_.Write(state)` — does nothing on DLL side
- Polling timer (`SharedStateSyncProc`, 300ms) reads SharedState but DLL never writes → timer is useless
- Only working notification: `PostMessageW(hwndTray, WM_MODE_CHANGED, wParam, 0)` from DLL
- EXE writes work (owner=true); `WantKey()` reads correctly — soft toggle works for typing behavior

## Architecture
- EXE: `TrayIcon` (message-only HWND class `"NexusKeyTrayClass"`); `SharedStateManager::Create()` → `FILE_MAP_ALL_ACCESS`, `isOwner=true`
- DLL: `TextService::Activate()` creates `EngineController`; `Deactivate()` resets it; `SharedStateManager::Open()` → `FILE_MAP_READ`, `isOwner=false`
- `SharedFlags::VIETNAMESE_MODE` = bit 0; seqlock IPC
- DLL notifies EXE via `FindWindowExW(HWND_MESSAGE, nullptr, L"NexusKeyTrayClass", nullptr)` + `PostMessageW(WM_MODE_CHANGED)`

## Hypotheses for Case 4 Failure
- **H-A**: TSF may call `ITfTextInputProcessorEx::ActivateEx()` for KL picker (not `Activate()`); if unimplemented, activation may be skipped entirely
- **H-B**: `FindWindowExW` may fail in KL picker context (language bar runs in `ctfmon.exe`/`explorer.exe`, different security context)
- **H-C**: UIPI may block `PostMessage` when DLL runs in SYSTEM process (`ctfmon.exe`) despite `ChangeWindowMessageFilterEx`
- **H-D**: Deactivate+Activate ordering/timing across threads/processes; message ordering not guaranteed cross-process
- **H-E**: TextService object lifetime — ruled out; `Activate()` creates new `EngineController`, `Deactivate()` resets; re-activation handled

## Unverified Items (Need Logging)
1. Does `TextService::Activate()` fire at all for KL picker → NexusKey?
2. Does `FindWindowExW` succeed in that `Activate()` call? (log returned HWND)
3. Does `PostMessageW` return TRUE?
4. Does `WM_MODE_CHANGED(1)` arrive at EXE message loop? (log ALL messages)
5. Which process hosts `Activate()` for KL picker? (expect foreground app, might be `ctfmon.exe`)

## Failed Approaches (Complete History)
1. `WM_MODE_CHANGED` from TSF Activate/Deactivate — partial; KL picker→Vi PostMessage doesn't arrive
2. Polling timer + `IsNexusKeyHkl()` heuristic — wrong HKL heuristic + race conditions
3. `RegisterShellHookWindow` + `HSHELL_LANGUAGE` — doesn't fire for same-LANGID TIP switches
4. Fix stale SharedState if/else in Activate — fixed wrong-mode bug but PostMessage still missing
5. Force `SetVietnameseMode(true)` in Activate — SharedState write no-op; PostMessage still fails
6. Polling SharedState as backup — useless; DLL can't write
7. `WM_INPUTLANGCHANGEREQUEST` for icon click — doesn't work for TIP pseudo-HKLs
8. `SendInput(Ctrl+Shift)` for icon click — `SetForegroundWindow` fails from message-only window

## Proposed Fixes
- **Fix A (SharedState writable)**: Change `Open()` to `FILE_MAP_ALL_ACCESS`; remove `isOwner` guard in `Write()`. Pro: polling timer works; icon syncs within 300ms. Con: breaks single-writer seqlock; concurrent writes possible but unlikely (user-triggered). Security: default DACL may block DLL write access; may need explicit permissive `SECURITY_ATTRIBUTES`.
- **Fix B (ITfActiveLanguageProfileNotifySink in EXE)**: Register COM sink via `ITfSource::AdviseSink(IID_ITfActiveLanguageProfileNotifySink)`; `OnActivated()` provides `clsid`, `guidProfile`, `fActivated`. Pro: most reliable; works for all switching methods. Con: requires COM setup + `ITfThreadMgr::Activate()` in EXE; EXE already has message pump.
- **Fix C (ITfInputProcessorProfiles::ActivateLanguageProfile)**: Use `ActivateLanguageProfile(CLSID_TextService, langid, guidProfile)` for icon click KL switch. Con: only affects calling thread; cross-process requires `AttachThreadInput` (fragile).
- **Fix D (Minimal quick fix)**: Apply Fix A only; polling timer becomes functional; icon syncs all cases within 300ms; icon click KL switch remains broken (low priority).

## Key Code Patterns
- `TextService::Activate()`: creates `EngineController`, calls `SetVietnameseMode(true)` — SharedState write no-op, PostMessage is only notification
- `TextService::Deactivate()`: calls `SetVietnameseMode(false)` then resets `engineController_`
- `EngineController::SetVietnameseMode()`: reads SharedState, modifies flags, writes (no-op); then `FindWindowExW` + `PostMessageW`
- `TrayIcon::ProcessMessage()`: on `WM_MODE_CHANGED` → `SetVietnameseMode(wParam != 0)` + callback
- `OnMenuCommand(ToggleMode)`: XOR `VIETNAMESE_MODE` in SharedState (works, EXE is owner); calls `SwitchForegroundLayout()`
- `SwitchForegroundLayout()`: gets foreground HWND tid → `GetKeyboardLayout` → cycles `GetKeyboardLayoutList` → `PostMessageW(WM_INPUTLANGCHANGEREQUEST)`
- `SharedStateSyncProc()`: 300ms timer; reads SharedState; compares with icon state — never fires because DLL can't write

## Open Questions
1. Why does PostMessage work for hotkey Activate but not KL picker Activate? Different TSF lifecycle?
2. Is giving DLL write access safe enough? Named mutex overkill?
3. Cross-process TIP switching API for icon click? (`ActivateLanguageProfile` is thread-local; `WM_INPUTLANGCHANGEREQUEST` fails for TIPs; `SendInput` needs foreground)
4. Is `ITfActiveLanguageProfileNotifySink` (Fix B) the recommended path?
5. Can EXE create its own `ITfThreadMgr` just for monitoring?
