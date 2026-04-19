# TSF Readonly Context Mode — Phase 1 (Auto-Cap)

**Date:** 2026-04-19
**Status:** Design approved, ready for implementation
**Scope:** Phase 1 of 3 — auto-capitalize only

---

## Problem

HookEngine's existing auto-cap (HookEngine.cpp:877-889) is keystroke-based — it tracks `. ? !` + SPACE transitions internally. This misses cases TSF's `ShouldAutoCapitalize` (EngineController.cpp:143) handles via document read:

1. Doc start (app just opened).
2. Paste preceding text: `"Hello. "` + type → Hook state=0, should be cap.
3. Click after `". "` → Hook state=0, should be cap.
4. `"." + multiple spaces/tabs` → Hook only sees first SPACE.
5. Standalone `\n` (no punct before) → Hook only catches its own VK_RETURN.
6. Cross-app switch with cursor mid-document.

TSF handles these because it reads preceding chars via `ITfRange`. Goal: let Hook consume TSF's document knowledge when Hook is the active engine.

---

## Overview

Currently `TSF_ACTIVE` flag gives mutual exclusion (TSF or Hook, not both). This phase adds a **readonly context mode** for TSF — it sinks document events and pushes an anchor to SharedState, without consuming keystrokes. Hook reads the anchor to enhance auto-cap to TSF-level quality.

Phase 1 ships auto-cap only. Phase 2 (cross-boundary tone) and phase 3 (word continuation) use the same anchor infrastructure.

---

## State Machine

New bit in `SharedFlags`:

```cpp
TSF_ACTIVE   = 0x0008   // full TIP (consume keys)          — existing
TSF_READONLY = 0x0010   // NEW: sink events, push anchor    — no consume
```

`HookEngine::OnFocusChanged` sets flags:

| Foreground app                  | TSF_ACTIVE | TSF_READONLY | Hook action      |
|---------------------------------|:---:|:---:|------------------|
| In TSF list                     | 1   | 0   | Passthrough      |
| Not in list, not excluded       | 0   | 1   | Process + anchor |
| Excluded                        | 0   | 0   | Force English    |
| EXE not running / no SharedState | 0  | 0   | TSF passthrough  |

**Invariant:** `TSF_ACTIVE` and `TSF_READONLY` never both set. EXE is sole writer.

### Activation toggle

No new checkbox. The existing "Register TSF DLL" setting is single source of truth. If TSF broken in some app, user unregisters entirely — readonly context and full TIP share TSF lifetime.

---

## Anchor Struct + Seqlock

```cpp
// src/core/ipc/SharedState.h
struct HookContextAnchor {
    std::atomic<uint32_t> generation;   // seqlock counter
    bool     isAvailable;               // false = password/console/read fail
    bool     isSentenceStart;           // doc start or after . ? !
    bool     isLineStart;               // after \n or doc start
    bool     isWordStart;               // preceding char is whitespace/empty
    uint8_t  syllableLen;               // 0..16
    wchar_t  currentSyllable[16];       // non-whitespace run before cursor
};
static_assert(sizeof(HookContextAnchor) == 44, "ABI frozen");
```

`currentSyllable` unused in phase 1; shipped now to freeze ABI for phase 2.

### Seqlock protocol

**Writer (TSF) in `OnEndEdit`:**
```cpp
uint32_t g = anchor.generation.fetch_add(1, release);  // odd: writing
anchor.isSentenceStart = ...;
anchor.isLineStart     = ...;
// ...
anchor.generation.store(g + 2, release);               // even: stable
```

**Reader (Hook) in `ProcessKeyDown`:**
```cpp
for (int retry = 0; retry < 3; ++retry) {
    uint32_t g1 = anchor.generation.load(acquire);
    if (g1 & 1) continue;                     // TSF writing, retry
    HookContextAnchor snap = anchor;
    uint32_t g2 = anchor.generation.load(acquire);
    if (g1 == g2) { use(snap); break; }
}
// 3 failed retries → treat as isAvailable=false (fallback)
```

3 retries sufficient: TSF writer is ~50ns (3 bool copies + array copy). Triple collision is effectively impossible.

### Invalidation

