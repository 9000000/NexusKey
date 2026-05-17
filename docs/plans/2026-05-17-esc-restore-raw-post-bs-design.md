# ESC Restore Raw — Post-BS (Hook + TSF) — Design

**Date**: 2026-05-17
**Author**: brainstorm session (PhatMT + AI assistant)
**Status**: Design approved, ready for implementation plan
**Related**: `docs/plans/2026-05-13-esc-restore-raw-design.md` (original feature)

---

## 1. Problem

Sau khi feature ESC-restore-raw (`escRestoreRawEnabled`) đã ship, scenario sau **không hoạt động**:

```
user gõ:  v i r u s            → screen: "víu"   engine.Count()=3
user gõ:  Space                → screen: "víu " engine.Count()=0, commit-undo Ready
user gõ:  Backspace            → screen: "víu"  engine.Count()=0, commit-undo Primed
user gõ:  ESC                  → screen: "víu"  (KHÔNG đổi — bug)
        kỳ vọng:                 screen: "virus"
```

Root cause: gate ESC restore-raw yêu cầu `engine_->Count() > 0` (Hook `HookEngine.cpp:1280`, TSF `KeyEventSink.cpp:181,286`). Sau commit + BS, engine đã `Reset()` → `escRawHistory_.clear()` → gate fail. ESC pass-through, raw bị mất.

Hành vi giống Unikey / EVKey / GoTiengViet: ESC **luôn** lấy lại raw của từ vừa commit gần nhất.

## 2. Behavior contract

**Trigger window**: ESC (down, không modifier) trong vòng **1500ms** (`kCommitUndoTimeoutMs` hiện có) kể từ commit trigger.

**Kích hoạt khi cả 4 đúng**:
1. `escRestoreRawEnabled == true`
2. Vietnamese mode active
3. Engine buffer **empty** (`engine_->Count() == 0`)
4. State machine ở `Primed` (Hook) hoặc tương đương ở TSF, với entry hợp lệ trong cache/stack

**Hành động**: Xóa display text của từ vừa commit → inject raw → reset commit-undo state → eat ESC.

**KHÔNG kích hoạt khi**:
- State ở `Ready` (no BS sau commit) — pass-through (xem §9 out of scope)
- Stack/cache trống hoặc hết timeout
- Modifier held (Ctrl/Alt/Win)
- Tempt-off-macro-esc đang chạy (`tempOffMacroEsc` branch — orthogonal feature, không bị ảnh hưởng vì branch đó gate ở engine empty + macro state khác)

**Edge cases**:
- Single-word scope. Multi-word ESC walkback **out of scope** (xem §9).
- Sau ESC restore-raw, state về Idle, stack/cache clear.
- Cursor movement bất kỳ (mouse click, arrow, focus change) → state Idle, ESC pass-through.

---

## 3. Architecture — Hook engine

### 3.1 Data — `CommitEntry` (`src/app/system/HookEngine.h:439`)

Thêm 1 field `rawInput`:

```cpp
struct CommitEntry {
    std::vector<wchar_t> history;     // Replay-driven, unchanged
    std::wstring text;                // Display text khi commit
    std::wstring rawInput;            // engine_->PeekRaw() snapshot — for Esc-restore-raw post-BS
    std::vector<uint8_t> widths;
    uint8_t extraLeadingTriggers = 0;
};
```

**Why field, not derived**: engine `escRawHistory_` bị clear trong `engine_->Reset()` (line `TypingEngine.cpp:1553`). Nếu derive từ `history` thì phải mô phỏng lại logic BS-marker (history chứa `kBackspaceMarker`) — duplicate engine logic. Snapshot 1 string ngay tại commit-time đơn giản & chính xác hơn.

### 3.2 Snapshot order — `CommitComposition()` (`HookEngine.cpp:1843`)

`engine_->Commit()` reset engine, **mất** `escRawHistory_`. Phải peek trước:

```cpp
bool HookEngine::CommitComposition() {
    bool wasQuickConsonant = engine_->HasActiveQuickConsonant();
    std::wstring rawSnapshot = engine_->PeekRaw();   // BEFORE Commit() — engine.Reset() inside Commit() clears escRawHistory_
    std::wstring committed = engine_->Commit();
    // ... existing flow ...
    if (!restored && !wasQuickConsonant && !inputHistory_.empty()) {
        CommitEntry entry;
        entry.history = inputHistory_;
        entry.text = previousComposition_;
        entry.rawInput = std::move(rawSnapshot);     // NEW
        entry.widths = previousEncodedWidths_;
        entry.extraLeadingTriggers = leadingTriggersForCurrentWord_;
        // ... unchanged ...
    }
}
```

