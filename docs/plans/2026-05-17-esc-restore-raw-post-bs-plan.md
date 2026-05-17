# ESC Restore Raw — Post-BS (Hook + TSF) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Cho phép ESC khôi phục raw keys (`víu → virus`) sau khi user đã commit + backspace, không chỉ khi đang gõ giữa chừng. Áp dụng cho cả Hook engine và TSF DLL.

**Architecture:** Hook engine extends `CommitEntry` với `rawInput` snapshot lấy trước `engine_->Commit()`; `TryEscRestoreRaw` có thêm path đọc từ `commitStack_` khi engine empty + state Primed. TSF DLL dựng commit-undo state machine lite trong `EngineController` (cache `LastCommit` + state Ready/Primed), BS detection trong `KeyEventSink::OnKeyDown`, ESC restore qua edit session mới `EscRestoreLastCommitSession`.

**Tech Stack:** C++20, GTest (Linux test target), TSF (`ITfContext`/`ITfRange::ShiftStart`/`SetText`), `IOutputInjector` plugin layer, `std::atomic` for config bools.

**Spec:** `docs/plans/2026-05-17-esc-restore-raw-post-bs-design.md`

**Coding rules:** `docs/CODING_RULES/` — Rule 9 (naming), Rule 11 (hot path), Rule 4 (interface). 7-step struct versioning NOT applicable (no SharedState change).

---

## File Structure

**Hook layer changes (Windows-only build):**
- Modify `src/app/system/HookEngine.h` — `CommitEntry::rawInput` field
- Modify `src/app/system/HookEngine.cpp` — snapshot in `CommitComposition`, gate at line ~1278, `TryEscRestoreRaw` path 2

**TSF layer changes (Windows-only build):**
- Modify `src/tsf/EngineController.h` — `LastCommit`, `CommitUndoState`, methods
- Modify `src/tsf/EngineController.cpp` — snapshot in `Commit`/`CommitWithChar`, state machine impl, helpers
- Create `src/tsf/EscRestoreLastCommitSession.h` (header-only edit session) — restore raw via `ITfRange::SetText`
- Modify `src/tsf/KeyEventSink.cpp` — BS detection, ESC branch extension, non-restore-key invalidation, focus reset

**Tests (Linux build, GTest):**
- Modify `tests/TelexEngineTest.cpp` — +1 test for `PeekRaw` snapshot order

**Manual smoke (Windows):** §4 of this plan.

---

## Phase 1 — Engine snapshot-order safety

### Task 1.1: Verify-test for `PeekRaw` before `Commit`

**Files:**
- Modify: `tests/TelexEngineTest.cpp`

**Why this task:** Documenting the contract that `engine_->PeekRaw()` MUST be called before `engine_->Commit()` because `Commit()` internally calls `Reset()` which clears `escRawHistory_` (verified in `TypingEngine.cpp:1495,1504,1539,1546` → `Reset()` at line 1550 → `escRawHistory_.clear()` at line 1553).

- [ ] **Step 1: Add failing test**

Find the existing `TEST(TelexEngineTest, EscRestoreRaw_BasicVirus)` block in `tests/TelexEngineTest.cpp`. Append immediately after it:

```cpp
TEST(TelexEngineTest, EscRestoreRaw_PeekRawClearedByCommit) {
    NextKey::TypingConfig cfg;
    cfg.inputMethod = NextKey::InputMethod::Telex;
    cfg.escRestoreRawEnabled = true;
    NextKey::TypingEngine engine(cfg);

    for (wchar_t c : std::wstring(L"virus")) engine.PushChar(c);
    ASSERT_EQ(engine.PeekRaw(), L"virus")
        << "Sanity: raw is populated while engine has buffer";

    std::wstring committed = engine.Commit();
    EXPECT_FALSE(committed.empty())
        << "Commit() should return non-empty composed text";
    EXPECT_EQ(engine.PeekRaw(), L"")
        << "Commit() must call Reset() which clears escRawHistory_; "
           "callers MUST snapshot PeekRaw() BEFORE Commit().";
    EXPECT_EQ(engine.Count(), 0u);
}
```

- [ ] **Step 2: Run test, verify it passes (sanity — no engine change yet)**

Run:
```bash
cmake --build build-linux --target NextKeyTests && \
  ./build-linux/tests/VKeyTests --gtest_filter="TelexEngineTest.EscRestoreRaw_PeekRawClearedByCommit"
```

Expected: `[  PASSED  ] 1 test.`

- [ ] **Step 3: Commit**

```bash
git add tests/TelexEngineTest.cpp
git commit -m "test(engine): document PeekRaw must precede Commit (contract test)"
```

---

## Phase 2 — Hook engine: ESC restores raw after commit + BS

### Task 2.1: Add `rawInput` field to `CommitEntry`

**Files:**
- Modify: `src/app/system/HookEngine.h:439-444`

- [ ] **Step 1: Add field**

In `src/app/system/HookEngine.h`, find:
```cpp
struct CommitEntry {
    std::vector<wchar_t> history;   // User keystrokes for replay
    std::wstring text;              // What was on screen when committed
    std::vector<uint8_t> widths;    // Encoded widths for non-Unicode code tables
    uint8_t extraLeadingTriggers = 0;  // ...
};
```