Hook tracks `lastSeenGen_`. Each key:
- `snap.generation != lastSeenGen_` → context changed → Hook clears any per-session state, updates `lastSeenGen_`.
- `snap.generation == lastSeenGen_` → no doc change since last check → keep state.

Phase 1 "state" is minimal (just `autoCapState_`); phase 2/3 will expand.

---

## TSF Side — `ReadonlyContextProvider`

New class, separate from `EngineController` (different concern: read-only, no composition).

```
src/tsf/ReadonlyContextProvider.cpp/h   // NEW
  class ContextProvider : ITfTextEditSink, ITfThreadMgrEventSink
    ├─ OnActivate(ITfThreadMgr*, TfClientId)
    ├─ OnDeactivate()
    ├─ OnSetFocus(ITfDocumentMgr* new, ITfDocumentMgr* prev)
    ├─ OnEndEdit(ITfContext*, TfEditCookie, ITfEditRecord*)
    └─ UpdateAnchor(ITfContext*, TfEditCookie)  // scan + push
```

### Lifecycle

`TextService::ActivateEx`:
1. Read `TSF_READONLY` flag. If unset → skip instantiation (save per-process overhead for apps running full TSF).
2. If set → instantiate provider, advise `ITfThreadMgrEventSink`.
3. In `OnSetFocus`, advise `ITfTextEditSink` on the new `ITfContext`, unadvise from the old one.

`TextService::Deactivate` → unadvise all sinks.

### Scan algorithm in `UpdateAnchor`

1. **Password check** — reuse existing `InputScopeChecker`. Password → `isAvailable=false`, bump generation, return.
2. Request sync read lock (`TF_ES_READ | TF_ES_SYNC`). On fail → request async; meanwhile stale anchor stays readable (safe).
3. Clone selection range, `ShiftStart(-64, ...)`, `GetText` → wchar buffer of preceding chars.
4. Derive flags (scan backward):
   - `isWordStart` = buffer empty OR last char is whitespace
   - `isLineStart` = skip trailing spaces/tabs, hit `\n` OR buffer empty
   - `isSentenceStart` = skip trailing spaces/tabs, hit `. ? !` OR buffer empty
5. Collect `currentSyllable`: scan back through non-whitespace, up to 16 chars.
6. Write via seqlock pattern above.

### Performance

OnEndEdit fires ~each keystroke. Scan 64 chars + SharedState write ≈ 10-50µs. At 10 keys/sec = 0.05% CPU per process. Negligible. No throttling needed phase 1.

TODO (logging): currently no user-facing log infra. When added, instrument read failures + slow reads.

---

## Hook Side — Auto-Cap Integration

Modify existing auto-cap block in HookEngine.cpp:1162-1166. No new function — replace truth source:

```cpp
// Before:
if (autoCaps_ && autoCapState_ == 2 && engine_->Count() == 0) {
    ch = towupper(ch);
    autoCapped = (ch != originalCh);
    autoCapState_ = 0;
}

// After:
if (autoCaps_ && engine_->Count() == 0) {
    bool shouldCap = (autoCapState_ == 2);   // keystroke fallback

    HookContextAnchor snap;
    if (ReadAnchorSeqlock(snap) && snap.isAvailable) {
        // Doc truth overrides keystroke state.
        shouldCap = snap.isSentenceStart || snap.isLineStart;
        lastSeenGen_ = snap.generation;
    }

    if (shouldCap) {
        ch = towupper(ch);
        autoCapped = (ch != originalCh);
    }
    autoCapState_ = 0;
}
```

### Conditions

