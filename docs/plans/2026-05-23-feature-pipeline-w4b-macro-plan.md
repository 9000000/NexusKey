# Wave 4b — Extract MacroFeature (Full extract, PreEngine prio 30)

**Date**: 2026-05-23
**Branch**: `feat/architecture-review-v3.1`
**Predecessor**: Wave 4a shipped (HEAD `a3af1ac` — EscRestoreRawFeature, 1961/1961 Linux + 54/55 Windows perf-neutral).
**Goal**: MacroFeature at `Stage::PreEngine` priority 30 (between CommitUndo 20 and EscRestoreRaw 40). MacroFeature owns BOTH macro tracking (rawMacroBuffer_ accumulation) AND expansion dispatch (TryExpandMacro call). All macro logic removed from HandlePreDispatch — net −65 LOC. Byte-identical macro behavior in EN + VN modes preserved.

---

## 0. Strategic decision — full extract per design philosophy

Anh 2026-05-23: "philosophy = plugin + KHÔNG code phân mảnh". Half-wrap (interface+class without Coordinator dispatch) ships fragmented structure — rejected. Skip = framework dở dang — rejected. Full extract = proper plugin shape.

**Why full extract is feasible**:
- Macro tracking and expansion ARE the macro subsystem — moving them together preserves semantics.
- HandlePreDispatch becomes simpler (loses 65 LOC of mode-conditional macro logic).
- MacroFeature::Try contains the gate decisions; executor adapter contains the legacy bodies.
- Single owner of `rawMacroBuffer_`, `tempMacroOff_`, `macroCrossCommit_` lifecycle.

**Risk**: HandlePreDispatch has EN-mode early-return path (`return Pass` at line 1547) that mixes macro tracking with mode dispatch. Extracting macro from EN mode requires preserving the "EN mode key passes to OS" semantic via `Intents::PassThrough` from the feature.

**Net code impact (W4b)**: ~−40 LOC (65 LOC removed from HandlePreDispatch, ~25 LOC added across 3 new pipeline files + HookEngine adapter).

---

## 1. Pre-W4b architecture audit

**Macro logic locations in HandlePreDispatch** (pre-W4b):

| Lines | Block | Purpose |
|-------|-------|---------|
| 1512–1547 | EN mode `if (!vnMode)` block | EN macro tracking + expansion + SkipMacro + buffer mgmt + `return KeyOutcome::Pass` at end |
| 1559–1572 | VN macro tracking | Per-keystroke buffer accumulation when `macroOn && hasMacros` |
| 1602–1613 | VN SkipMacro hotkey | When CancelMacro intent matches with empty engine + empty buffer → set tempMacroOff_ |
| 1615–1624 | VN macro expansion | On commit trigger: call TryExpandMacro + return outcome |

**Macro state fields** (per W4 survey):
- `macroEnabled_` (atomic bool, read-only on hook) — main thread writes
- `macroInEnglish_` (atomic bool, read-only on hook) — main thread writes
- `tempMacroOff_` (bool, hook-thread only) — per-word, cleared on ClearWordState
- `macroCrossCommit_` (bool, hook-thread only) — cleared inside TryExpandMacro + HandleCommitUndoFsm
- `rawMacroBuffer_` (std::wstring, hook-thread only) — accumulator
- `configSnapshot_->macroTable` (RCU shared_ptr, read-only on hook)

After W4b all hook-thread fields (`tempMacroOff_`, `macroCrossCommit_`, `rawMacroBuffer_`) are touched exclusively by the executor adapter — no other call sites mutate them. MacroFeature provides the **single owner** discipline.

---

## 2. Task breakdown — TDD per task

### W4b.1 — `IMacroExecutor` + `MacroOutcome` enum

**File**: `src/core/pipeline/IMacroExecutor.h` (new) + CMakeLists.txt