### 3.3 Gate — `HookEngine.cpp:1278`

```cpp
const bool hasComposition = engine_->Count() > 0;
const bool hasPrimedCommit = (commitUndoState_ == CommitUndoState::Primed)
                             && !commitStack_.empty()
                             && !commitStack_.back().rawInput.empty();
if (escRestoreRaw && vkCode == VK_ESCAPE
    && !cachedCtrl && !cachedAlt && !cachedWin
    && (hasComposition || hasPrimedCommit)) {
    return TryEscRestoreRaw();
}
```

### 3.4 `TryEscRestoreRaw()` (`HookEngine.cpp:3424`) — hai nhánh

```cpp
HookEngine::KeyOutcome HookEngine::TryEscRestoreRaw() {
    auto inj = injector_.load(std::memory_order_acquire);

    // Path 1: live composition (existing behavior, unchanged).
    if (engine_->Count() > 0) {
        const std::wstring raw = engine_->PeekRaw();
        if (raw.empty()) return KeyOutcome::Fallthrough;
        if (!inj->Replace(engine_->Count(), std::wstring_view(raw))) {
            return KeyOutcome::Fallthrough;
        }
        engine_->Reset();
        rawMacroBuffer_.clear();
        tempMacroOff_ = false;
        return KeyOutcome::Eat;
    }

    // Path 2: post-BS — engine reset, raw snapshot in commitStack top.
    // Gate caller already verified Primed + non-empty stack + non-empty rawInput.
    if (GetTickCount() - commitReadyTime_ > kCommitUndoTimeoutMs) {
        CancelCommitUndo();
        return KeyOutcome::Fallthrough;
    }
    const auto& top = commitStack_.back();
    const size_t bsCount = top.text.size();   // Primed: trailing trigger already deleted by user's BS
    if (!inj->Replace(bsCount, std::wstring_view(top.rawInput))) {
        return KeyOutcome::Fallthrough;
    }
    CancelCommitUndo();   // Clears commitStack_ + state Idle — single-word scope
    rawMacroBuffer_.clear();
    tempMacroOff_ = false;
    return KeyOutcome::Eat;
}
```

**Why `CancelCommitUndo()` (not pop-only)**: User scope = single-word. Sau restore raw cho `víu→virus`, nếu giữ entries cũ trong stack thì BS kế tiếp lại walk back tới word trước → confusing UX. Clear hết → trở về Idle như fresh state.

---

## 4. Architecture — TSF DLL (parity)

TSF không có sẵn commit-undo state machine (`KeyEventSink.cpp` xử lý BS như phím raw). Phải dựng phiên bản lite trong `EngineController`.

### 4.1 Data — `EngineController.h`

```cpp
enum class CommitUndoState : uint8_t { Idle, Ready, Primed };

struct LastCommit {
    std::wstring text;          // What was written to document
    std::wstring rawInput;      // engine_->PeekRaw() snapshot
    bool hasTrailingChar = false;  // True for CommitWithChar (space appended), false for plain Commit
    DWORD timestamp = 0;        // GetTickCount() at commit
};

// In EngineController:
LastCommit lastCommit_;
CommitUndoState commitUndoState_ = CommitUndoState::Idle;

void RecordCommitSnapshot(const std::wstring& text, const std::wstring& raw, bool hasTrailing);
void OnNonRestoreKey();   // Any key that breaks the undo window → state Idle
bool TryRestoreLastCommitRaw(ITfContext* pContext);   // ESC handler
bool IsCommitUndoPrimed() const noexcept { return commitUndoState_ == CommitUndoState::Primed; }
```

### 4.2 Snapshot — `Commit` / `CommitWithChar` (`EngineController.cpp:401,418`)

Trước khi `engine_->Commit()` reset engine:

```cpp
void EngineController::Commit(ITfContext* pContext) {
    std::wstring rawSnapshot = engine_->PeekRaw();
    std::wstring committed = engine_->Commit();
    // ... existing edit session ...
    RecordCommitSnapshot(committed, rawSnapshot, /*hasTrailing=*/false);
}

void EngineController::CommitWithChar(ITfContext* pContext, wchar_t appendChar) {
    std::wstring rawSnapshot = engine_->PeekRaw();
    std::wstring committed = engine_->Commit();
    // ... existing edit session (appends appendChar) ...
    RecordCommitSnapshot(committed, rawSnapshot, /*hasTrailing=*/true);
}

void EngineController::RecordCommitSnapshot(const std::wstring& text,
                                            const std::wstring& raw,
                                            bool hasTrailing) {
    if (raw.empty() || text.empty()) {
        commitUndoState_ = CommitUndoState::Idle;
        lastCommit_ = {};
        return;
    }
    lastCommit_.text = text;
    lastCommit_.rawInput = raw;
    lastCommit_.hasTrailingChar = hasTrailing;
    lastCommit_.timestamp = GetTickCount();
    commitUndoState_ = hasTrailing ? CommitUndoState::Ready : CommitUndoState::Idle;
    // Plain Commit (no trailing char) → no Ready state; the only way to enter Primed is via CommitWithChar
}
```

**Lý do bỏ Ready cho plain Commit**: Plain `Commit()` chỉ xảy ra với phím "non-handled" như Enter, arrow, F-key (`KeyEventSink.cpp:243`). Sau Enter/arrow, cursor đã di chuyển sang context khác → BS không liên quan đến text vừa commit nữa.

### 4.3 BS detection — `KeyEventSink.cpp` `OnKeyDown` VK_BACK

Trước `BackspaceRevive` branch (line 227 hiện tại):

```cpp
if (vk == VK_BACK && !pEngineController_->HasEngineBuffer()) {
    // Commit-undo BS detection: only valid in Ready (CommitWithChar window).
    if (pEngineController_->IsCommitUndoReady()
        && pEngineController_->WithinUndoWindow()) {
        pEngineController_->TransitionToPrimed();
        // Let BS pass through to delete the trailing char in the document.
        *pfEaten = FALSE;
        return S_OK;
    }
    // Second BS in Primed → cancel (user deleting our committed text, not undoing tone).
    if (pEngineController_->IsCommitUndoPrimed()) {
        pEngineController_->ResetCommitUndo();
        // fall through to BackspaceRevive
    }
}
```

`WithinUndoWindow()` = `GetTickCount() - lastCommit_.timestamp < kCommitUndoTimeoutMs`.

**Why không verify cursor position**: Edit session để query selection sẽ tốn 1-2ms + đụng spec "no doc mutation in test phase" cho phía OnTestKeyDown. Tradeoff: nếu user click chuột sang chỗ khác rồi BS, ta sai-detect Primed → ESC kế tiếp có thể xóa nhầm text. Mitigation: §4.5 invalidation.

### 4.4 ESC handler — `KeyEventSink.cpp:179,284`

Mở rộng gate ở cả `OnTestKeyDown` (line 179) và `OnKeyDown` (line 284):

```cpp
if (vk == VK_ESCAPE && pEngineController_->IsEscRestoreRawEnabled()) {
    if (pEngineController_->HasEngineBuffer()) {
        // Live composition path — existing CommitRawAndEnd
        if (pEngineController_->CommitRawAndEnd(pContext)) {
            *pfEaten = TRUE;
            return S_OK;
        }
    } else if (pEngineController_->IsCommitUndoPrimed()
               && pEngineController_->WithinUndoWindow()) {
        // Post-BS path — restore from lastCommit_
        if (pEngineController_->TryRestoreLastCommitRaw(pContext)) {
            *pfEaten = TRUE;
            return S_OK;
        }
    }
}
```

`TryRestoreLastCommitRaw(pContext)`:
1. Mở edit session SYNC READWRITE.
2. `pContext->GetSelection(ec, ...)` lấy current range.
3. `range->ShiftStart(ec, -static_cast<LONG>(lastCommit_.text.size()), ...)` mở rộng range về sau N chars.
4. `range->SetText(ec, 0, lastCommit_.rawInput.data(), lastCommit_.rawInput.size())` thay thế.
5. Reset `commitUndoState_ = Idle`, `lastCommit_ = {}`.
6. Return true.
7. Nếu bất kỳ HRESULT FAILED → log, return false, caller fallback pass-through.

### 4.5 State invalidation

Bất kỳ key khác BS/ESC trong Ready/Primed → `commitUndoState_ = Idle`:

Trong `OnTestKeyDown` ngay sau modifier guard, trước các branch khác:
```cpp
if (commitUndoState_ != CommitUndoState::Idle
    && vk != VK_BACK && vk != VK_ESCAPE) {
    pEngineController_->OnNonRestoreKey();   // → Idle
}
```