Replace with:
```cpp
struct CommitEntry {
    std::vector<wchar_t> history;   // User keystrokes for replay
    std::wstring text;              // What was on screen when committed
    std::wstring rawInput;          // engine_->PeekRaw() snapshot — for Esc-restore-raw post-BS (design 2026-05-17)
    std::vector<uint8_t> widths;    // Encoded widths for non-Unicode code tables
    uint8_t extraLeadingTriggers = 0;  // Extra trigger chars typed between previous commit and this word's body — must be backspaced before this entry's commit trigger can be primed during multi-word undo
};
```

- [ ] **Step 2: Commit**

```bash
git add src/app/system/HookEngine.h
git commit -m "feat(hook): add CommitEntry.rawInput field for Esc-restore-raw post-BS"
```

### Task 2.2: Snapshot raw in `CommitComposition()`

**Files:**
- Modify: `src/app/system/HookEngine.cpp` — `CommitComposition()` starting line 1843

- [ ] **Step 1: Snapshot before `engine_->Commit()`**

Find:
```cpp
bool HookEngine::CommitComposition() {
    HOOK_LOG(L"  CommitComposition (count=%zu, prev='%s')", engine_->Count(), previousComposition_.c_str());

    // Check quick consonant BEFORE Commit() resets the engine.
    // Words ending in active quick consonant (e.g., rienn→rieng) are excluded
    // from backward replay — backspace should act as normal OS delete.
    bool wasQuickConsonant = engine_->HasActiveQuickConsonant();

    std::wstring committed = engine_->Commit();
```

Change to:
```cpp
bool HookEngine::CommitComposition() {
    HOOK_LOG(L"  CommitComposition (count=%zu, prev='%s')", engine_->Count(), previousComposition_.c_str());

    // Check quick consonant BEFORE Commit() resets the engine.
    // Words ending in active quick consonant (e.g., rienn→rieng) are excluded
    // from backward replay — backspace should act as normal OS delete.
    bool wasQuickConsonant = engine_->HasActiveQuickConsonant();

    // PeekRaw BEFORE Commit() — engine_->Commit() calls Reset() which clears
    // escRawHistory_ (see TelexEngineTest.EscRestoreRaw_PeekRawClearedByCommit).
    // Snapshot lives in CommitEntry.rawInput for post-BS ESC restore.
    std::wstring rawSnapshot = engine_->PeekRaw();

    std::wstring committed = engine_->Commit();
```

- [ ] **Step 2: Wire snapshot into `CommitEntry`**

In the same function find:
```cpp
        CommitEntry entry;
        entry.history = inputHistory_;
        entry.text = previousComposition_;
        entry.widths = previousEncodedWidths_;
        entry.extraLeadingTriggers = leadingTriggersForCurrentWord_;
```

Change to:
```cpp
        CommitEntry entry;
        entry.history = inputHistory_;
        entry.text = previousComposition_;
        entry.rawInput = std::move(rawSnapshot);
        entry.widths = previousEncodedWidths_;
        entry.extraLeadingTriggers = leadingTriggersForCurrentWord_;
```

- [ ] **Step 3: Linux build smoke (does it still compile-link the engine subset?)**

Run:
```bash
cmake --build build-linux --target NextKeyTests 2>&1 | tail -5
```

Expected: build succeeds. HookEngine.cpp is Win32-only so it won't be in the Linux target — this step only verifies no header includes broke. If you see `error:` referencing HookEngine, recheck the header changes from Task 2.1.

- [ ] **Step 4: Commit**

```bash
git add src/app/system/HookEngine.cpp
git commit -m "feat(hook): snapshot raw input into CommitEntry before engine reset"
```

### Task 2.3: Extend `TryEscRestoreRaw()` with post-BS path

**Files:**
- Modify: `src/app/system/HookEngine.cpp:3424-3443`

- [ ] **Step 1: Replace `TryEscRestoreRaw` body with two-path version**

Find the existing function:
```cpp
HookEngine::KeyOutcome HookEngine::TryEscRestoreRaw() {
    const size_t composedCount = engine_->Count();
    if (composedCount == 0) return KeyOutcome::Fallthrough;

    const std::wstring raw = engine_->PeekRaw();
    if (raw.empty()) return KeyOutcome::Fallthrough;

    auto inj = injector_.load(std::memory_order_acquire);
    HOOK_LOG(L"  EscRestoreRaw: bs=%zu raw='%ls'", composedCount, raw.c_str());
    if (!inj->Replace(composedCount, std::wstring_view(raw))) {
        HOOK_LOG(L"  EscRestoreRaw: injector reported partial delivery");
        return KeyOutcome::Fallthrough;
    }

    engine_->Reset();
    rawMacroBuffer_.clear();
    tempMacroOff_ = false;
    return KeyOutcome::Eat;
}
```