**Skeleton**:
```cpp
#pragma once
#include <cstdint>

namespace NextKey::Pipeline {

// Mirror of HookEngine's macro dispatch results. Differentiates the three
// "key consumed" paths that pre-W4b HandlePreDispatch had:
//   Eat        — return true from ProcessKeyDown (macro expanded, trigger eaten)
//   Pass       — return false (English mode pass-through; or SkipMacro Esc)
//   Fallthrough — continue to step 3+ (no macro action, no buffer-only update)
//   NoOp       — buffer updated but no commit trigger; continue dispatch
// NoOp vs Fallthrough: both let the hook continue; NoOp signals "tracking ran";
// Fallthrough signals "macro subsystem didn't engage at all" (macroOn=false etc).
enum class MacroOutcome : unsigned char {
    Eat         = 0,
    Pass        = 1,
    Fallthrough = 2,
    NoOp        = 3,
};

class IMacroExecutor {
public:
    virtual ~IMacroExecutor() = default;

    // Process a keystroke through the macro subsystem. The implementation
    // reads vnMode, macroEnabled_, macroInEnglish_ atomics + the RCU macroTable
    // snapshot internally. Mutates rawMacroBuffer_/tempMacroOff_/macroCrossCommit_
    // hook-thread fields. Calls existing TryExpandMacro for actual expansion.
    [[nodiscard]] virtual MacroOutcome HandleMacro(
        std::uint16_t vkCode,
        bool shift, bool capsLock, bool ctrl, bool alt, bool win) = 0;
};

}  // namespace NextKey::Pipeline
```

**TDD steps**: No standalone test. Exercised via W4b.2 mock.

**Commit**: `feat(pipeline): W4b.1 — IMacroExecutor + MacroOutcome`

---

### W4b.2 — `MacroFeature` plugin

**Files**:
- `src/core/pipeline/MacroFeature.h` (new)
- `src/core/pipeline/MacroFeature.cpp` (new)
- `tests/pipeline/MacroFeatureTest.cpp` (new)
- `CMakeLists.txt`

**Skeleton**:
```cpp
// MacroFeature.cpp
Result MacroFeature::Try(const KeyContext& ctx, IntentSink& sink) {
    const MacroOutcome outcome = exec_.HandleMacro(
        ctx.vk, ctx.shift, ctx.capsLock, ctx.ctrl, ctx.alt, ctx.win);
    switch (outcome) {
        case MacroOutcome::Eat:
            sink.Emit(Intents::ConsumeKey{});
            return Result::Handled;
        case MacroOutcome::Pass:
            sink.Emit(Intents::PassThrough{});
            return Result::Veto;
        case MacroOutcome::Fallthrough:
        case MacroOutcome::NoOp:
            return Result::Pass;
    }
    return Result::Pass;
}
```

Metadata: `Stage::PreEngine`, `Priority() = 30`, `Requires() = 0u`.

**TDD steps**:
1. RED — 6 tests with MockMacroExecutor:
   - `EatOutcomeEmitsConsumeKeyAndHandled`
   - `PassOutcomeEmitsPassThroughAndVeto`
   - `FallthroughOutcomeEmitsNothingAndPass`
   - `NoOpOutcomeEmitsNothingAndPass`
   - `PassesAllSixArgsToExecutor` — set ctx with vk=0x41, shift=true, alt=true; assert mock received exactly those values
   - `MetadataIsPreEngine_Prio30_NoGates`
2. GREEN — implement per skeleton.
3. REFACTOR — `final`/`noexcept`/`[[nodiscard]]`/`override`.

**Verify**: 1961 → 1967 tests.

**Commit**: `feat(pipeline): W4b.2 — MacroFeature plugin`

---

### W4b.3 — HookEngine implements IMacroExecutor