Cursor di chuyển ngoài keystroke (mouse, OnSelectionChange): khoanh-vùng đơn giản bằng cách reset state trong `OnSetFocus`. Mouse click giữa keystrokes là edge case; per `kCommitUndoTimeoutMs` 1500ms timeout, nếu user click rồi 1.5s sau mới ESC thì state đã expired.

---

## 5. Performance gates (per Rule 11)

| Pillar | Impact Hook | Impact TSF |
|---|---|---|
| **Nhanh** | +1 atomic compare ở gate khi ESC. Allocation chỉ khi Primed thực fire. | +1 timestamp compare khi BS/ESC empty. SnapShot string copy mỗi Commit (đã có raw từ engine — chỉ thêm 1 move). |
| **Nhẹ** | +`std::wstring` mỗi CommitEntry (~24B sso, ≤72B cho stack=3). | +1 struct `LastCommit` (~80B static trong EngineController). |
| **Mượt** | Dùng `IOutputInjector` plugin layer có sẵn. | TSF edit session SYNC — pattern đã prove ở `CommitRawAndEnd` hiện tại. |

**Code Governance Q1–Q5** (per `docs/CODE_GOVERNANCE.md`):
- **Q1 Layer**: Hook chỉnh trong `HookEngine` (đúng); TSF chỉnh trong `EngineController` + `KeyEventSink` (đúng — text mutation qua edit session).
- **Q2 Perf**: Hot path cost = 1 atomic load (escRestoreRawEnabled_) + 1 state compare. Active path chỉ chạy khi user thực bấm ESC.
- **Q3 Native**: Không API mới, dùng `IOutputInjector::Replace` (Hook) + `ITfContext::GetSelection` + `ITfRange::ShiftStart`/`SetText` (TSF spec-compliant).
- **Q4 No-lock**: Atomic config; commitUndoState_ là single-threaded (TSF thread cho TSF; hook thread cho Hook).
- **Q5 Trade-off**: 1 wstring/CommitEntry + 1 LastCommit struct vs feature parity với Unikey/EVKey — chi phí thấp, value cao cho user.

---

## 6. Test plan

**Test-first** (per `project_test_first` memory): viết test FAIL trước khi sửa.

### 6.1 Engine layer (`tests/TelexEngineTest.cpp`)

Đã có 7 EscRestoreRaw tests cho live-composition path (PeekRaw). Bổ sung 1 test cover snapshot order:
- `EscRestoreRaw_PeekRawBeforeCommitReset` — PushChar("virus"), PeekRaw → "virus", Commit() → reset, PeekRaw → "". Verify snapshot phải lấy trước Commit().

### 6.2 Hook layer (`tests/HookEngineAtomicTests.cpp` hoặc test mới)

```
HookEscRestoreRaw_PostBS_Primed_RestoresRaw
  - Setup: drive ProcessKeyDown qua sequence "virus space backspace"
  - Verify: commitUndoState_ == Primed, commitStack_.back().rawInput == "virus"
  - Action: ProcessKeyDown(VK_ESCAPE)
  - Assert: injector mock thấy Replace(3, "virus"), commitStack_ empty, state Idle

HookEscRestoreRaw_PostBS_StackEmpty_Passthrough
  - Engine empty, stack empty → ESC pass-through (KeyOutcome::Fallthrough)

HookEscRestoreRaw_PostBS_ReadyState_Passthrough
  - "virus space" rồi ESC ngay (chưa BS) → Ready state, gate fail, pass-through (per scope out)

HookEscRestoreRaw_PostBS_EmptyRawInput_Passthrough
  - Force commitStack_ entry với rawInput rỗng (English word path?) → ESC pass-through

HookEscRestoreRaw_PostBS_TimeoutExpired_Passthrough
  - Drive Primed, advance time > kCommitUndoTimeoutMs, ESC → pass-through
  - Implementation note: `commitReadyTime_` set ở `SetCommitUndoReady` và KHÔNG reset khi Ready→Primed transition (HookEngine.cpp:1013). Vì vậy timestamp đo từ Ready start, áp dụng cho cả Primed window. Thêm check thời gian trong `TryEscRestoreRaw` path 2: `if (GetTickCount() - commitReadyTime_ > kCommitUndoTimeoutMs) { CancelCommitUndo(); return KeyOutcome::Fallthrough; }`

HookEscRestoreRaw_PostBS_Snapshot_PreservesCase
  - "VIRUS" → BS → ESC → injected raw == "VIRUS"
```

