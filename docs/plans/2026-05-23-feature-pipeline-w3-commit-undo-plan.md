# Wave 3 — Extract CommitUndoFeature + per-stage Coordinator dispatch

**Date**: 2026-05-23
**Branch**: `feat/architecture-review-v3.1`
**Predecessor**: Wave 2 shipped (HEAD `f8af611` — BackwardEditFeature wired, all 6 ReplaceComposition call-sites route through Coordinator, 54/55 Windows chaos).
**Goal**: Second feature plugin — `CommitUndoFeature` at `Stage::PreEngine` priority 20. `ProcessKeyDown` step 2d (lines 1034–1038) routes through `Coordinator::HandleKeyAtStage(PreEngine, …)` instead of calling `HandleCommitUndo` inline. Byte-identical behavior preserved.

---

## 0. Strategic decision — "wrap, don't lift" (continued from W2)

Wave 3 wraps the existing `HandleCommitUndo` (267 LOC) + helpers (`SetCommitUndoReady` 8, `CancelCommitUndo` 6, `ReplayCommittedChars` 47 = 61 LOC) behind an `ICommitUndoExecutor` interface. HookEngine implements that interface by calling its existing methods. `CommitUndoFeature::Try()` delegates synchronously and maps the legacy `KeyOutcome` (Eat/Pass/Fallthrough) to the pipeline's `Result` + flow-control intents.

**Why wrap-only**:
1. The FSM has 3 states + 5+ transition triggers + synth-guard exemption + multi-space + UserDefined OEM routing. Touching the body risks subtle desync.
2. The 2-cancel-site bug (memo `project_commit_undo_synth_guard_exemption`) and multi-space fix (memo `project_commit_undo_space_2026-04-20`) are NOT introduced or worsened by wrapping. They remain in HookEngine — to be addressed by a future "lift" wave when the framework is more battle-tested.
3. W2 wrap succeeded with 54/55 byte-identical. Same approach for W3 = momentum + low risk.
4. Anh chose wrap explicitly (2026-05-23 brainstorm) over lift-now or wrap-then-lift.

**Net code impact (W3)**: ~120 LOC added across 8 new/modified files. `HookEngine.cpp` body untouched except for step 2d (4-line replacement) and ctor registration (~3 lines). Memo bugs preserved as-is.

---

## 1. Framework evolution required for W3

Two pipeline-level changes precede CommitUndoFeature itself:

### 1a. New intents for flow control

`HandleCommitUndo`'s return values are not purely about output — they encode flow control ("eat the key" / "pass to OS" / "continue host dispatch"). To express these via the pipeline's intent contract, add:

```cpp
namespace Intents {
    struct ConsumeKey {};      // ProcessKeyDown returns true (key eaten)
    struct PassThrough {};     // ProcessKeyDown returns false (pass to OS, no further work)
}
using Intent = std::variant<Backspace, Text, Reinject, ConsumeKey, PassThrough>;
```

Caller (HookEngine) inspects the batch after Coordinator dispatch and decides eat-vs-pass-vs-continue from the emitted intents.

### 1b. Per-stage dispatch

Current `Coordinator::HandleKey` runs **all stages** in one call. W3 needs to dispatch only PreEngine at step 2d, then later only PostEngine at the existing 6 `DispatchCoordinator` sites. Add:

```cpp
void HandleKeyAtStage(Stage stage, const KeyContext& ctx, IntentSink& sink);
```

This method runs only features registered at the given stage. Gates still evaluate once per call. The existing `HandleKey(ctx, sink)` is kept (calls all 3 stages in order) for test convenience and future "single-entry-point" use cases.

After W3.6, both W2 and W3 call sites use `HandleKeyAtStage` — `HandleKey` is no longer called from production code but lives on as a test/debug helper.

---

## 2. Task breakdown — TDD per task

Order: W3.1 → W3.2 → W3.3 → W3.4 → W3.5 → W3.6 → W3.7. Each ends with `cmake --build build-linux --target VKeyTests && ./build-linux/tests/VKeyTests`. Same subagent strategy as W2.

---

### W3.1 — Add `Intents::ConsumeKey` + `Intents::PassThrough`

