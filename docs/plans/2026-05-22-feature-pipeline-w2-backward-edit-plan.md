# Wave 2 — Extract BackwardEditFeature + wire Coordinator into HookEngine

**Date**: 2026-05-22
**Branch**: `feat/architecture-review-v3.1`
**Predecessor**: Wave 1 skeleton (HEAD `fbcda40` — Coordinator + IFeature + IGate + OutputChannel inert)
**Goal**: First feature plugin routed via Coordinator. All six `ReplaceComposition` call-sites in `HookEngine.cpp` go through `Coordinator::HandleKey`. Behavior must be **byte-identical** with today's inline call.

---

## 0. Strategic decision — "wrap, don't lift"

Wave 2 builds the **wiring** (Session, Gate, Feature, Coordinator route). It does **not** semantically refactor `ReplaceComposition`'s 200-LOC Stage A–E branching. The feature is a thin wrapper that delegates to a `IBackwardEditExecutor` interface; HookEngine implements that interface by calling its existing `ReplaceComposition` method.

**Why wrap-only**:
1. Byte-identical constraint — moving 209 LOC across 4 conditional output branches risks subtle desync (sync-render retry-loop timing, VB6 EM_REPLACESEL fallback chain, raw reinject VK ordering, RecordSynthDispatch counters).
2. Wave 2 is the architectural *route*. Semantic lift (pure diff → feature; Stage A–E → OutputChannel executor) is Wave 3+ work after Coordinator route is proven.
3. Sequencing principle (anh 2026-05-22): "từ lớn tới bé, từ rộng tới hẹp" — get the big shape right first, optimize internals later.

**Net code impact (W2)**: ~150 LOC added in 5 new files (`HookCompositionSession`, `BackwardEditExecutor`, `BackwardEditFeature`, real gate read, Coordinator instantiation). `HookEngine.cpp` net change ≈ 0 (6 call-sites edited, but no logic removed — `ReplaceComposition` body untouched).

---

## 1. Task breakdown — TDD per task

Order: W2.1 → W2.2 → W2.3 → W2.4 → W2.5 → W2.6 → W2.7. Each ends with `cmake --build build-linux --target VKeyTests && ./build-linux/tests/VKeyTests`.

Subagent strategy mirrors Wave 1: dispatch a TDD-strict subagent per task with full spec, source files, expected commits. Reviewer agent after each.

---

### W2.1 — `HookCompositionSession` concrete impl

**Purpose**: Concrete `ICompositionSession` that wraps three `wstring_view`s. Caller constructs per-keystroke with the three relevant strings; session does not own them.

**Files**:
- `src/core/pipeline/HookCompositionSession.h` (new)
- `tests/pipeline/HookCompositionSessionTest.cpp` (new)
- `CMakeLists.txt` — add to `NEXTKEY_CORE_SOURCES` and `NEXTKEY_TEST_SOURCES`

**Skeleton**:
```cpp
// src/core/pipeline/HookCompositionSession.h
#pragma once
#include "core/pipeline/ICompositionSession.h"
#include <string_view>

namespace NextKey::Pipeline {

class HookCompositionSession final : public ICompositionSession {
public:
    HookCompositionSession(std::wstring_view prevRendered,
                            std::wstring_view engineRendered,
                            std::wstring_view rawInput) noexcept
        : prev_(prevRendered), eng_(engineRendered), raw_(rawInput) {}

    [[nodiscard]] std::wstring_view PreviousRendered() const noexcept override { return prev_; }
    [[nodiscard]] std::wstring_view EngineRendered()  const noexcept override { return eng_; }
    [[nodiscard]] std::wstring_view RawInput()        const noexcept override { return raw_; }

private:
    std::wstring_view prev_;
    std::wstring_view eng_;
    std::wstring_view raw_;
};

}
```