**Files**:
- `src/app/system/HookEngine.h` — add inheritance + override decl
- `src/app/system/HookEngine.cpp` — impl `HandleMacro` containing:
  - Atomic loads of `vnMode`, `macroEnabled_`, `macroInEnglish_`
  - RCU snapshot load for `configSnapshot_->macroTable`
  - Mods packing via existing `ComputeModMask`
  - EN-mode block: mirror lines 1512-1547 logic
  - VN-mode tracking block: mirror lines 1563-1572
  - VN SkipMacro hotkey: mirror lines 1606-1613
  - VN expansion: mirror lines 1615-1624
  - Maps `MacroResult::{ExpandedEatTrigger,ExpandedPassTrigger,NoMatch}` → `MacroOutcome::{Eat,Pass,Fallthrough/NoOp}` per branch

**Key contract preservation**:
- EN mode early-return semantics: when in EN mode AND macro section runs (line 1546 implicit), the result is `Pass`. Map to `MacroOutcome::Pass` → MacroFeature emits PassThrough → ProcessKeyDown returns false → key passes to OS as English. Byte-identical.
- VN mode `Fallthrough`: when macro didn't engage (e.g., not a commit trigger, or buffer empty), continue to next dispatch step. Map to `MacroOutcome::Fallthrough` or `NoOp`.
- `tempMacroOff_` mutation paths preserved (line 1522, 1537, 1610).
- `InjectKey(vkCode)` synthetic-pending path at line 1531, 1621 — feature can't emit synthetic injection via intents; executor calls `InjectKey` directly inside the adapter (side effect) and returns `Eat`.

**Implementation strategy** — single 80-LOC `HandleMacro` body that's a 1:1 transcription of the pre-W4b inline blocks, just guarded by mode + with the executor calling `TryExpandMacro` itself.

**Pseudocode outline**:
```cpp
MacroOutcome HookEngine::HandleMacro(...) {
    const bool vnMode = vietnameseMode_.load(acquire);
    const bool macroOn = macroEnabled_.load(acquire);
    const auto cfgSnap = configSnapshot_.load(acquire);
    const bool hasMacros = cfgSnap && !cfgSnap->macroTable.empty();
    auto hotkeysSnap = hotkeys_.load(acquire);
    const uint32_t currentMods = ComputeModMask(ctrl, shift, alt, win);

    if (!vnMode) {
        const bool macroEng = macroInEnglish_.load(acquire);
        if (!(macroOn && macroEng)) return MacroOutcome::Fallthrough;
        // <EN-mode tracking + SkipMacro + expansion block>
        return MacroOutcome::Pass;  // EN mode always returns Pass to OS
    }

    // VN mode:
    if (macroOn && hasMacros) {
        // <Tracking block>
    }
    if (hotkeysSnap && macroOn && hasMacros
        && hotkeysSnap->Matches(Intent::SkipMacro, vk, currentMods, false, false)
        && engine_->Count() == 0 && rawMacroBuffer_.empty()) {
        tempMacroOff_ = true;
        return MacroOutcome::Pass;  // Esc passes through
    }
    if (macroOn && hasMacros && !tempMacroOff_ && IsMacroTrigger(vk) && !rawMacroBuffer_.empty()) {
        wchar_t triggerChar = VkToMacroChar(vk);
        auto result = TryExpandMacro(triggerChar);
        if (result == MacroResult::ExpandedEatTrigger) return MacroOutcome::Eat;
        if (result == MacroResult::ExpandedPassTrigger) {
            if (synthEventsPending_ > 0) { InjectKey(vk); return MacroOutcome::Eat; }
            return MacroOutcome::Pass;
        }
    }
    return MacroOutcome::Fallthrough;
}
```

**Ctor registration**:
```cpp
coordinator_.Register(
    std::make_unique<NextKey::Pipeline::MacroFeature>(*this));
```

**TDD steps**:
1. No new unit tests for the adapter — verified by chaos at W4b.5.
2. GREEN — apply changes. Linux build passes.

**Verify**: 1967 Linux tests pass (no regression).

**Commit**: `feat(pipeline): W4b.3 — HookEngine implements IMacroExecutor`

---

### W4b.4 — Remove macro from HandlePreDispatch

**File**: `src/app/system/HookEngine.cpp`