Replace with:
```cpp
HookEngine::KeyOutcome HookEngine::TryEscRestoreRaw() {
    auto inj = injector_.load(std::memory_order_acquire);

    // Path 1: live composition (existing behavior).
    if (engine_->Count() > 0) {
        const size_t composedCount = engine_->Count();
        const std::wstring raw = engine_->PeekRaw();
        if (raw.empty()) return KeyOutcome::Fallthrough;
        HOOK_LOG(L"  EscRestoreRaw[live]: bs=%zu raw='%ls'", composedCount, raw.c_str());
        if (!inj->Replace(composedCount, std::wstring_view(raw))) {
            HOOK_LOG(L"  EscRestoreRaw[live]: injector reported partial delivery");
            return KeyOutcome::Fallthrough;
        }
        engine_->Reset();
        rawMacroBuffer_.clear();
        tempMacroOff_ = false;
        return KeyOutcome::Eat;
    }

    // Path 2: post-BS (engine empty, raw snapshot in commitStack top).
    // Engine empty here; rawInput preserved in commitStack_ from CommitComposition
    // snapshot. CancelCommitUndo clears stack (single-word scope per design 2026-05-17).
    if (commitUndoState_ != CommitUndoState::Primed || commitStack_.empty()) {
        return KeyOutcome::Fallthrough;
    }
    if (GetTickCount() - commitReadyTime_ > kCommitUndoTimeoutMs) {
        HOOK_LOG(L"  EscRestoreRaw[post-BS]: Primed expired (elapsed > %ums)", kCommitUndoTimeoutMs);
        CancelCommitUndo();
        return KeyOutcome::Fallthrough;
    }
    const auto& top = commitStack_.back();
    if (top.rawInput.empty()) return KeyOutcome::Fallthrough;
    const size_t bsCount = top.text.size();  // Primed: trailing commit-trigger already deleted by user's BS
    HOOK_LOG(L"  EscRestoreRaw[post-BS]: bs=%zu raw='%ls' text='%ls'",
             bsCount, top.rawInput.c_str(), top.text.c_str());
    if (!inj->Replace(bsCount, std::wstring_view(top.rawInput))) {
        HOOK_LOG(L"  EscRestoreRaw[post-BS]: injector reported partial delivery");
        return KeyOutcome::Fallthrough;
    }
    CancelCommitUndo();
    rawMacroBuffer_.clear();
    tempMacroOff_ = false;
    return KeyOutcome::Eat;
}
```

- [ ] **Step 2: Commit**

```bash
git add src/app/system/HookEngine.cpp
git commit -m "feat(hook): TryEscRestoreRaw post-BS path reads commitStack top"
```

### Task 2.4: Extend ESC gate to allow Primed state

**Files:**
- Modify: `src/app/system/HookEngine.cpp` — ESC branch at line ~1278

- [ ] **Step 1: Replace gate**

Find:
```cpp
    // 3b'. Esc-restore-raw: when enabled, bare Esc with active composition
    // injects the user's raw keys (víu → virus) instead of the Vietnamese
    // form, then eats the Esc so the app never sees it. DispatchKeyAction's
    // step 5 modifier guard runs *downstream* of HandlePreDispatch — Ctrl+Esc
    // / Alt+Esc would still reach this branch, so guard modifiers explicitly.
    if (escRestoreRaw && vkCode == VK_ESCAPE
        && !cachedCtrl && !cachedAlt && !cachedWin
        && engine_->Count() > 0) {
        return TryEscRestoreRaw();
    }
```

Replace with:
```cpp
    // 3b'. Esc-restore-raw: when enabled, bare Esc with active composition
    // injects the user's raw keys (víu → virus) instead of the Vietnamese
    // form, then eats the Esc so the app never sees it. DispatchKeyAction's
    // step 5 modifier guard runs *downstream* of HandlePreDispatch — Ctrl+Esc
    // / Alt+Esc would still reach this branch, so guard modifiers explicitly.
    //
    // Post-BS extension (design 2026-05-17): if engine is empty but commit-undo
    // is Primed (user typed space then BS), reuse the snapshot from
    // commitStack_.back().rawInput. TryEscRestoreRaw handles both paths.
    const bool hasLiveComposition = engine_->Count() > 0;
    const bool hasPrimedCommit =
        (commitUndoState_ == CommitUndoState::Primed) &&
        !commitStack_.empty() &&
        !commitStack_.back().rawInput.empty();
    if (escRestoreRaw && vkCode == VK_ESCAPE
        && !cachedCtrl && !cachedAlt && !cachedWin
        && (hasLiveComposition || hasPrimedCommit)) {
        return TryEscRestoreRaw();
    }
```

- [ ] **Step 2: Commit**

```bash
git add src/app/system/HookEngine.cpp
git commit -m "feat(hook): gate ESC restore-raw allows Primed commit-undo state"
```

---

## Phase 3 — TSF DLL: commit-undo cache + ESC restore parity

### Task 3.1: Add state machine + cache to `EngineController.h`

**Files:**
- Modify: `src/tsf/EngineController.h`

- [ ] **Step 1: Add types + members + methods**

In `src/tsf/EngineController.h`, find the existing class body. Add **near the top of the class (after `CommitRawAndEnd` declaration around line 53)**:

```cpp
    /// Commit-undo state machine for ESC-restore-raw post-BS (design 2026-05-17).
    /// Mirrors HookEngine's state machine but lighter — single-entry cache, no replay.
    enum class CommitUndoState : uint8_t {
        Idle   = 0,
        Ready  = 1,  // Just CommitWithChar'd — waiting for first BS
        Primed = 2,  // BS happened in Ready — ESC will now restore from cache
    };

    [[nodiscard]] bool IsCommitUndoReady()  const noexcept { return commitUndoState_ == CommitUndoState::Ready; }
    [[nodiscard]] bool IsCommitUndoPrimed() const noexcept { return commitUndoState_ == CommitUndoState::Primed; }
    [[nodiscard]] bool WithinUndoWindow()   const noexcept;
    void TransitionUndoReadyToPrimed() noexcept;
    void ResetCommitUndo() noexcept;
    void OnNonRestoreKey() noexcept;  // Any key besides BS/ESC in Ready/Primed → Idle
    [[nodiscard]] bool TryRestoreLastCommitRaw(ITfContext* pContext);
```

And in the **private section** (find existing `private:` block):

```cpp
    struct LastCommit {
        std::wstring text;       // What was written to document (including trailing char)
        std::wstring rawInput;   // engine_->PeekRaw() snapshot
        bool hasTrailingChar = false;
        DWORD timestamp = 0;     // GetTickCount() at commit
    };
    LastCommit lastCommit_;
    CommitUndoState commitUndoState_ = CommitUndoState::Idle;

    static constexpr DWORD kCommitUndoTimeoutMs = 1500;

    void RecordCommitSnapshot(std::wstring text, std::wstring rawInput, bool hasTrailingChar) noexcept;
```

- [ ] **Step 2: Commit**

```bash
git add src/tsf/EngineController.h
git commit -m "feat(tsf): declare commit-undo state machine for ESC restore-raw"
```

### Task 3.2: Implement state-machine helpers in `EngineController.cpp`

**Files:**
- Modify: `src/tsf/EngineController.cpp`

- [ ] **Step 1: Add helper methods**

Append at the end of `EngineController.cpp` (before the closing namespace brace if any; otherwise after the last existing method):

```cpp
void EngineController::RecordCommitSnapshot(std::wstring text,
                                            std::wstring rawInput,
                                            bool hasTrailingChar) noexcept {
    if (text.empty() || rawInput.empty()) {
        commitUndoState_ = CommitUndoState::Idle;
        lastCommit_ = {};
        return;
    }
    lastCommit_.text = std::move(text);
    lastCommit_.rawInput = std::move(rawInput);
    lastCommit_.hasTrailingChar = hasTrailingChar;
    lastCommit_.timestamp = GetTickCount();
    // Only CommitWithChar (trailing space) enters Ready. Plain Commit (Enter,
    // arrow, F-key) means cursor moves elsewhere — no undo window.
    commitUndoState_ = hasTrailingChar ? CommitUndoState::Ready : CommitUndoState::Idle;
    TSF_LOG(L"RecordCommitSnapshot: state=%d text='%ls' raw='%ls'",
            static_cast<int>(commitUndoState_), lastCommit_.text.c_str(), lastCommit_.rawInput.c_str());
}

bool EngineController::WithinUndoWindow() const noexcept {
    if (commitUndoState_ == CommitUndoState::Idle) return false;
    return (GetTickCount() - lastCommit_.timestamp) <= kCommitUndoTimeoutMs;
}

void EngineController::TransitionUndoReadyToPrimed() noexcept {
    if (commitUndoState_ != CommitUndoState::Ready) return;
    commitUndoState_ = CommitUndoState::Primed;
    TSF_LOG(L"CommitUndo: Ready -> Primed");
}

void EngineController::ResetCommitUndo() noexcept {
    if (commitUndoState_ == CommitUndoState::Idle) return;
    TSF_LOG(L"CommitUndo: -> Idle (was state=%d)", static_cast<int>(commitUndoState_));
    commitUndoState_ = CommitUndoState::Idle;
    lastCommit_ = {};
}

void EngineController::OnNonRestoreKey() noexcept {
    if (commitUndoState_ != CommitUndoState::Idle) ResetCommitUndo();
}
```

(Leave `TryRestoreLastCommitRaw` for Task 3.4 — needs edit session class first.)

- [ ] **Step 2: Commit**

```bash
git add src/tsf/EngineController.cpp
git commit -m "feat(tsf): commit-undo state helpers (RecordSnapshot, transitions)"
```

### Task 3.3: Snapshot raw in `Commit` and `CommitWithChar`

**Files:**
- Modify: `src/tsf/EngineController.cpp:401-437`

- [ ] **Step 1: Modify `Commit`**

Find:
```cpp
void EngineController::Commit(ITfContext* pContext) {
    // VietType pattern: Get committed text, then request edit session, then reset engine.
    // This ensures TSF state and engine state are synchronized atomically.
    std::wstring committed = engine_->Commit();
```

Change to:
```cpp
void EngineController::Commit(ITfContext* pContext) {
    // VietType pattern: Get committed text, then request edit session, then reset engine.
    // This ensures TSF state and engine state are synchronized atomically.
    //
    // PeekRaw BEFORE engine_->Commit() — Commit() internally calls Reset() which
    // clears escRawHistory_ (see TelexEngineTest.EscRestoreRaw_PeekRawClearedByCommit).
    std::wstring rawSnapshot = engine_->PeekRaw();
    std::wstring committed = engine_->Commit();
```