| Condition              | Phase 1 |
|------------------------|:---:|
| Vietnamese mode ON     | Yes |
| English mode           | No (defer to Windows/app auto-cap) |
| Excluded app           | No (Hook doesn't process) |
| `TSF_READONLY` = 1     | Enables anchor read path |
| Password field         | No (`isAvailable` = false → fallback) |

### Fallback

- Anchor available → doc truth (matches TSF behavior).
- Anchor unavailable (TSF unregistered / password / read failed / TSF-only mode running) → keystroke state machine (existing behavior, no regression).

Keystroke state machine stays wired in parallel. Cheap; not disabled.

### Races (tolerated)

- Hook types "a" at sentence start → reads old anchor (sentenceStart=true) → commits "A" → OnEndEdit → anchor flips to false → next key lowercase. ✓
- App that eats input (no text shown) → anchor never updates → Hook may cap subsequent letters wrongly. Such apps are unusable with Hook regardless — not a regression.
- Paste / click followed by key within ~5ms (edit session latency) → first letter may miss cap. Acceptable.

---

## Files Changed

| File | Change |
|---|---|
| `src/core/ipc/SharedState.h` | `TSF_READONLY` flag; `HookContextAnchor` struct; follow 7-step struct versioning (docs/CODING_RULES/5-struct-versioning.md) |
| `src/core/ipc/SharedStateManager.cpp` | Initialize anchor; expose seqlock read/write helpers |
| `src/tsf/ReadonlyContextProvider.cpp/h` | **NEW** — sinks + scan + push |
| `src/tsf/TextService.cpp` | Wire provider in `ActivateEx` / `Deactivate`, gated on `TSF_READONLY` |
| `src/app/system/HookEngine.cpp` | `OnFocusChanged`: set `TSF_READONLY` for non-TSF apps. Auto-cap path: read anchor, override truth when available |
| `tests/HookContextAnchorTest.cpp` | **NEW** |

---

## Testing

### Unit tests (Linux)

Extract scan into pure free function `DeriveAnchorFlags(std::wstring_view preceding)` for testability.

**Seqlock invariants:**
- Writer odd→even transition.
- Reader retries on torn read.
- Concurrent stress (2 writers, 8 readers, std::thread) — no torn snapshots.

**Scan logic:**
| Input | sentenceStart | lineStart | wordStart |
|---|:---:|:---:|:---:|
| `""` | true | true | true |
| `"hello. "` | true | false | true |
| `"hello.\t\t "` | true | false | true |
| `"hello\n"` | false | true | true |
| `"hello"` | false | false | false |
| `"hello,\n"` | false | true | true |
| `"a b"` | false | false | false |

**ABI:** `static_assert(sizeof(HookContextAnchor) == 44)`.

### Manual Windows tests

| App | Case | Expected |
|---|---|---|
| Notepad | Empty doc + type "abc" | "Abc" (doc start) |
| Notepad | Type "x. abc" | "X. Abc" |
| Notepad | Paste "Hello. " + type "abc" | "Hello. Abc" ← phase 1 win |
| Notepad | Click after ". " + type "abc" | "Abc" ← phase 1 win |
| Notepad | Type "x.\t\tabc" | "X.\t\tAbc" |
| Notepad | Type "line1\nabc" | "line1\nAbc" |
| Chrome password field | Type "abc" | "abc" (`isAvailable=false`) |
| CMD | Type "abc" | "abc" (no TSF context) |
| Word | Various auto-cap cases | Document Word's own auto-cap interaction |

---

## Rollout — Three Separate Commits

1. **Infra** — SharedState struct, seqlock helpers, unit tests. Zero runtime effect. Safe to merge.
2. **TSF provider** — `ReadonlyContextProvider` + wire-up. TSF pushes anchor; Hook doesn't read yet. Dogfood 3-7 days to verify no TSF full mode regression and monitor for crashes.
3. **Hook integration** — anchor read in auto-cap path. Feature go-live.

Each commit independently revertable. If commit 3 regresses → revert only commit 3; infrastructure remains for phase 2.

---

## Known Limitations (Ship Notes)

- Console apps (CMD, PowerShell native) lack TSF → fallback to keystroke state machine.
- Some Electron apps report flaky TSF — validate during manual tests, may need per-app blocklist later.
- Short race (< 5ms, one edit session) after paste or click — first letter may miss cap.
- App that accepts keystrokes but shows no text — anchor never updates → possible false caps. Hook generally unusable in such apps regardless.

---

## Future Phases (Out of Scope)

**Phase 2** — cross-boundary tone. Hook reads `anchor.currentSyllable`, treats as prefix when its internal buffer is empty; tone key modifies via engine, commits via backspace + new chars. Uses existing anchor infra.

**Phase 3** — word continuation. User clicks mid-word, Hook loads partial word from anchor into engine state and continues building.

Both phases invasive to Hook commit path — separated for isolation and bisectability.