**Removals** (HandlePreDispatch body, after macro now owned by feature at step 2d):
- Lines 1512-1547 — entire `if (!vnMode)` EN-mode block (36 LOC)
  - **Caveat**: the trailing `HOOK_LOG(L"  skip: Vietnamese mode OFF"); return KeyOutcome::Pass;` at lines 1546-1547 was the EN mode terminator. Pre-W4b it ran for non-vnMode AFTER the macro tracking. Post-W4b, the EN macro logic is in MacroFeature (which emits PassThrough → ProcessKeyDown returns false). But if EN mode is active and the feature returns NoOp/Fallthrough (e.g., macroOn=false), HandlePreDispatch must still terminate EN dispatch.
  - **Solution**: keep a 2-line `if (!vnMode) { HOOK_LOG(...); return KeyOutcome::Pass; }` after macro removal. Pre-W4b this also lived inside the `if (!vnMode)` body; now it's the only thing left.
- Lines 1559-1572 — VN macro tracking block (14 LOC)
- Lines 1602-1613 — VN SkipMacro hotkey block (12 LOC)
- Lines 1615-1624 — VN expansion block (10 LOC)

**Add**: comment block explaining macro is now owned by MacroFeature at step 2d.

**Total HandlePreDispatch reduction**: ~70 LOC removed, ~5 LOC added (comment + EN guard preservation) = net ~−65 LOC.

**TDD steps**:
1. Apply removals carefully — verify no leftover refs to `tempMacroOff_`, `rawMacroBuffer_`, `macroCrossCommit_` inside HandlePreDispatch (other than auto-caps / ToggleEnabled / DispatchKeyAction that touch these later — those stay).
2. Linux build + tests.

**Verify**: 1967 tests pass on Linux.

**Commit**: `refactor(pipeline): W4b.4 — remove macro logic from HandlePreDispatch`

---

### W4b.5 — Verify Linux + Windows chaos

**Linux**: 1967/1967.

**Windows** (anh runs): chaos.toml tag `w4b-release`. Target ≥54/55 (Chrome 1.3 false-fail expected).

**Manual macro scenarios**:
1. **EN mode macro**: switch to English mode, type `vt` + SPACE → expect macro expansion (assuming `vt` is in macro table).
2. **VN mode macro**: VN mode, type `vt` + SPACE → expect expansion.
3. **Multi-char macro**: type `aaaaa` + SPACE → expect buffer accumulation + (no match if `aaaaa` not in table).
4. **SkipMacro hotkey (default Esc)**: VN mode, empty engine + buffer → press Esc → expect tempMacroOff_ enabled, next macro typed doesn't expand.
5. **TempMacroOff reset**: after SkipMacro, type a word + commit → next word should re-enable macro.
6. **Cross-commit macro**: type word + SPACE (commit) + immediately type macro key + SPACE → expect macroCrossCommit_ handles multi-word case.

**Commit**: `docs(pipeline): W4b.5 — verification log`

---

## 3. Risk analysis

| Risk | Severity | Mitigation |
|------|----------|-----------|
| EN mode early-return at line 1547 lost during HandlePreDispatch refactor | HIGH | W4b.4 preserves the `if (!vnMode) return Pass` after macro removal — explicit in plan §W4b.4. |
| `tempMacroOff_` mutation race between feature (step 2d) and other paths | Low | Hook-thread exclusive; same as pre-W4b. |
| `rawMacroBuffer_` ordering: feature runs BEFORE engine push, so buffer state diverges from pre-W4b where tracking happened in step 3 (after step 2d) | Medium | Pre-W4b ProcessKeyDown step 2d (CommitUndo) doesn't touch rawMacroBuffer_; tracking at step 3 is the first mutation. Post-W4b tracking happens at step 2d which is functionally identical (no other code reads buffer between step 2d and step 3). |
| `InjectKey(vk)` side effect inside executor mid-Try() | Low | InjectKey is synchronous SendInput. Pre-W4b same pattern. |
| Feature returns Veto (Pass outcome) but EscRestoreRawFeature at prio 40 needed to run | Medium | Verify: ESC in EN mode with macroOn → MacroFeature might return Pass before EscRestoreRaw runs. Check: ESC in EN mode pre-W4b also hit `if (!vnMode) return Pass` so EscRestoreRaw NEVER fires in EN mode pre-W4b. Same post-W4b. ✓ |
| HandlePreDispatch comment/structure misalignment after removal | Low | Re-number "3a/3b/3c/3d" comments since macro blocks gone. |