Then find:
```cpp
    TSF_LOG(L"Commit called, text='%ls'", committed.c_str());
}
```

Change to:
```cpp
    TSF_LOG(L"Commit called, text='%ls'", committed.c_str());

    // Plain Commit (no trailing char) → no undo window. Caller is Enter/arrow/F-key.
    RecordCommitSnapshot(std::move(committed), std::move(rawSnapshot), /*hasTrailingChar=*/false);
}
```

- [ ] **Step 2: Modify `CommitWithChar`**

Find:
```cpp
void EngineController::CommitWithChar(ITfContext* pContext, wchar_t appendChar) {
    // VietType pattern: Get committed text, then request edit session, then reset engine.
    std::wstring committed = engine_->Commit();
```

Change to:
```cpp
void EngineController::CommitWithChar(ITfContext* pContext, wchar_t appendChar) {
    // VietType pattern: Get committed text, then request edit session, then reset engine.
    //
    // PeekRaw BEFORE engine_->Commit() — see Commit() comment above.
    std::wstring rawSnapshot = engine_->PeekRaw();
    std::wstring committed = engine_->Commit();
```

Then find:
```cpp
    TSF_LOG(L"CommitWithChar called, text='%ls'", committed.c_str());
}
```

Change to:
```cpp
    TSF_LOG(L"CommitWithChar called, text='%ls'", committed.c_str());

    // CommitWithChar always appends a trigger (space) — opens undo window.
    // `committed` already includes the appended char (set above).
    const bool hasTrailing = (appendChar != L'\0');
    RecordCommitSnapshot(std::move(committed), std::move(rawSnapshot), hasTrailing);
}
```

- [ ] **Step 3: Commit**

```bash
git add src/tsf/EngineController.cpp
git commit -m "feat(tsf): snapshot raw input in Commit/CommitWithChar for undo cache"
```

### Task 3.4: Create `EscRestoreLastCommitSession` edit session

**Files:**
- Create: `src/tsf/EscRestoreLastCommitSession.h`

- [ ] **Step 1: Write header-only edit session**

Create `src/tsf/EscRestoreLastCommitSession.h`:

```cpp
// src/tsf/EscRestoreLastCommitSession.h
//
// Edit session that restores raw keys for the most recent commit when user
// presses ESC after backspacing the commit trigger. Used by EngineController
// post-BS path (design 2026-05-17).
//
// Steps inside DoEditSession:
//   1. Read current selection (caret position).
//   2. ShiftStart back by `text.size()` chars → range covers the committed body.
//   3. SetText(range, rawInput) → replaces composed Vietnamese with raw keys.
//   4. Collapse range to end and update selection so caret lands after the
//      inserted raw text.
#pragma once

#include "tsf/EditSession.h"
#include "core/Logger.h"

#include <msctf.h>
#include <string>

namespace NextKey::TSF {

class EscRestoreLastCommitSession : public EditSession {
public:
    EscRestoreLastCommitSession(ITfContext* pContext,
                                std::wstring textToReplace,
                                std::wstring rawInput) noexcept
        : EditSession(pContext),
          textToReplace_(std::move(textToReplace)),
          rawInput_(std::move(rawInput)) {}

    [[nodiscard]] HRESULT STDMETHODCALLTYPE DoEditSession(TfEditCookie ec) override {
        if (textToReplace_.empty()) return S_OK;

        TF_SELECTION sel{};
        ULONG fetched = 0;
        HRESULT hr = pContext_->GetSelection(ec, TF_DEFAULT_SELECTION, 1, &sel, &fetched);
        if (FAILED(hr) || fetched == 0 || sel.range == nullptr) {
            TSF_LOG(L"EscRestoreLastCommitSession: GetSelection failed hr=0x%08X", hr);
            if (sel.range) sel.range->Release();
            return hr;
        }

        // Shift start back by text length so the range covers the committed body.
        LONG shifted = 0;
        hr = sel.range->ShiftStart(ec,
                                   -static_cast<LONG>(textToReplace_.size()),
                                   &shifted,
                                   nullptr);
        if (FAILED(hr) || shifted != -static_cast<LONG>(textToReplace_.size())) {
            TSF_LOG(L"EscRestoreLastCommitSession: ShiftStart partial hr=0x%08X shifted=%ld", hr, shifted);
            sel.range->Release();
            return FAILED(hr) ? hr : E_FAIL;
        }

        // Replace with raw keys.
        hr = sel.range->SetText(ec, 0, rawInput_.data(), static_cast<LONG>(rawInput_.size()));
        if (FAILED(hr)) {
            TSF_LOG(L"EscRestoreLastCommitSession: SetText failed hr=0x%08X", hr);
            sel.range->Release();
            return hr;
        }

        // Collapse range to end and update selection so caret follows inserted text.
        hr = sel.range->Collapse(ec, TF_ANCHOR_END);
        if (SUCCEEDED(hr)) {
            sel.style.ase = TF_AE_END;
            sel.style.fInterimChar = FALSE;
            (void)pContext_->SetSelection(ec, 1, &sel);
        }

        sel.range->Release();
        TSF_LOG(L"EscRestoreLastCommitSession: replaced '%ls' with raw '%ls'",
                textToReplace_.c_str(), rawInput_.c_str());
        return S_OK;
    }

private:
    std::wstring textToReplace_;
    std::wstring rawInput_;
};

}  // namespace NextKey::TSF
```