**Purpose**: Two new variants in the Intent union for flow-control signaling.

**Files**:
- `src/core/pipeline/Intent.h` (modify)
- `tests/pipeline/IntentTest.cpp` (extend)
- `tests/pipeline/IntentSinkTest.cpp` (extend if needed)

**Change**:
```cpp
namespace Intents {
    struct Backspace { unsigned count; };
    struct Text      { std::wstring text; };
    struct Reinject  { std::uint16_t vk; };
    struct ConsumeKey {};                    // NEW
    struct PassThrough {};                   // NEW
}

using Intent = std::variant<Intents::Backspace,
                            Intents::Text,
                            Intents::Reinject,
                            Intents::ConsumeKey,
                            Intents::PassThrough>;
```

**TDD steps**:
1. RED — add 2 tests to `IntentTest.cpp`: construct `Intent{Intents::ConsumeKey{}}`, assert `std::holds_alternative<Intents::ConsumeKey>(...)`. Same for PassThrough.
2. GREEN — modify Intent.h, add the two trivial structs + variant args.
3. REFACTOR — verify no need for special trivial-ctor handling (empty structs are trivially constructible).

**Verify**: 1944 → 1946 tests.

**Commit**: `feat(pipeline): W3.1 — add ConsumeKey + PassThrough flow-control intents`

---

### W3.2 — Add `Coordinator::HandleKeyAtStage(Stage, ctx, sink)`

**Purpose**: Per-stage dispatch so PreEngine and PostEngine features fire at different points in `ProcessKeyDown`.

**Files**:
- `src/core/pipeline/Coordinator.h` (modify — add method decl)
- `src/core/pipeline/Coordinator.cpp` (modify — add impl)
- `tests/pipeline/CoordinatorDispatchTest.cpp` (extend) or new test file `CoordinatorStageDispatchTest.cpp`

**Change**:
```cpp
// Coordinator.h — add public method
void HandleKeyAtStage(Stage stage, const KeyContext& ctx, IntentSink& sink);

// Coordinator.cpp
void Coordinator::HandleKeyAtStage(Stage stage, const KeyContext& ctx, IntentSink& sink) {
    const GateMask raised = EvaluateGates(ctx);
    auto& bucket = features_[static_cast<std::size_t>(stage)];
    for (auto& feature : bucket) {
        if ((feature->Requires() & raised) != 0u) continue;
        const Result r = feature->Try(ctx, sink);
        if (r == Result::Handled) break;
        if (r == Result::Veto)    return;
    }
}
```

**TDD steps**:
1. RED — add tests:
   - `HandleKeyAtStage_PreEngine_runsOnlyPreEngineFeatures` — register one feature at PreEngine + one at PostEngine; HandleKeyAtStage(PreEngine) → only PreEngine.Try fires.
   - `HandleKeyAtStage_PostEngine_runsOnlyPostEngine` — symmetric.
   - `HandleKeyAtStage_Veto_doesNotAffectOtherStages` — Veto in PreEngine stops PreEngine only; subsequent HandleKeyAtStage(PostEngine) still runs PostEngine.
   - `HandleKeyAtStage_GatesEvalOnce` — verify gate evaluation runs once (use a mock IGate that counts calls).
2. GREEN — add impl per skeleton.
3. REFACTOR — `noexcept` where applicable.

**Verify**: 1946 → 1950 tests.

**Commit**: `feat(pipeline): W3.2 — Coordinator::HandleKeyAtStage per-stage dispatch`

---

### W3.3 — Migrate W2 `DispatchCoordinator` to `HandleKeyAtStage(PostEngine, …)`

**Purpose**: Once `HandleKeyAtStage` exists, W2's call sites must scope to PostEngine so a future PreEngine feature (W3.5 CommitUndoFeature) doesn't double-fire when `DispatchCoordinator` runs after engine processing.

**Files**:
- `src/app/system/HookEngine.cpp` — `DispatchCoordinator` body change

**Change**:
```cpp
// BEFORE:
coordinator_.HandleKey(keyCtx, outputChannel_);

// AFTER:
coordinator_.HandleKeyAtStage(NextKey::Pipeline::Stage::PostEngine, keyCtx, outputChannel_);
```