---

## 4. Done definition

- [ ] 1967+ Linux gtests pass (1961 baseline + 6 new in W4b.2).
- [ ] Windows build succeeds.
- [ ] Chaos ≥ 54/55.
- [ ] All 6 manual macro scenarios pass on Windows.
- [ ] 6 W4b commits on `feat/architecture-review-v3.1`.
- [ ] Memory file updated.
- [ ] `HandlePreDispatch` net size reduced ~65 LOC. No macro state mutations remain inside HandlePreDispatch.

---

## 5. Out of scope for W4b

- **Lift TryExpandMacro body** into MacroFeature — defer; ~70 LOC of expansion logic stays in HookEngine.
- **MacroCase / MacroPrefix helpers** — internal to TryExpandMacro, no extraction needed.
- **Macro state RCU snapshot per-feature** — Wave 6+ when replay harness is in.

---

## 6. Verification log

### Linux gtest (W4b.5, 2026-05-23)

```
[==========] 1967 tests from 110 test suites ran. (3118 ms total)
[  PASSED  ] 1967 tests.
```

Pre-W4b baseline: 1961. After W4b: 1967. Net: +6 new (W4b.2 feature tests), 0 regressions.

### Wave 4b commit chain (on `feat/architecture-review-v3.1`)

```
afa99de refactor(pipeline): W4b.4 — remove macro logic from HandlePreDispatch
28e8820 feat(pipeline): W4b.3 — HookEngine implements IMacroExecutor
49931aa feat(pipeline): W4b.2 — MacroFeature plugin
3bbd93d feat(pipeline): W4b.1 — IMacroExecutor + MacroOutcome enum
022d3bd docs(pipeline): W4b plan — full extract MacroFeature (PreEngine prio 30)
```

### HookEngine.cpp net diff

- W4b.3 added ~120 LOC (HandleMacro adapter + ctor registration)
- W4b.4 removed ~85 LOC (macro blocks + cfgSnap/hasMacros locals + macroOn/macroEng params from HandlePreDispatch)
- Net: ~+35 LOC in HookEngine.cpp; +25 LOC across new pipeline files (IMacroExecutor.h + MacroFeature.h/.cpp)
- HandlePreDispatch body shrank ~70 LOC; single-owner discipline for macro state achieved

### Windows chaos + smoke (pending — anh runs)

```powershell
powershell -ExecutionPolicy Bypass -File tools\run-chaos.ps1 `
  -Tag w4b-release `
  -VKeyExe build\Release\VKey.exe `
  -RunnerExe build\tools\Release\VKeyTestRunner.exe
```

Target: ≥ 54/55 (Chrome 1.3 false-fail expected). Macro behavior must be byte-identical with pre-W4b.

Manual macro scenarios:
1. EN mode: switch to English → type `vt` + SPACE → expect VN macro expansion (if `vt` in table).
2. VN mode: type `vt` + SPACE → expect expansion.
3. Multi-char macro: type `aaaaa` + SPACE → no expansion (no match), buffer accumulates.
4. SkipMacro hotkey (Esc by default): empty engine + buffer → Esc → tempMacroOff_ on → next macro skipped.
5. Cross-commit macro: word + SPACE (commit) → immediately type macro + SPACE → expect macroCrossCommit_ behavior.
6. EN mode pass-through: type alpha chars in EN mode WITHOUT macro engagement → expect keys pass to OS unchanged (MacroFeature returns Pass via PassThrough intent).