**Verification:** Confirm the parent class `EditSession` from `src/tsf/EditSession.h` exposes a protected `pContext_` member or stores the context. If the parent uses a different name (e.g., `context_`, `pContext`), replace the references in `DoEditSession` to match. Run `grep -n "ITfContext\|protected:\|public:" src/tsf/EditSession.h` and inspect.

If `EditSession` stores the context differently, adapt the constructor + access accordingly (no behavior change needed).

- [ ] **Step 2: Add file to CMake**

If the TSF target lists headers explicitly, add `src/tsf/EscRestoreLastCommitSession.h` to that list. Run:
```bash
grep -rn "EscRestoreLastCommitSession\|CompositionEditSession" src/tsf/CMakeLists.txt 2>/dev/null
```

If CMake auto-globs headers via `file(GLOB ...)`, no change needed. Otherwise add the new header alongside `CompositionEditSession.h`.

- [ ] **Step 3: Commit**

```bash
git add src/tsf/EscRestoreLastCommitSession.h src/tsf/CMakeLists.txt
git commit -m "feat(tsf): EscRestoreLastCommitSession edit session for raw restore"
```

### Task 3.5: Implement `TryRestoreLastCommitRaw` in `EngineController.cpp`

**Files:**
- Modify: `src/tsf/EngineController.cpp`

- [ ] **Step 1: Add include**

Near the top of `src/tsf/EngineController.cpp`, find the existing TSF edit-session includes and add:

```cpp
#include "tsf/EscRestoreLastCommitSession.h"
```

- [ ] **Step 2: Implement the method**

Append after the helpers added in Task 3.2:

```cpp
bool EngineController::TryRestoreLastCommitRaw(ITfContext* pContext) {
    if (commitUndoState_ != CommitUndoState::Primed) return false;
    if (!WithinUndoWindow()) {
        TSF_LOG(L"TryRestoreLastCommitRaw: window expired (state=%d age=%ums)",
                static_cast<int>(commitUndoState_),
                GetTickCount() - lastCommit_.timestamp);
        ResetCommitUndo();
        return false;
    }
    if (lastCommit_.text.empty() || lastCommit_.rawInput.empty()) {
        ResetCommitUndo();
        return false;
    }
    // The Primed state means the user already deleted the trailing trigger (space).
    // We replace just the committed body. If RecordCommitSnapshot stored the full
    // string including trailing char, strip it now.
    std::wstring body = lastCommit_.text;
    if (lastCommit_.hasTrailingChar && !body.empty()) {
        body.pop_back();
    }
    auto* pSession = new TSF::EscRestoreLastCommitSession(pContext, body, lastCommit_.rawInput);
    RequestEditSession(pContext, pSession);
    pSession->Release();

    // Reset state regardless of edit session outcome — session failure is logged
    // inside DoEditSession; caller falls back to pass-through ESC.
    ResetCommitUndo();
    return true;
}
```

- [ ] **Step 3: Commit**

```bash
git add src/tsf/EngineController.cpp
git commit -m "feat(tsf): TryRestoreLastCommitRaw — replaces committed body with raw"
```

### Task 3.6: ESC branch in `KeyEventSink.cpp`

**Files:**
- Modify: `src/tsf/KeyEventSink.cpp`

- [ ] **Step 1: Extend ESC branch in `OnTestKeyDown`**

Find (around line 179):
```cpp
    if (wParam == VK_ESCAPE
        && pEngineController_->IsEscRestoreRawEnabled()
        && pEngineController_->HasEngineBuffer()) {
        if (pEngineController_->CommitRawAndEnd(pContext)) {
            *pfEaten = TRUE;
            return S_OK;
        }
    }
```

Replace with:
```cpp
    if (wParam == VK_ESCAPE && pEngineController_->IsEscRestoreRawEnabled()) {
        // Live composition path — existing CommitRawAndEnd.
        if (pEngineController_->HasEngineBuffer()) {
            if (pEngineController_->CommitRawAndEnd(pContext)) {
                *pfEaten = TRUE;
                return S_OK;
            }
        }
        // Post-BS path — restore raw from lastCommit cache (design 2026-05-17).
        else if (pEngineController_->IsCommitUndoPrimed()
                 && pEngineController_->WithinUndoWindow()) {
            if (pEngineController_->TryRestoreLastCommitRaw(pContext)) {
                *pfEaten = TRUE;
                return S_OK;
            }
        }
    }
```

- [ ] **Step 2: Extend ESC branch in `OnKeyDown`**

Find (around line 284):
```cpp
    if (vk == VK_ESCAPE
        && pEngineController_->IsEscRestoreRawEnabled()
        && pEngineController_->HasEngineBuffer()) {
        if (pEngineController_->CommitRawAndEnd(pContext)) {
            *pfEaten = TRUE;
            return S_OK;
        }
    }
```