### 6.3 TSF layer

TSF tests chạy trên Windows (DLL), không có harness Linux. Manual smoke test:
- Bật toggle, restart, set TSF mode active.
- Notepad / Word / Chrome: gõ `virus` → space → BS → ESC → verify "virus" hiện trên màn.
- Negative: gõ `virus` → space → bấm vào màn khác → BS → ESC: state phải Idle, ESC pass-through (no mangling).
- Negative: gõ `virus` → space → đợi 2s → BS → ESC: state expired, ESC pass-through.

### 6.4 Regression

- `chaos.toml` đầy đủ trên 5 host baseline. Không được giảm pass rate.
- 7 EscRestoreRaw existing tests vẫn pass.
- Commit-undo replay (`HandleAlphaKey` post-BS replay) không đổi behavior — verify multi-word backward vẫn hoạt động.

---

## 7. File checklist

| File | Change |
|---|---|
| `src/app/system/HookEngine.h` | `CommitEntry::rawInput` field |
| `src/app/system/HookEngine.cpp` | `CommitComposition` snapshot trước Commit; gate mở rộng `:1278`; `TryEscRestoreRaw` path 2; auto-expire Primed nếu thiếu |
| `src/tsf/EngineController.h` | `LastCommit`, `CommitUndoState`, methods `RecordCommitSnapshot`/`OnNonRestoreKey`/`TryRestoreLastCommitRaw`/`IsCommitUndo*`/`WithinUndoWindow` |
| `src/tsf/EngineController.cpp` | Snapshot trong `Commit`/`CommitWithChar`; state machine impl; edit session `EscRestoreLastCommitSession` |
| `src/tsf/KeyEventSink.cpp` | BS detection branch; ESC gate mở rộng (cả OnTestKeyDown lẫn OnKeyDown); non-restore key invalidation; `OnSetFocus` reset |
| `tests/TelexEngineTest.cpp` | +1 snapshot order test |
| `tests/HookEngineAtomicTests.cpp` | +6 post-BS tests |
| `tests/TestHelper.h` | (Nếu cần) helper drive HookEngine tới Primed state với mock injector |

---

## 8. Why-comments (per Rule 9.10.2)

Chỉ thêm comment tại 4 chỗ non-obvious:

1. **`HookEngine.cpp` `CommitComposition` snapshot**: `// PeekRaw BEFORE Commit() — engine_->Commit() calls Reset() which clears escRawHistory_`
2. **`HookEngine.cpp` `TryEscRestoreRaw` path 2**: `// Engine empty here; rawInput preserved in commitStack_ from CommitComposition snapshot. CancelCommitUndo clears stack (single-word scope per design 2026-05-17).`
3. **`EngineController.cpp` `Commit` plain (no trailing)**: `// No Ready transition for plain Commit — only CommitWithChar (space appended) enters the undo window. Enter/arrow/F-key route via Commit and break the window naturally.`
4. **`KeyEventSink.cpp` BS detection**: `// No cursor verification — would cost an edit session in OnKeyDown. Mitigated by short timeout (kCommitUndoTimeoutMs) + invalidation on OnSetFocus + any-other-key transition to Idle.`

---

## 9. Out of scope

- **Ready-state ESC** (`virus space ESC` không có BS): hành vi ambiguous (giữ space hay xóa?), user không yêu cầu. Gate hiện tại fail naturally, ESC pass-through như hành vi cũ.
- **Multi-word ESC walkback** (`a b c BS BS BS ESC` walk qua nhiều words): commitStack_ chỉ pop top, ESC chỉ restore word gần nhất. User confirmed: không cần.
- **Cursor-precise verification trong TSF BS**: edit session query selection mỗi BS sẽ cost 1-2ms hot path. Trade-off: chấp nhận false-positive Primed nếu user click chuột giữa space và BS — mitigated bằng 1500ms timeout + focus reset.
- **VK_BACK arriving qua synthetic events**: nếu một synthetic BS từ injector vào Primed window thì sẽ false-trigger restore. Mitigation: VKEY_EXTRA_INFO check trong hook giải quyết Hook side; TSF không thấy injected events.
- **English word commits**: `restored == true` path trong `CommitComposition` (line 1856) skip `commitStack_.push_back`. Đúng — English bypass không cần undo.
