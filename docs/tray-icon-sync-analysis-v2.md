# NexusKey Tray Icon Sync — Analysis v2 (for Senior Review)

## 1. GOAL

Synchronize the NexusKey tray icon (V/E) with the actual active keyboard layout:

| Case | Trigger | Expected | Current Status |
|------|---------|----------|----------------|
| 1. Hotkey | User presses Ctrl+Shift | KL switches + icon updates | **WORKS** |
| 2. Icon click | User left-clicks tray icon | KL switches + icon updates | **BROKEN** (icon changes, KL doesn't) |
| 3. KL picker → English | User selects English from language bar | icon → E | **WORKS** |
| 4. KL picker → Vietnamese | User selects NexusKey from language bar | icon → V | **BROKEN** (icon stays E) |

## 2. ARCHITECTURE

```
┌──────────────────────────────────────────────────────────┐
│  NexusKey.exe (main process)                             │
│                                                          │
│  TrayIcon (message-only HWND: "NexusKeyTrayClass")       │
│    ├─ Shows V (Vietnamese) or E (English) icon           │
│    ├─ Handles WM_MODE_CHANGED from TSF DLL               │
│    ├─ Left-click → ToggleMode (soft toggle + KL attempt) │
│    └─ Polling timer (300ms) reads SharedState as backup   │
│                                                          │
│  SharedState (memory-mapped file, seqlock IPC)           │
│    └─ SharedFlags::VIETNAMESE_MODE (bit 0)               │
│    └─ EXE: Create() → FILE_MAP_ALL_ACCESS, isOwner=true │
│    └─ DLL: Open()   → FILE_MAP_READ,       isOwner=false│
│                                                          │
│  HotkeyManager (optional, only if custom hotkey)         │
│    └─ WH_KEYBOARD_LL hook → posts WM_HOTKEY              │
└──────────────────────────────────────────────────────────┘
        ▲                              ▲
        │ WM_MODE_CHANGED              │ SharedState READ-ONLY
        │ (PostMessage cross-process)  │ (DLL can only read)
┌───────┴──────────────────────────────┴───────────────────┐
│  NextKeyTSF.dll (loaded into every app process by TSF)   │
│                                                          │
│  TextService::Activate()                                 │
│    → engineController_->SetVietnameseMode(true)          │
│       → sharedState_.Write(VIETNAMESE_MODE=1)  ← NO-OP! │
│       → PostMessageW(hwndTray, WM_MODE_CHANGED, 1, 0)   │
│                                                          │
│  TextService::Deactivate()                               │
│    → engineController_->SetVietnameseMode(false)         │
│       → sharedState_.Write(VIETNAMESE_MODE=0)  ← NO-OP! │
│       → PostMessageW(hwndTray, WM_MODE_CHANGED, 0, 0)   │
│                                                          │
│  WantKey() reads SharedState flags directly (zero-copy)  │
└──────────────────────────────────────────────────────────┘
```

## 3. CRITICAL BUG FOUND: SharedState is READ-ONLY on TSF DLL side

### The Bug

`SharedStateManager::Open()` (used by TSF DLL) opens shared memory with **read-only** access:

```cpp
// SharedStateManager.cpp:77-81
pImpl_->hMapping = OpenFileMappingW(
    FILE_MAP_READ,     // ← READ-ONLY
    FALSE,
    SHARED_MEM_NAME
);

// SharedStateManager.cpp:87-88
pImpl_->pState = static_cast<volatile SharedState*>(
    MapViewOfFile(pImpl_->hMapping, FILE_MAP_READ, 0, 0, sizeof(SharedState))
    //                              ^^^^^^^^^^^^^^^^ READ-ONLY
);
```

And `Write()` has an owner guard:

```cpp
// SharedStateManager.cpp:136
void SharedStateManager::Write(const SharedState& state) noexcept {
    if (!pImpl_->pState || !pImpl_->isOwner) {
        return;  // ← SILENTLY RETURNS for TSF DLL (isOwner=false)
    }
    // ... write logic never reached
}
```

### Impact

`EngineController::SetVietnameseMode()` calls `sharedState_.Write(state)` — this is a **silent no-op** on the TSF DLL side. The TSF DLL can NEVER write to SharedState.

This means:
- **Polling timer is useless**: It reads SharedState, but TSF DLL never writes to it, so no changes are ever detected.
- **The ONLY notification mechanism is `PostMessageW(WM_MODE_CHANGED)`** from the TSF DLL.
- The soft toggle from EXE (icon click) writes SharedState successfully (EXE is owner), and `WantKey()` reads it correctly — this is why icon click soft toggle "works" for changing typing behavior.

## 4. CURRENT TEST RESULTS — DETAILED ANALYSIS

### Case 1: Hotkey (Ctrl+Shift) — WORKS

**Flow:**
1. User presses Ctrl+Shift
2. Windows detects layout hotkey → switches keyboard layout
3. TSF calls `Deactivate()` on NexusKey → `SetVietnameseMode(false)` → `PostMessageW(WM_MODE_CHANGED, 0)` → icon=E
4. TSF calls `Activate()` on new layout (if switching TO NexusKey) → `SetVietnameseMode(true)` → `PostMessageW(WM_MODE_CHANGED, 1)` → icon=V

**Why it works:** PostMessage from TSF DLL arrives at EXE. Both Activate and Deactivate fire correctly for hotkey-triggered layout switches.

### Case 2: Icon Click — PARTIALLY WORKS

**Current flow (after latest changes):**
1. Left-click → `ToggleMode` handler
2. Soft toggle: `SharedState.flags ^= VIETNAMESE_MODE` → icon updates immediately ✓
3. `SwitchForegroundLayout()` → `PostMessageW(target, WM_INPUTLANGCHANGEREQUEST, 0, nextHkl)`

**What works:** Icon changes (soft toggle). `WantKey()` reads SharedState and stops/starts processing keys.

**What doesn't work:** Windows language bar doesn't change.

**Possible reasons `WM_INPUTLANGCHANGEREQUEST` fails:**
1. **TIP pseudo-HKL may not be supported** by `WM_INPUTLANGCHANGEREQUEST` / `ActivateKeyboardLayout`. These APIs were designed for traditional keyboard layouts, not TSF TIPs.
2. **`GetKeyboardLayoutList` returns layouts for the EXE process**, not the target. The EXE may have different layouts loaded than the foreground app.
3. **`GetKeyboardLayout(tid)` cross-process** — may not return the correct HKL when called from a different process.
4. **The target window may reject** `WM_INPUTLANGCHANGEREQUEST` (handle it and return 0 instead of passing to DefWindowProc).

### Case 3: KL Picker → English — WORKS

**Flow:**
1. User selects "ENG US" from language bar
2. TSF calls `Deactivate()` on NexusKey TextService
3. `SetVietnameseMode(false)` → SharedState write (no-op) → `PostMessageW(WM_MODE_CHANGED, 0)`
4. EXE receives `WM_MODE_CHANGED(0)` → `SetVietnameseMode(false)` → icon=E ✓

**Why it works:** `PostMessageW(WM_MODE_CHANGED, 0)` from `Deactivate()` reaches the EXE.

### Case 4: KL Picker → Vietnamese — BROKEN

**Expected flow:**
1. User selects "NexusKey" from language bar
2. TSF calls `Activate()` on NexusKey TextService
3. `SetVietnameseMode(true)` → SharedState write (no-op) → `PostMessageW(WM_MODE_CHANGED, 1)`
4. EXE receives `WM_MODE_CHANGED(1)` → icon=V

**What actually happens:** Icon stays at E.

**Key observation: PostMessage works for Deactivate (Case 3) but NOT for Activate (Case 4), when triggered by KL picker. However, PostMessage works for BOTH Activate and Deactivate when triggered by hotkey (Case 1).**

## 5. HYPOTHESES FOR CASE 4 FAILURE

### Hypothesis A: TSF doesn't call `Activate()` for KL picker → NexusKey

TSF may use a different activation mechanism for KL picker vs hotkey:
- **Hotkey**: `ITfTextInputProcessor::Activate()` called
- **KL picker**: Might use `ITfTextInputProcessorEx::ActivateEx()` instead, which our TextService doesn't implement

If TSF calls `ActivateEx` and our TextService doesn't implement `ITfTextInputProcessorEx`, TSF might fall back to `Activate()` or might skip activation entirely. **We never verified whether `Activate()` is actually called in the KL picker case.**

### Hypothesis B: `FindWindowExW(HWND_MESSAGE, ...)` fails in KL picker context

When switching via KL picker:
- The language bar UI runs in `ctfmon.exe` or `explorer.exe`
- The Activate might execute in a different security context or desktop where `FindWindowExW` can't find the message-only window

### Hypothesis C: PostMessage is blocked by UIPI in KL picker context

`ChangeWindowMessageFilterEx` allows `WM_MODE_CHANGED` to be received from any process. But:
- For hotkey: the TSF DLL runs in the foreground app (same IL as EXE)
- For KL picker: the TSF DLL might run in a SYSTEM process (like `ctfmon.exe`) that has a different IL or session

### Hypothesis D: Deactivate(old) + Activate(new) ordering/timing issue

For KL picker, the sequence might be:
1. `Activate(NexusKey)` called FIRST (in new process/thread)
2. `Deactivate(NexusKey)` called SECOND (in old process/thread — NO, this doesn't make sense for the same thread)

Or they might happen in different threads, and message ordering is not guaranteed across threads/processes.

### Hypothesis E: TextService object lifetime

TSF might reuse the same TextService object across KL picker switches. If `Deactivate()` was called but `Activate()` is called on a stale object, `engineController_` might be null.

Looking at the code:
```cpp
// Activate():
engineController_ = std::make_unique<EngineController>();  // Creates new

// Deactivate():
engineController_.reset();  // Destroys
```

If Activate is called without prior Deactivate (re-activation), the old EngineController is replaced. If Deactivate is called without subsequent Activate, engineController_ is null. Both cases are handled.

## 6. CURRENT CODE — EXACT STATE

### `TextService::Activate()` (TSF DLL)
```cpp
IFACEMETHODIMP TextService::Activate(ITfThreadMgr* pThreadMgr, TfClientId tfClientId) {
    pThreadMgr_ = pThreadMgr;
    pThreadMgr_->AddRef();
    clientId_ = tfClientId;

    CoCreateInstance(CLSID_TF_CategoryMgr, ...);

    engineController_ = std::make_unique<EngineController>();
    engineController_->SetClientId(tfClientId);
    engineController_->SetCategoryMgr(pCategoryMgr_);

    keyEventSink_ = std::make_unique<KeyEventSink>(this, engineController_.get());
    keyEventSink_->Advise(pThreadMgr);

    // Always force Vietnamese mode on activation
    engineController_->SetVietnameseMode(true);
    // ↑ SharedState write is NO-OP (DLL is read-only)
    // ↑ PostMessage WM_MODE_CHANGED(1) is the only notification

    return S_OK;
}
```

### `TextService::Deactivate()` (TSF DLL)
```cpp
IFACEMETHODIMP TextService::Deactivate() {
    if (engineController_) {
        engineController_->SetVietnameseMode(false);
        // ↑ SharedState write is NO-OP
        // ↑ PostMessage WM_MODE_CHANGED(0) is the only notification
    }
    keyEventSink_->Unadvise(); keyEventSink_.reset();
    engineController_.reset();
    SafeRelease(pCategoryMgr_); SafeRelease(pThreadMgr_);
    return S_OK;
}
```

### `EngineController::SetVietnameseMode()` (TSF DLL)
```cpp
void EngineController::SetVietnameseMode(bool mode) {
    vietnameseMode_ = mode;

    // Write to SharedState — SILENT NO-OP (isOwner=false, FILE_MAP_READ)
    if (sharedState_.IsConnected()) {
        SharedState state = sharedState_.Read();
        if (state.IsValid()) {
            if (mode) state.flags |= SharedFlags::VIETNAMESE_MODE;
            else state.flags &= ~SharedFlags::VIETNAMESE_MODE;
            sharedState_.Write(state);  // ← Does nothing
        }
    }

    // Notify EXE — THIS IS THE ONLY WORKING NOTIFICATION
    HWND hwndTray = FindWindowExW(HWND_MESSAGE, nullptr, L"NexusKeyTrayClass", nullptr);
    if (hwndTray) {
        PostMessageW(hwndTray, WM_MODE_CHANGED, mode ? 1 : 0, 0);
    }
}
```

### `TrayIcon::ProcessMessage()` (EXE)
```cpp
if (msg == WM_MODE_CHANGED) {
    bool vietnamese = (wParam != 0);
    SetVietnameseMode(vietnamese);  // Updates icon
    if (modeChangeCallback_) modeChangeCallback_(vietnamese);  // Just logs
    return true;
}
```

### `OnMenuCommand(ToggleMode)` (EXE — icon left-click)
```cpp
case TrayMenuId::ToggleMode: {
    // Soft toggle — writes SharedState (EXE is owner, this WORKS)
    SharedState state = g_sharedState.Read();
    if (state.IsValid()) {
        state.flags ^= SharedFlags::VIETNAMESE_MODE;
        g_sharedState.Write(state);  // ← WORKS (EXE is owner)
        g_trayIcon.SetVietnameseMode((state.flags & SharedFlags::VIETNAMESE_MODE) != 0);
    }
    // Best-effort KL sync via WM_INPUTLANGCHANGEREQUEST
    SwitchForegroundLayout();  // ← Probably doesn't work for TIPs
    break;
}
```

### `SwitchForegroundLayout()` (EXE)
```cpp
void SwitchForegroundLayout() {
    HWND target = g_lastForegroundHwnd;
    if (!target || !IsWindow(target)) target = GetForegroundWindow();
    if (!target) return;

    DWORD tid = GetWindowThreadProcessId(target, nullptr);
    HKL currentHkl = GetKeyboardLayout(tid);

    // Cycle through keyboard layouts
    int count = GetKeyboardLayoutList(0, nullptr);
    if (count < 2) return;
    HKL layouts[16];
    count = GetKeyboardLayoutList(min(count, 16), layouts);
    HKL nextHkl = layouts[0];
    for (int i = 0; i < count; i++) {
        if (layouts[i] == currentHkl) {
            nextHkl = layouts[(i + 1) % count];
            break;
        }
    }

    PostMessageW(target, WM_INPUTLANGCHANGEREQUEST, 0, (LPARAM)nextHkl);
}
```

### `SharedStateSyncProc()` (EXE — polling timer, currently USELESS)
```cpp
static void CALLBACK SharedStateSyncProc(HWND, UINT, UINT_PTR, DWORD) {
    SharedState state = g_sharedState.Read();
    if (!state.IsValid()) return;
    bool vietnamese = (state.flags & SharedFlags::VIETNAMESE_MODE) != 0;
    if (vietnamese != g_trayIcon.IsVietnameseMode()) {
        g_trayIcon.SetVietnameseMode(vietnamese);
        // ← NEVER FIRES because TSF DLL can't write SharedState
    }
}
```

### SharedState access model
```cpp
// EXE (SharedStateManager::Create):
//   OpenFileMapping → FILE_MAP_ALL_ACCESS, isOwner=true
//   Write() → WORKS

// TSF DLL (SharedStateManager::Open):
//   OpenFileMapping → FILE_MAP_READ, isOwner=false
//   Write() → { if (!isOwner) return; } → SILENT NO-OP
```

## 7. WHAT WAS TRIED (complete history)

| # | Approach | Result | Root cause of failure |
|---|----------|--------|----------------------|
| 1 | WM_MODE_CHANGED from TSF Activate/Deactivate | Partial: hotkey works, KL picker→Vi doesn't | PostMessage doesn't arrive for KL picker Activate (unknown why) |
| 2 | Polling timer with IsNexusKeyHkl() heuristic | Didn't work | HKL heuristic wrong + race conditions |
| 3 | RegisterShellHookWindow + HSHELL_LANGUAGE | Didn't work | HSHELL_LANGUAGE doesn't fire for same-LANGID TIP switches (confirmed by senior) |
| 4 | Fix stale SharedState in Activate (if/else) | Fixed one bug, KL picker still broken | Fixed the wrong-mode bug, but PostMessage still doesn't arrive for KL picker→Vi |
| 5 | Force SetVietnameseMode(true) in Activate | KL picker still broken | SharedState write is no-op (READ-ONLY), PostMessage still doesn't arrive |
| 6 | Polling SharedState as backup | Useless | TSF DLL can't write SharedState (READ-ONLY, isOwner=false) |
| 7 | WM_INPUTLANGCHANGEREQUEST for icon click | Didn't work | Probably doesn't work for TIP pseudo-HKLs |
| 8 | SendInput(Ctrl+Shift) for icon click | Didn't work | SetForegroundWindow fails from message-only window |

## 8. THINGS NEVER VERIFIED (need logging)

1. **Does `TextService::Activate()` fire when switching TO NexusKey via KL picker?** This is the #1 unknown. Add `TSF_LOG` at the top of Activate and check.

2. **Does `FindWindowExW(HWND_MESSAGE, nullptr, "NexusKeyTrayClass", nullptr)` succeed in the Activate call?** Log the returned HWND.

3. **Does `PostMessageW(hwndTray, WM_MODE_CHANGED, 1, 0)` return TRUE?** Log the return value.

4. **Does `WM_MODE_CHANGED(1)` actually arrive at the EXE's message loop?** Add logging in ProcessMessage for ALL messages to check.

5. **In which process does Activate run for KL picker switches?** The DLL should load in the foreground app, but maybe KL picker triggers it in ctfmon.exe.

## 9. PROPOSED FIXES

### Fix A: Give TSF DLL write access to SharedState (enables polling fix)

Change `Open()` to `FILE_MAP_ALL_ACCESS` and remove/relax the `isOwner` check in `Write()`.

```cpp
// SharedStateManager::Open() — change:
FILE_MAP_READ  →  FILE_MAP_ALL_ACCESS

// SharedStateManager::Write() — change:
if (!pImpl_->pState || !pImpl_->isOwner) return;
// to:
if (!pImpl_->pState) return;
```

**Pro:** Polling timer becomes functional. TSF DLL can write VIETNAMESE_MODE. Even if PostMessage fails, the EXE detects the change within 300ms.

**Con:** Breaks single-writer seqlock guarantee. Two processes can now write. Risk: corrupted reads if EXE and DLL write simultaneously. Mitigation: VIETNAMESE_MODE writes are triggered by user actions (not continuous), so simultaneous writes are extremely unlikely.

**Security note:** Shared memory needs matching security descriptors. `Create()` uses default DACL which may prevent the DLL (in another process) from opening with write access. May need explicit `SECURITY_ATTRIBUTES` with a permissive DACL.

### Fix B: ITfActiveLanguageProfileNotifySink in EXE (recommended by senior)

Register a COM sink in the EXE process to receive TSF profile change notifications.

```cpp
// In EXE, after CoInitialize:
ITfThreadMgr* pThreadMgr;
CoCreateInstance(CLSID_TF_ThreadMgr, NULL, CLSCTX_INPROC_SERVER,
                 IID_ITfThreadMgr, (void**)&pThreadMgr);
pThreadMgr->Activate(&clientId);

ITfSource* pSource;
pThreadMgr->QueryInterface(IID_ITfSource, (void**)&pSource);
pSource->AdviseSink(IID_ITfActiveLanguageProfileNotifySink, pSink, &cookie);
```

The sink's `OnActivated()` callback provides:
- `clsid` — the CLSID of the activated TIP (can check against our CLSID)
- `guidProfile` — the profile GUID
- `fActivated` — whether activated or deactivated

**Pro:** Most reliable. Designed for exactly this purpose. Works for all switching methods (hotkey, KL picker, API calls).

**Con:** Requires COM setup in EXE. The EXE must have a message pump on the COM apartment thread (already has one). Need to implement the COM interface.

### Fix C: ITfInputProcessorProfiles::ActivateLanguageProfile for icon click

Instead of `WM_INPUTLANGCHANGEREQUEST` or `SendInput`, use the proper TSF API:

```cpp
ITfInputProcessorProfiles* pProfiles;
CoCreateInstance(CLSID_TF_InputProcessorProfiles, NULL, CLSCTX_INPROC_SERVER,
                 IID_ITfInputProcessorProfiles, (void**)&pProfiles);

// To switch to NexusKey:
pProfiles->ActivateLanguageProfile(CLSID_TextService, langid, guidProfile);

// To switch to default keyboard:
pProfiles->ActivateLanguageProfile(CLSID_NULL, langid, GUID_NULL);
```

**Pro:** Correct API for switching TIPs. Works for TIP pseudo-HKLs.

**Con:** Only affects the calling thread. To switch another app's layout, need to run in that app's thread (not possible from EXE). For the foreground app, could use `AttachThreadInput` trick, but this is fragile.

### Fix D: Minimal — just make SharedState writable (Fix A) as quick fix

If full COM implementation (Fix B) is too much work right now:

1. Apply Fix A (SharedState writable from DLL)
2. The 300ms polling timer becomes functional
3. Icon syncs for ALL cases within 300ms
4. Icon click soft toggle works for typing behavior
5. KL sync for icon click remains broken (low priority — can use keyboard or KL picker)

## 10. QUESTIONS FOR SENIOR

1. **Why does PostMessage(WM_MODE_CHANGED, 1) work for hotkey-triggered Activate but NOT for KL picker-triggered Activate?** Is there a different TSF lifecycle for KL picker? Does `Activate()` even get called?

2. **Is giving TSF DLL write access to SharedState (Fix A) safe enough?** The seqlock loses its single-writer guarantee, but concurrent writes from different user actions are practically impossible. Is a named mutex overkill?

3. **For the icon click KL switch, is there a way to switch another process's TIP from the EXE?** `ActivateLanguageProfile` only works on the calling thread. `WM_INPUTLANGCHANGEREQUEST` doesn't work for TIPs. `SendInput` requires foreground permission. Is there a cross-process TIP switching API?

4. **Is ITfActiveLanguageProfileNotifySink (Fix B) the recommended path forward?** It would make the EXE self-sufficient for detecting layout changes, eliminating the dependency on PostMessage from the DLL.

5. **Should the EXE create its own `ITfThreadMgr` for receiving notifications?** The EXE is a tray app with a message loop — can it host a TSF thread manager just for monitoring?