Replace with:
```cpp
    if (vk == VK_ESCAPE && pEngineController_->IsEscRestoreRawEnabled()) {
        if (pEngineController_->HasEngineBuffer()) {
            if (pEngineController_->CommitRawAndEnd(pContext)) {
                *pfEaten = TRUE;
                return S_OK;
            }
        }
        else if (pEngineController_->IsCommitUndoPrimed()
                 && pEngineController_->WithinUndoWindow()) {
            if (pEngineController_->TryRestoreLastCommitRaw(pContext)) {
                *pfEaten = TRUE;
                return S_OK;
            }
        }
    }
```

- [ ] **Step 3: Commit**

```bash
git add src/tsf/KeyEventSink.cpp
git commit -m "feat(tsf): ESC restore-raw post-BS path in KeyEventSink"
```

### Task 3.7: BS detection (Ready → Primed) in `OnKeyDown`

**Files:**
- Modify: `src/tsf/KeyEventSink.cpp`

- [ ] **Step 1: Add Ready → Primed transition before existing BS revive branch**

In `OnKeyDown`, find the existing BackspaceRevive logic. The exact line depends on local edits but look for:
```cpp
    if (!wantKey && wParam == VK_BACK && !pEngineController_->HasEngineBuffer()) {
        if (pEngineController_->PrepareBackspaceRevive(pContext)) {
            wantKey = true;
        }
    }
```

**Note:** If the above is in `OnTestKeyDown` only, search OnKeyDown for `VK_BACK`. The relevant insertion point is the first place in OnKeyDown that handles VK_BACK with engine empty. If no such place exists, add the new block immediately after the ESC branch from Task 3.6.

Add **before** the BackspaceRevive block (or at the determined insertion point):

```cpp
    // Commit-undo BS detection (design 2026-05-17): Ready → Primed.
    // The user is about to delete the trailing commit char (space). Let BS
    // pass through to the document; ESC arriving next will restore raw via
    // TryRestoreLastCommitRaw. Second BS in Primed → cancel (user is deleting
    // committed body, not undoing tone).
    if (vk == VK_BACK && !pEngineController_->HasEngineBuffer()) {
        if (pEngineController_->IsCommitUndoReady()
            && pEngineController_->WithinUndoWindow()) {
            pEngineController_->TransitionUndoReadyToPrimed();
            // Fall through — BS still passes to host so document state matches
            // (host deletes the space).
        } else if (pEngineController_->IsCommitUndoPrimed()) {
            // Second BS — user is now deleting committed body, drop the undo window.
            pEngineController_->ResetCommitUndo();
        }
    }
```

- [ ] **Step 2: Commit**

```bash
git add src/tsf/KeyEventSink.cpp
git commit -m "feat(tsf): BS in Ready transitions commit-undo to Primed"
```

### Task 3.8: Invalidate undo state on non-BS/ESC keys + focus change

**Files:**
- Modify: `src/tsf/KeyEventSink.cpp`

- [ ] **Step 1: Add invalidation in `OnTestKeyDown`**

Find the top of `OnTestKeyDown` (after modifier check / before wantKey logic). Add:

```cpp
    // Commit-undo invalidation: any key other than BS/ESC during Ready/Primed
    // means the user is no longer in the post-commit window. Drop the cache to
    // prevent stale restore on a later ESC.
    if (wParam != VK_BACK && wParam != VK_ESCAPE) {
        pEngineController_->OnNonRestoreKey();
    }
```

Place it AFTER the existing modifier check (Ctrl/Alt/Win) and BEFORE the ESC/punct/wantKey logic.

- [ ] **Step 2: Add the same in `OnKeyDown`**

Mirror the same block in `OnKeyDown`, in the same relative position.

- [ ] **Step 3: Add focus reset**

Find `OnSetFocus` (or the equivalent activation handler) in `KeyEventSink.cpp` or `EngineController.cpp`. If `KeyEventSink::OnSetFocus` exists, add inside its body:

```cpp
pEngineController_->ResetCommitUndo();
```

If only `EngineController::Reset()` exists for focus-loss, add inside its body:
```cpp
ResetCommitUndo();
```

Search command to locate:
```bash
grep -n "OnSetFocus\|::Reset\b" src/tsf/EngineController.cpp src/tsf/KeyEventSink.cpp
```

- [ ] **Step 4: Commit**

```bash
git add src/tsf/KeyEventSink.cpp src/tsf/EngineController.cpp
git commit -m "feat(tsf): invalidate commit-undo on non-restore keys + focus reset"
```

---

## Phase 4 — Verification

### Task 4.1: Linux test build + run engine test

**Files:** (no code changes)

- [ ] **Step 1: Build cross-platform tests**

```bash
cmake --build build-linux --target NextKeyTests 2>&1 | tail -20
```

Expected: build succeeds. If TypingEngine changes from previous tasks broke the contract, errors will surface here.

- [ ] **Step 2: Run engine tests**

```bash
./build-linux/tests/VKeyTests --gtest_filter="TelexEngineTest.EscRestoreRaw*"
```

Expected: all `EscRestoreRaw_*` tests PASS (existing 7 + new `EscRestoreRaw_PeekRawClearedByCommit` from Task 1.1).