**TDD steps**:
1. No new unit tests — integration verified by existing tests + W3.7 chaos.
2. GREEN — one-line change.
3. Linux build + gtest — must still show 1950 pass (no regression).

**Verify**: 1950 tests still pass. Chaos at W3.7 confirms byte-identical PostEngine path.

**Commit**: `refactor(pipeline): W3.3 — DispatchCoordinator scoped to PostEngine stage`

---

### W3.4 — Define `ICommitUndoExecutor` interface

**Purpose**: Backend port for CommitUndoFeature. Mirrors W2's `IBackwardEditExecutor`.

**Files**:
- `src/core/pipeline/ICommitUndoExecutor.h` (new)
- (no test file — exercised via W3.5's mock)
- `CMakeLists.txt` — add header to NEXTKEY_CORE_SOURCES

**Skeleton**:
```cpp
// src/core/pipeline/ICommitUndoExecutor.h
#pragma once
#include <cstdint>

namespace NextKey::Pipeline {

// Mirror of NextKey::KeyOutcome — declared in pipeline namespace so the feature
// stays decoupled from HookEngine's enum (which lives in src/app/system/).
// HookEngine maps its KeyOutcome → this enum at the boundary.
enum class CommitUndoOutcome : unsigned char {
    Eat         = 0,   // ProcessKeyDown should return true
    Pass        = 1,   // ProcessKeyDown should return false (pass to OS)
    Fallthrough = 2,   // ProcessKeyDown should continue to step 3+
};

class ICommitUndoExecutor {
public:
    virtual ~ICommitUndoExecutor() = default;

    // Process a keystroke through the commit-undo FSM.
    // Wave 3: HookEngine implements by calling its existing HandleCommitUndo.
    // Wave N+: lift FSM body into the feature for true single-owner state.
    [[nodiscard]] virtual CommitUndoOutcome HandleCommitUndo(
        std::uint16_t vkCode, bool vnMode) = 0;
};

}  // namespace NextKey::Pipeline
```

**TDD steps**:
1. No standalone tests — covered by W3.5's mock executor.
2. GREEN — create the header. Add to CMake.
3. Build passes (header-only).

**Verify**: 1950 tests still pass (header doesn't change runtime).

**Commit**: `feat(pipeline): W3.4 — ICommitUndoExecutor + CommitUndoOutcome`

---

### W3.5 — `CommitUndoFeature` plugin

**Purpose**: PreEngine prio 20 feature. Delegates to executor, maps outcome to Result + intent.

**Files**:
- `src/core/pipeline/CommitUndoFeature.h` (new)
- `src/core/pipeline/CommitUndoFeature.cpp` (new)
- `tests/pipeline/CommitUndoFeatureTest.cpp` (new)
- `CMakeLists.txt` — add all three

**Design**:
```cpp
// CommitUndoFeature.h
#pragma once
#include "core/pipeline/IFeature.h"
#include "core/pipeline/ICommitUndoExecutor.h"

namespace NextKey::Pipeline {

class CommitUndoFeature final : public IFeature {
public:
    explicit CommitUndoFeature(ICommitUndoExecutor& exec) noexcept : exec_(exec) {}

    [[nodiscard]] Stage    FeatureStage() const noexcept override { return Stage::PreEngine; }
    [[nodiscard]] int      Priority()     const noexcept override { return 20; }
    [[nodiscard]] GateMask Requires()     const noexcept override { return 0u; }  // always run

    [[nodiscard]] Result Try(const KeyContext& ctx, IntentSink& sink) override;

private:
    ICommitUndoExecutor& exec_;
};

}
```

```cpp
// CommitUndoFeature.cpp
#include "core/pipeline/CommitUndoFeature.h"
#include "core/pipeline/Intent.h"

namespace NextKey::Pipeline {

Result CommitUndoFeature::Try(const KeyContext& ctx, IntentSink& sink) {
    const CommitUndoOutcome outcome = exec_.HandleCommitUndo(ctx.vk, /*vnMode*/true);
    // ^ vnMode currently always true at the W3 wiring site (step 2d only runs in vn-mode block;
    //   anh wires the real flag in W3.6 via KeyContext or extra ctx field if needed).
    //   See W3.6 §"vnMode plumbing" for the resolution.

    switch (outcome) {
        case CommitUndoOutcome::Eat:
            sink.Emit(Intents::ConsumeKey{});
            return Result::Handled;
        case CommitUndoOutcome::Pass:
            sink.Emit(Intents::PassThrough{});
            return Result::Veto;
        case CommitUndoOutcome::Fallthrough:
            return Result::Pass;
    }
    return Result::Pass;  // defensive
}

}
```

**TDD steps**:
1. RED — `CommitUndoFeatureTest.cpp` with 6 tests using a `MockCommitUndoExecutor`:
   - `EatOutcomeEmitsConsumeKeyAndHandled`
   - `PassOutcomeEmitsPassThroughAndVeto`
   - `FallthroughOutcomeEmitsNothingAndPass`
   - `PassesVkToExecutor` — assert exec.last_vk_ == ctx.vk
   - `MetadataIsPreEngine_Prio20_NoGates` — assert stage/priority/requires
   - `MultipleCallsAreStateless` — invoke Try twice; second call independent of first
2. GREEN — create files per skeletons. Add to CMake.
3. REFACTOR — `final`, `noexcept`, `[[nodiscard]]`, `override`.

**Verify**: 1950 → 1956 tests.

**Commit**: `feat(pipeline): W3.5 — CommitUndoFeature thin wrapper`

---

### W3.6 — Wire CommitUndoFeature into HookEngine step 2d

**Purpose**: HookEngine implements `ICommitUndoExecutor`. Ctor registers the feature. `ProcessKeyDown` step 2d routes through `Coordinator::HandleKeyAtStage(PreEngine, …)` and inspects the batch.

**Files**:
- `src/app/system/HookEngine.h` — add executor inheritance + override decl
- `src/app/system/HookEngine.cpp` — impl + ctor registration + step 2d replacement

**Changes**:

```cpp
// HookEngine.h
#include "core/pipeline/ICommitUndoExecutor.h"

class HookEngine final
    : public NextKey::Pipeline::IBackwardEditExecutor
    , public NextKey::Pipeline::ICommitUndoExecutor {
    // ...

    // ICommitUndoExecutor — Wave 3 adapter.
    [[nodiscard]] NextKey::Pipeline::CommitUndoOutcome HandleCommitUndo(
        std::uint16_t vkCode, bool vnMode) override;
};
```

Note: HookEngine already has a `HandleCommitUndo(DWORD, bool)` method. The override has a different signature (`uint16_t` vs `DWORD`, returns `Pipeline::CommitUndoOutcome` vs `KeyOutcome`). They coexist — the override is the interface adapter, calling through to the existing method:

```cpp
// HookEngine.cpp — near ExecuteReplace adapter
NextKey::Pipeline::CommitUndoOutcome HookEngine::HandleCommitUndo(
    std::uint16_t vkCode, bool vnMode) {
    // Disambiguate: this is the Pipeline::ICommitUndoExecutor override.
    // It calls the *other* HandleCommitUndo (DWORD-flavored, returns
    // KeyOutcome) which contains the FSM body.
    const KeyOutcome out = this->HandleCommitUndo(
        static_cast<DWORD>(vkCode), vnMode);
    switch (out) {
        case KeyOutcome::Eat:         return NextKey::Pipeline::CommitUndoOutcome::Eat;
        case KeyOutcome::Pass:        return NextKey::Pipeline::CommitUndoOutcome::Pass;
        case KeyOutcome::Fallthrough: return NextKey::Pipeline::CommitUndoOutcome::Fallthrough;
    }
    return NextKey::Pipeline::CommitUndoOutcome::Fallthrough;  // defensive
}
```

**ATTENTION naming collision**: the override above CALLS itself recursively if disambiguation fails. Two options:
- Rename the override to `HandleCommitUndoForFeature` and have it call `HandleCommitUndo(DWORD, bool)` — clean, no ambiguity.
- Use explicit qualification on the inner call: `this->HookEngine::HandleCommitUndo(static_cast<DWORD>(vkCode), vnMode)` — still ambiguous if both overloads share the name.

Recommended: **rename the existing FSM method to `HandleCommitUndoFsm(DWORD, bool)`** and have the override call the renamed FSM directly. This is a small rename in 1 file (HookEngine.h decl + HookEngine.cpp impl + 1 call site in ProcessKeyDown step 2d — which is replaced in W3.6 anyway).

Then:
```cpp
// HookEngine.cpp
NextKey::Pipeline::CommitUndoOutcome HookEngine::HandleCommitUndo(
    std::uint16_t vkCode, bool vnMode) {
    const KeyOutcome out = HandleCommitUndoFsm(static_cast<DWORD>(vkCode), vnMode);
    switch (out) {
        case KeyOutcome::Eat:         return NextKey::Pipeline::CommitUndoOutcome::Eat;
        case KeyOutcome::Pass:        return NextKey::Pipeline::CommitUndoOutcome::Pass;
        case KeyOutcome::Fallthrough: return NextKey::Pipeline::CommitUndoOutcome::Fallthrough;
    }
    return NextKey::Pipeline::CommitUndoOutcome::Fallthrough;
}
```

**Ctor registration** (after existing Wave 2 registration):
```cpp
coordinator_.Register(
    std::make_unique<NextKey::Pipeline::CommitUndoFeature>(*this));
```

**Step 2d replacement** (HookEngine.cpp lines 1034–1038):
```cpp
// BEFORE:
switch (HandleCommitUndo(vkCode, vnMode)) {
    case KeyOutcome::Eat: return true;
    case KeyOutcome::Pass: return false;
    case KeyOutcome::Fallthrough: break;
}

// AFTER (W3.6 replacement):
{
    std::wstring rawSnapshot = engine_ ? engine_->PeekRaw() : std::wstring{};
    const std::wstring& engineRendered = engine_ ? engine_->Peek() : std::wstring{};
    NextKey::Pipeline::HookCompositionSession session(
        previousComposition_, engineRendered, rawSnapshot);
    NextKey::Pipeline::KeyContext keyCtx{
        static_cast<std::uint16_t>(vkCode),
        L'\0',
        false, false, false, false, false,
        session,
        0
    };
    coordinator_.HandleKeyAtStage(
        NextKey::Pipeline::Stage::PreEngine, keyCtx, outputChannel_);
    auto batch = outputChannel_.TakeBatch();
    for (const auto& intent : batch) {
        if (std::holds_alternative<NextKey::Pipeline::Intents::ConsumeKey>(intent))   return true;
        if (std::holds_alternative<NextKey::Pipeline::Intents::PassThrough>(intent))  return false;
    }
    // No intent → Fallthrough: continue to step 3+
}
```

**vnMode plumbing**: the W3.5 feature passes `/*vnMode*/true` to the executor. The real `vnMode` flag is local to `ProcessKeyDown` and isn't on `KeyContext`. For W3, the override receives only `vkCode`. To pass real vnMode:
- Option A — add `bool vnMode` to `KeyContext`. Feature reads it, passes to executor.
- Option B — executor reads `vietnameseMode_.load()` itself (it's already a HookEngine member).
- Option C — the override always reads `vietnameseMode_.load()` from HookEngine inside its body.

Recommended: **Option C**. The override is a HookEngine method so it has access. `vnMode` was previously passed as a param because the legacy `HandleCommitUndoFsm` is called inline where `vnMode` is local. The Pipeline override always reads atomically — slightly different but byte-equivalent because `vietnameseMode_` only changes between keystrokes (writers are main thread, reader is hook thread). Safe.

Adjust W3.5 feature accordingly: drop `vnMode` from the interface — make it `HandleCommitUndo(std::uint16_t vk)` only. HookEngine reads vnMode internally. Simpler.

**Updated ICommitUndoExecutor**:
```cpp
[[nodiscard]] virtual CommitUndoOutcome HandleCommitUndo(std::uint16_t vkCode) = 0;
```

CommitUndoFeature drops the vnMode plumbing. HookEngine override:
```cpp
NextKey::Pipeline::CommitUndoOutcome HookEngine::HandleCommitUndo(std::uint16_t vkCode) {
    const bool vnMode = vietnameseMode_.load(std::memory_order_acquire);
    const KeyOutcome out = HandleCommitUndoFsm(static_cast<DWORD>(vkCode), vnMode);
    // ... mapping switch
}
```

**TDD steps**:
1. Rename `HandleCommitUndo` → `HandleCommitUndoFsm` in HookEngine.h + HookEngine.cpp + 1 call site in ProcessKeyDown step 2d (the line that's about to be replaced).
2. Add ICommitUndoExecutor inheritance + override impl.
3. Add ctor registration.
4. Replace step 2d body.
5. Linux build — verify clean compile.
6. Linux gtest — 1956/1956 pass (no new tests; integration verified by chaos at W3.7).

**Verify**: 1956 tests pass on Linux.

**Commit**: `feat(pipeline): W3.6 — wire CommitUndoFeature, route step 2d through Coordinator`

---

### W3.7 — Verify Linux + Windows chaos

**Linux**: 1956/1956.

**Windows** (anh runs):
1. Build: `cmake --build build --target VKeyApp --config Debug` (or Release).
2. (Optional) gtest Windows: `cmake --build build --target VKeyTests --config Debug` then run `build\tests\Debug\VKeyTests.exe`.
3. Chaos: `powershell -ExecutionPolicy Bypass -File tools\run-chaos.ps1 -Tag w3-release -VKeyExe build\Release\VKey.exe -RunnerExe build\tools\Release\VKeyTestRunner.exe`. Target: ≥ 54/55 (W2 baseline; Chrome 1.3 expected false-fail).
4. Manual scenarios that exercise commit-undo:
   - Type "ca" + SPACE (commit). Then BS → should restore "ca" + reach Primed state. Type tone modifier `j` → replay produces "cạ".
   - Type word + SPACE × 2 (multi-space). BS BS → commit-undo state recovers (memo `commit_undo_space`).
   - Type word + SPACE + ESC → cancel-composition exemption (Primed state, ESC is exempt).
   - Type word + SPACE + alpha → starts new word (commit-undo cancelled, replay does NOT fire).
5. Document in §5 verification log.

**Commit**: `docs(pipeline): W3.7 — chaos baseline + manual commit-undo scenarios`

---

## 3. Risk analysis

| Risk | Severity | Mitigation |
|------|----------|-----------|
| `HandleCommitUndoFsm` rename breaks a call site I miss | Medium | grep `HandleCommitUndo(` after rename; verify only step 2d (about to be replaced) referenced the renamed method. |
| `vnMode` re-read from atomic gives different value than the stack-local | Low | `vietnameseMode_` writers only fire on user toggle / config reload — both async to ProcessKeyDown. The hook callback already reloaded the atomic at function top; one extra read is benign. |
| `outputChannel_` accumulates flow-control intents across calls if step 2d's TakeBatch is missed | High | Step 2d **must** call `outputChannel_.TakeBatch()` before returning. Same pattern as W2. Verify in code review. |
| Intent variant grows — std::variant size increases | Low | Adding 2 empty structs adds 0 bytes to variant payload; only the discriminator bound shifts. No layout/perf concern. |
| `HandleKeyAtStage` regression for W2 PostEngine path | Medium | W3.3 changes W2's call site to PostEngine-only. If a feature later registers at PreEngine for backward edit, W3.3 ensures it only fires at the PreEngine site, not the PostEngine site. Tests in W3.2 verify single-stage scoping. |
| Multi-stage Veto leaks across stages | Low | `HandleKeyAtStage` only knows one stage. Veto stops within that stage. Per-stage callers (HookEngine step 2d and DispatchCoordinator) decide independently what Veto means in their context. |
| Test fixture for CommitUndoFeature needs vnMode plumbing | Resolved | Per W3.6 redesign, ICommitUndoExecutor's method takes only `vkCode`. No vnMode param needed in mock. |

---

## 4. Done definition

- [ ] 1956+ Linux gtests pass (1944 baseline + 12 new across W3.1/W3.2/W3.5).
- [ ] Windows build succeeds (Debug + Release).
- [ ] Chaos.toml 5 hosts: ≥ 54/55 (W2 baseline; Chrome 1.3 false-fail expected).
- [ ] Manual commit-undo scenarios all pass (W3.7 list).
- [ ] All 7 W3 commits on `feat/architecture-review-v3.1`.
- [ ] Memory file updated with W3 SHIPPED + new HEAD.
- [ ] `HandleCommitUndo` (old name) no longer called from any production path — only `HandleCommitUndoFsm` is, and only via the executor adapter.

---

## 5. Verification log

### Linux gtest (W3.7, 2026-05-23)

```
[==========] 1956 tests from 108 test suites ran. (3064 ms total)
[  PASSED  ] 1956 tests.
```

Pre-W3 baseline: 1944. After W3: 1956. Net: +12 new tests, 0 regressions.

Breakdown:
- W3.1 Intent variants: 2 tests
- W3.2 HandleKeyAtStage: 4 tests
- W3.5 CommitUndoFeature: 6 tests

### Wave 3 commit chain (on `feat/architecture-review-v3.1`)

```
74b86bd feat(pipeline): W3.6 — wire CommitUndoFeature, route step 2d through Coordinator
b2a6c3c feat(pipeline): W3.5 — CommitUndoFeature thin wrapper
f3dc5e3 feat(pipeline): W3.4 — ICommitUndoExecutor + CommitUndoOutcome
bc0eda6 refactor(pipeline): W3.3 — DispatchCoordinator scoped to PostEngine stage
8555f30 feat(pipeline): W3.2 — Coordinator::HandleKeyAtStage per-stage dispatch
c92cd42 feat(pipeline): W3.1 — add ConsumeKey + PassThrough flow-control intents
5796f8d docs(pipeline): W3 plan — extract CommitUndoFeature + per-stage Coordinator dispatch
```

### Windows chaos (pending — anh runs)

Required:
```powershell
powershell -ExecutionPolicy Bypass -File tools\run-chaos.ps1 `
  -Tag w3-release `
  -VKeyExe build\Release\VKey.exe `
  -RunnerExe build\tools\Release\VKeyTestRunner.exe
```

Target: ≥ 54/55 (W2 baseline). Chrome 1.3 expected false-fail (omnibox autocomplete, not VKey — same as W2).

Manual commit-undo scenarios to confirm:
1. Type "ca" + SPACE (commit). Then BS → Primed state. Type `j` (Telex tone) → expect replay producing "cạ".
2. Type word + SPACE × 2 (multi-space). BS × 2 → expect commit-undo state recovers correctly (multi-space fix preserved).
3. Type word + SPACE + ESC → cancel-composition exemption (Primed state, ESC is exempt). Expect undo state cleared, no replay.
4. Type word + SPACE + alpha → starts new word. Expect commit-undo cancelled, no replay fired.

Windows gtest (optional):
```cmd
cmake --build build --target VKeyTests --config Debug
build\tests\Debug\VKeyTests.exe
```

---

## 6. Out of scope for Wave 3

Deferred to later waves:
1. **Lift HandleCommitUndoFsm body into CommitUndoFeature::Try** — true single-owner state. Resolves 2-cancel-site bug (memo `commit_undo_synth_guard_exemption`) by unifying the two cancellation paths. Wave 7 or later.
2. **OEM customKeyMap commit-undo replay** — open TODO from memo `project_userdefined_oem_route_2026-05-16`: `;` bound to ToneDot doesn't replay through commit-undo. Hook this once FSM body is lifted (Wave 7+).
3. **MacroFeature + EscRestoreRawFeature** — next plugin extractions per design doc Wave 4. Both also `Stage::PreEngine`, will share dispatch with CommitUndoFeature.
4. **Real SpellCheckGate + ToneEscapeGate** — still W1 shells.
5. **`Result::Veto` cross-stage semantics** — currently HandleKey (all stages) treats Veto as "stop all stages". HandleKeyAtStage (single stage) treats Veto as "stop this stage". Inconsistency. Will revisit when more features land.

---

**End of Wave 3 plan.**