**TDD steps**:
1. **RED** — write `HookCompositionSessionTest.cpp` with 3 tests:
   - `ReturnsPreviousRendered` — construct with `(L"hie", L"hiê", L"hie")`, assert `PreviousRendered() == L"hie"`.
   - `ReturnsEngineRendered` — assert `EngineRendered() == L"hiê"`.
   - `ReturnsRawInput` — assert `RawInput() == L"hie"`.
   - Build fails (file doesn't exist).
2. **GREEN** — create `HookCompositionSession.h` per skeleton. Add to CMake. Tests pass.
3. **REFACTOR** — verify `noexcept`, `final`, `[[nodiscard]]`, header-only OK (no .cpp).

**Verify**: 1934 → 1937 tests pass.

**Commit**: `feat(pipeline): W2.1 — HookCompositionSession concrete view`

---

### W2.2 — Wire real `EnglishBiasGate`

**Purpose**: Replace the W1 shell `IsRaised() == false` with a real read of `vietnameseMode_`. Gate is **raised** (= blocks BackwardEdit) when in English mode.

**Files**:
- `src/core/pipeline/gates/EnglishBiasGate.h` (modify — ctor + member ref)
- `tests/pipeline/EnglishBiasGateTest.cpp` (new)
- `CMakeLists.txt` — add test file

**Design**:
- Gate holds `const std::atomic<bool>& vnMode_`. Constructor takes the reference.
- `IsRaised(const KeyContext&) const noexcept { return !vnMode_.load(std::memory_order_acquire); }`
- Atomic read is hot-path safe (matches existing `vietnameseMode_.load(acquire)` pattern in HookEngine).

**Skeleton**:
```cpp
// src/core/pipeline/gates/EnglishBiasGate.h
#pragma once
#include <atomic>
#include "core/pipeline/IGate.h"

namespace NextKey::Pipeline {

class EnglishBiasGate final : public IGate {
public:
    explicit EnglishBiasGate(const std::atomic<bool>& vnMode) noexcept : vnMode_(vnMode) {}

    [[nodiscard]] GateId Id() const noexcept override { return GateId::EnglishBias; }
    [[nodiscard]] bool   IsRaised(const KeyContext&) const noexcept override {
        return !vnMode_.load(std::memory_order_acquire);
    }

private:
    const std::atomic<bool>& vnMode_;
};

}
```

**TDD steps**:
1. **RED** — `EnglishBiasGateTest.cpp` with 3 tests:
   - `NotRaisedWhenVnModeTrue` — `std::atomic<bool> vn{true}; EnglishBiasGate g(vn); EXPECT_FALSE(g.IsRaised(stubCtx));`
   - `RaisedWhenVnModeFalse` — `vn = false; EXPECT_TRUE(g.IsRaised(stubCtx));`
   - `IdIsEnglishBias` — `EXPECT_EQ(g.Id(), GateId::EnglishBias);`
   - Build fails because old ctor signature is default.
2. **GREEN** — modify header per skeleton; update `tests/pipeline/CoordinatorGateFilterTest.cpp` if it constructs `EnglishBiasGate` directly (likely needs a local `atomic<bool>` to pass).
3. **REFACTOR** — verify `noexcept`, `[[nodiscard]]`.

**Verify**: 1937 → 1940 tests pass. Pre-existing CoordinatorGateFilterTest may need minor ctor fix.

**Commit**: `feat(pipeline): W2.2 — EnglishBiasGate reads vietnameseMode_`

---

### W2.3 — `BackwardEditFeature` thin wrapper

**Purpose**: Plugin class that, when invoked by Coordinator at `Stage::PostEngine` with EnglishBias unraised, delegates the backward-edit operation to an `IBackwardEditExecutor` interface. The feature does NOT emit intents in W2 — direct delegation. Intent-based output is deferred to Wave 3+.

**Files**:
- `src/core/pipeline/IBackwardEditExecutor.h` (new)
- `src/core/pipeline/BackwardEditFeature.h` (new)
- `src/core/pipeline/BackwardEditFeature.cpp` (new — tiny, but keeps header light)
- `tests/pipeline/BackwardEditFeatureTest.cpp` (new)
- `CMakeLists.txt` — three new entries

**Design**:
```cpp
// IBackwardEditExecutor.h
#pragma once
#include <cstdint>
#include <string_view>

namespace NextKey::Pipeline {

class IBackwardEditExecutor {
public:
    virtual ~IBackwardEditExecutor() = default;

    // Replace the foreground app's text with `newText`, optionally re-injecting `reinjectVk` first.
    // Wave 2: HookEngine implements this by calling its existing ReplaceComposition method.
    // Wave 3+: split into pure-diff (feature) + Stage A–E execute (OutputChannel).
    virtual void ExecuteReplace(std::wstring_view newText, std::uint16_t reinjectVk) = 0;
};

}
```

```cpp
// BackwardEditFeature.h
#pragma once
#include "core/pipeline/IFeature.h"
#include "core/pipeline/IBackwardEditExecutor.h"

namespace NextKey::Pipeline {

class BackwardEditFeature final : public IFeature {
public:
    explicit BackwardEditFeature(IBackwardEditExecutor& exec) noexcept : exec_(exec) {}

    [[nodiscard]] Stage    FeatureStage() const noexcept override { return Stage::PostEngine; }
    [[nodiscard]] int      Priority()     const noexcept override { return 10; }
    [[nodiscard]] GateMask Requires()     const noexcept override { return GateMaskFor(GateId::EnglishBias); }

    [[nodiscard]] Result Try(const KeyContext& ctx, IntentSink& /*sink*/) override;

private:
    IBackwardEditExecutor& exec_;
};

}
```

```cpp
// BackwardEditFeature.cpp
#include "core/pipeline/BackwardEditFeature.h"

namespace NextKey::Pipeline {

Result BackwardEditFeature::Try(const KeyContext& ctx, IntentSink& /*sink*/) {
    exec_.ExecuteReplace(ctx.session.EngineRendered(), ctx.reinjectVk);
    return Result::Handled;
}

}
```

**TDD steps**:
1. **RED** — `BackwardEditFeatureTest.cpp` with 4 tests:
   - `DelegatesEngineRenderedToExecutor` — mock executor records args; assert exec gets `L"hiê"` + `vk=0`.
   - `PassesReinjectVk` — `ctx.reinjectVk = 'A'`; assert exec gets `vk=0x41`.
   - `ReturnsHandled` — exec returns void; feature returns `Result::Handled`.
   - `MetadataIsPostEngine_Prio10_RequiresEnglishBias` — assert stage/priority/requires.
   - Build fails (files don't exist).
2. **GREEN** — create three files per skeletons. Wire into CMake. Tests pass.
3. **REFACTOR** — verify `final`, `noexcept`, `[[nodiscard]]`. Mock executor uses a small helper struct in the test file.

**Verify**: 1940 → 1944 tests pass.

**Commit**: `feat(pipeline): W2.3 — BackwardEditFeature thin wrapper`

---

### W2.4 — `HookEngine` implements `IBackwardEditExecutor`

**Purpose**: Hook the existing `ReplaceComposition(const std::wstring&, DWORD)` method to the new interface so the feature can call it without coupling to the full `HookEngine` class.

**Files**:
- `src/app/system/HookEngine.h` — add `IBackwardEditExecutor` to inheritance list, add `ExecuteReplace` override
- `src/app/system/HookEngine.cpp` — one-line method implementation

**Change**:
```cpp
// HookEngine.h
#include "core/pipeline/IBackwardEditExecutor.h"

class HookEngine final : public NextKey::Pipeline::IBackwardEditExecutor {
    // ... existing members ...

    // IBackwardEditExecutor — implemented in .cpp
    void ExecuteReplace(std::wstring_view newText, std::uint16_t reinjectVk) override;
};
```

```cpp
// HookEngine.cpp — append at end (near ReplaceComposition body)
void HookEngine::ExecuteReplace(std::wstring_view newText, std::uint16_t reinjectVk) {
    // Wave 2: thin adapter — delegates to existing logic. Wave 3+: replaces ReplaceComposition.
    ReplaceComposition(std::wstring{newText}, static_cast<DWORD>(reinjectVk));
}
```

**TDD steps**:
1. No new tests for this task — the interface contract is exercised by W2.3's mock, and the real behavior is exercised by W2.5's wiring + chaos at W2.7.
2. **GREEN** — apply changes. `cmake --build build-linux --target VKeyTests` succeeds (HookEngine.cpp is Win32-only so this only verifies header inclusion compiles on Linux side).
3. Windows build verification: deferred to W2.7. Linux verifies the header compiles cleanly via the include chain.

**Verify**: 1944 tests still pass (no behavior change).

**Commit**: `feat(pipeline): W2.4 — HookEngine implements IBackwardEditExecutor`

---

### W2.5 — Wire `Coordinator` into HookEngine at canary site

**Purpose**: Instantiate `Coordinator` + register `EnglishBiasGate` + register `BackwardEditFeature` in `HookEngine::Start` (or ctor). Route ONE `ReplaceComposition` call-site (HandleAlphaKey, line 2153) through `Coordinator::HandleKey`. This proves the wiring end-to-end before mass migration.

**Files**:
- `src/app/system/HookEngine.h` — add `std::unique_ptr<Coordinator> coordinator_` member, add `Pipeline::OutputChannel outputChannel_` member (kept across keystrokes for batch reuse — `TakeBatch` clears between calls)
- `src/app/system/HookEngine.cpp` — instantiate in `Start()` (or ctor), edit `HandleAlphaKey` call site

**Coordinator construction**:
```cpp
// HookEngine.cpp Start() (or appropriate init)
coordinator_ = std::make_unique<NextKey::Pipeline::Coordinator>();
coordinator_->RegisterGate(std::make_unique<NextKey::Pipeline::EnglishBiasGate>(vietnameseMode_));
coordinator_->Register(std::make_unique<NextKey::Pipeline::BackwardEditFeature>(*this));
```

**Canary call-site change** (HandleAlphaKey around line 2153):
```cpp
// BEFORE:
ReplaceComposition(composition, reinjectVk);

// AFTER:
{
    NextKey::Pipeline::HookCompositionSession session(
        previousComposition_, composition, engine_->PeekRaw());
    NextKey::Pipeline::KeyContext keyCtx{
        static_cast<std::uint16_t>(vkCode),
        keyChar,
        shift, capsLock, ctrl, alt, win,
        session,
        static_cast<std::uint16_t>(reinjectVk)
    };
    coordinator_->HandleKey(keyCtx, outputChannel_);
    outputChannel_.TakeBatch();  // discard — feature delegated, didn't emit
}
```

**Notes**:
- The local variables `shift`, `capsLock`, `ctrl`, `alt`, `win`, `keyChar` need to be derivable at the call-site. If they're not already in scope, pass placeholder values (`false`, `0`) — they're unused by `BackwardEditFeature::Try()`. Wave 3+ features may need them.
- `outputChannel_.TakeBatch()` is called to clear any inadvertent intents and prevent batch growth across keystrokes. In W2, the batch should be empty after every call (feature doesn't emit).

**TDD steps**:
1. **RED** — no new unit tests; integration verified by existing HookEngine tests + Linux build + Windows manual at W2.7.
2. **GREEN** — apply changes. Build passes on Linux (headers compile).
3. **REFACTOR** — extract a helper method `DispatchCoordinator(DWORD vkCode, DWORD reinjectVk, const std::wstring& composition)` if the call-site block is too verbose. Likely deferred until W2.6 when 5 more sites switch.

**Verify**: 1944 tests still pass on Linux. Anh runs Windows build + smoke test (single Vietnamese word typed, alpha key path).

**Commit**: `feat(pipeline): W2.5 — wire Coordinator into HookEngine, canary at HandleAlphaKey`

---

### W2.6 — Switch remaining 5 `ReplaceComposition` call-sites

**Purpose**: Replace the other five direct `ReplaceComposition` calls with the Coordinator path. After this, the framework owns all 6 sites; the old `ReplaceComposition` method survives only as the implementation backend of `ExecuteReplace`.

**Call-sites** (per W2 survey, all in HookEngine.cpp):
1. Line ~1688 — `HandleCommitUndo` (bracket key path)
2. Line ~1729 — `HandleCommitUndo` (UserDefined OEM path)
3. Line ~2164 — `HandleVniDigitKey`
4. Line ~2175 — `HandleBackspace`
5. Line ~2219 — `CommitComposition`

**Refactor strategy**:
- Each site replaces:
  ```cpp
  ReplaceComposition(composition, 0);  // most have reinjectVk == 0
  ```
  with:
  ```cpp
  DispatchCoordinator(vkCode, 0, composition);
  ```
- Define `void HookEngine::DispatchCoordinator(DWORD vkCode, DWORD reinjectVk, const std::wstring& composition)` as a private helper containing the boilerplate from W2.5.

**TDD steps**:
1. **RED** — no new unit tests.
2. **GREEN** — extract helper, replace all 5 sites. Build passes on Linux.
3. **REFACTOR** — verify W2.5's canary site also uses the helper (single source).

**Verify**: 1944 tests pass on Linux. Windows manual smoke at W2.7.

**Commit**: `feat(pipeline): W2.6 — switch all ReplaceComposition call-sites to Coordinator path`

---

### W2.7 — Verify chaos 55/55 + `hiệu→hiêj` regression

**Purpose**: Confirm byte-identical behavior on Windows.

**Steps**:
1. **Linux gtest**: `cmake --build build-linux --target VKeyTests && ./build-linux/tests/VKeyTests` → 1944/1944.
2. **Anh runs Windows build**:
   - `powershell.exe -Command "cd '\\wsl.localhost\Ubuntu-24.04\home\phatmt\code\NexusKey\build'; cmake --build . --target VKeyApp --config Debug"`
   - Manual smoke: type "hieu" → "hiệu" via Telex; confirm no garbled output.
   - Run NexusKey + chaos.toml at 1ms inter-key on Notepad: target 11/11 cases pass (or whatever baseline shows pre-W2 — must not regress).
   - Specific test: trigger `hiệu→hiêj` reproduction (focus flap during 'e' input). Should still NOT occur after W2 (W2 doesn't fix the bug — that's Wave 0/Phase 2 already on Main; this is verifying no NEW bug introduced).
3. **No regression** rule: any case that worked on `feat/architecture-review-v3.1` HEAD `fbcda40` must still work after W2.6.

**Document results**: append a short verification log to this plan doc under `## Verification log`.

**Commit**: `chore(pipeline): W2.7 — verification log + chaos 55/55 pass`

---

## 2. Risk analysis

| Risk | Severity | Mitigation |
|------|----------|-----------|
| Coordinator's HandleKey adds ns overhead on hot path | Low | One std::array indexed-access + gate eval + 1 virtual call. ~50 ns. Acceptable for 16 ms keystroke budget. |
| `outputChannel_` accumulates intents across keystrokes | Medium | Each call ends with `TakeBatch()`. Verify in W2.5 by asserting batch empty after canary site. |
| `KeyContext` field order mismatch with W1 struct | Low | Use designated initializers or named-field aggregate construction. Compiler catches reorder. |
| `engine_->PeekRaw()` returns by value → temporary destroyed mid-call | High | Snapshot to local `std::wstring rawSnapshot = engine_->PeekRaw();` THEN construct session pointing at `rawSnapshot`. Session lives inside the same block. |
| `previousComposition_` mutated during `ExecuteReplace` while session still has wstring_view to it | Medium | Session is destroyed before HandleKey returns; the `wstring_view` is read once per feature call. `ReplaceComposition` only mutates `previousComposition_` at its very end. Order: session.PreviousRendered() → diff → execute → write back. Safe. |
| Inheriting `IBackwardEditExecutor` on HookEngine introduces vtable, breaks layout assumptions | Low | HookEngine already has multiple virtual methods. Adding one more is a single vtable slot. Verify via build. |
| Windows-only behavioral divergence not caught by Linux gtest | High | W2.7 mandates Windows manual smoke + chaos. Cannot ship without anh's pass. |

---

## 3. Done definition

- [ ] 1944 Linux gtest pass (1934 + ~10 new across W2.1-W2.3 tests).
- [ ] Windows build succeeds (Debug + Release).
- [ ] Anh confirms: single-word "hiệu" types correctly via all 6 call-paths.
- [ ] Anh confirms: chaos.toml at 1ms inter-key on Notepad shows ≥ pre-W2 baseline (no regression).
- [ ] No `hiệu→hiêj` reproduction in normal use.
- [ ] All 7 W2 commits on `feat/architecture-review-v3.1`.
- [ ] Memory file `project_feature_pipeline_framework_2026-05-22.md` updated with W2 SHIPPED + new HEAD.

---

## 4. Out of scope for Wave 2

Deferred to Wave 3 or later:
1. **Semantic lift** — moving pure prefix-diff computation into `BackwardEditFeature::Try()` and emitting `Intents::Backspace + Intents::Text + Intents::Reinject`. Wave 3 work.
2. **OutputChannel executor** — building the real intent-flushing logic that calls `injector_->Replace()` and handles Stage A–E branching. Wave 3 work.
3. **Real `SpellCheckGate` and `ToneEscapeGate`** — still W1 shells. Wave 3 will wire these to `engProt_.bias == HardEnglish` and `toneEscaped_` respectively.
4. **Coordinator wired into TSF DLL** — TSF EngineController has its own ReplaceCompositionSimilar path. Wave 6+ work.
5. **Per-feature perf budget enforcement** — Wave 6 (replay harness).
6. **CommitUndoFeature** — Wave 3 (next plugin extraction; resolves `commit_undo_synth_guard_exemption` + `commit_undo_space` memos as side-effect of single-owner state).

---

## 5. Verification log

(To be filled by W2.7.)

---

**End of Wave 2 plan.**