- [ ] **Step 3: Run full test suite (regression)**

```bash
./build-linux/tests/VKeyTests
```

Expected: same pass count as before this branch (typically 1477+ tests, see `project_path_g_2026-05-07.md`). Any new failure → root-cause before proceeding.

### Task 4.2: Windows build (manual)

**Files:** (no code changes)

- [ ] **Step 1: Build VKey from WSL**

```bash
powershell.exe -Command "cd '\\wsl.localhost\Ubuntu-24.04\home\phatmt\code\NexusKey\build'; cmake --build . --target VKey --config Debug 2>&1" | tail -30
```

Expected: build succeeds. Look for warnings/errors on new files.

- [ ] **Step 2: Pack resources if Release**

```bash
extern/sciter/bin/packfolder.exe src/app/ui src/app/resources.cpp -v resources
```

(Skip for Debug build.)

### Task 4.3: Manual smoke — Hook engine

**Setup:** Run `VKey.exe` on Windows. Open Settings → Bảng gõ → toggle ON "ESC trả lại phím gốc". Restart app.

Test in **Notepad** first (simplest host):

- [ ] **Step 1: Baseline — live ESC still works**

Type `v i r u s` in Notepad. Composition shows `víu`. Press ESC. Verify screen shows `virus`. (Existing behavior — must NOT regress.)

- [ ] **Step 2: Bug fix — space then BS then ESC**

Type `v i r u s`. Composition `víu`. Press Space — screen `víu `. Press Backspace — screen `víu`. Press ESC. **Verify screen shows `virus`**.

- [ ] **Step 3: Negative — ESC pass-through when timed out**

Type `v i r u s Space Backspace`. Wait 3 seconds. Press ESC. Verify ESC reaches Notepad (no text change, focus dialog appears if there is one). Repeat with `tempOffMacroEsc` enabled — ESC must STILL pass through.

- [ ] **Step 4: Negative — ESC pass-through in Ready state (no BS)**

Type `v i r u s Space`. Without pressing BS, press ESC. Screen stays `víu ` (space preserved). Confirms Ready-state path is correctly out of scope.

- [ ] **Step 5: Multi-host smoke**

Repeat Step 2 in: Chrome omnibox, Discord, ChatGPT (Chromium), Notepad++, VS Code. Verify result in each.

### Task 4.4: Manual smoke — TSF DLL

**Setup:** TSF must be registered (`regsvr32 NextKeyTSF.dll` from build folder, Admin Cmd). Switch to TSF mode via toggle or restart with TSF active.

- [ ] **Step 1: Same flow as 4.3 Step 2 in WordPad (TSF host)**

Type `v i r u s Space Backspace ESC`. Verify screen `virus`.

- [ ] **Step 2: Negative — click cursor then ESC**

Type `v i r u s Space`. Click mouse elsewhere in the same window (cursor moves). Click back to original position. Press Backspace, then ESC. Verify ESC does NOT restore raw (cache invalidated by intervening key/non-BS-ESC events or by stale state). Acceptable outcomes: ESC passes through to host, OR ESC fires but with stale text — both are documented limitations.

- [ ] **Step 3: Negative — second BS deletes committed body**

Type `v i r u s Space`. Press Backspace twice. Verify screen shows `ví` (second BS deletes 'u' from committed body). Press ESC — must pass through (no restore).

- [ ] **Step 4: Regression — TSF live ESC still works**

Type `v i r u s` in WordPad (live composition). Press ESC. Verify `virus` (existing CommitRawAndEnd path).

### Task 4.5: Chaos regression

- [ ] **Step 1: Run chaos.toml across 5 baseline hosts**

Per `docs/baselines/perf-baseline-channeltraits-chaos.md`. Verify pass rate matches pre-branch baseline. Any new failure attributable to this branch → file in `docs/baselines/` or roll back the relevant task.

---

## Self-review checklist

After all phases:

- [ ] Spec §3 (Hook architecture) → Tasks 2.1–2.4 implement every bullet ✓
- [ ] Spec §4 (TSF architecture) → Tasks 3.1–3.8 implement every bullet ✓
- [ ] Spec §5 (Performance gates) → Hook hot path adds only 1 atomic compare + 1 state check; TSF adds only timestamp compare ✓
- [ ] Spec §6 (Test plan) → Engine snapshot test Task 1.1; manual smoke Tasks 4.3–4.4 ✓
- [ ] Spec §7 (File checklist) → matches Tasks 2.1, 2.2, 2.4, 3.1–3.8 ✓
- [ ] Spec §8 (Why-comments) → Task 2.2 + 3.3 + 2.3 (path 2) + 3.7 each include the why-comment ✓
- [ ] Spec §9 (Out of scope) → no task addresses multi-word, Ready ESC, cursor verification — correct ✓
- [ ] Coding Rule 11 (hot path): Tasks 2.3/2.4 only add atomic load + state compare + early return for non-ESC keys ✓
- [ ] Coding Rule 9 (naming): `rawInput`, `lastCommit_`, `commitUndoState_` follow conventions ✓
- [ ] No placeholders: every step has exact code / commands / expected output ✓
