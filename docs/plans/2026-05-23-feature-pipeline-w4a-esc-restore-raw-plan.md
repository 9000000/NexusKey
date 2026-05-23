# Wave 4a — Extract EscRestoreRawFeature (PreEngine prio 40)

**Date**: 2026-05-23
**Branch**: `feat/architecture-review-v3.1`
**Predecessor**: Wave 3 shipped (HEAD `27f36d4` — CommitUndoFeature at PreEngine prio 20, 1956/1956 Linux + 54/55 Windows chaos perf-neutral).
**Goal**: Add `EscRestoreRawFeature` at `Stage::PreEngine` priority 40. The `HandlePreDispatch` hotkey-triggered ESC check at lines 1578–1582 moves to the PreEngine pipeline. `TryEscRestoreRaw` body unchanged. **MacroFeature deferred to W4b** — independent wave because its EN/VN dispatch paths require HandlePreDispatch restructure.

---

## 0. Strategic decision — split W4 into W4a (Esc) + W4b (Macro)

Survey 2026-05-23 found:
- **EscRestoreRawFeature**: 55 LOC body (`TryEscRestoreRaw`) + 5-LOC dispatch site in HandlePreDispatch. Self-contained. Single call site (the MOD-CANCEL path at line 1981 stays — different trigger).
- **MacroFeature**: 68 LOC body (`TryExpandMacro`) + ~40 LOC of dual EN/VN paths (lines 1503–1535, 1549–1562, 1617–1626). Interleaved with mode tracking + buffer accumulation. Higher restructure cost.

Bundling both is achievable in one wave but multiplies risk for no shipping benefit. Splitting ships smaller, validates pipeline pattern for a 3rd feature, and isolates the macro complexity. Wave 4b can immediately follow.

**Net code impact (W4a)**: ~80 LOC added across 4 new/modified files. HookEngine net change ≈ −3 LOC (ESC check removed from HandlePreDispatch; +ctor registration; +executor adapter).

---

## 1. Pipeline state after W3

```
ProcessKeyDown
├── step 1   ...
├── step 2a-2c ...
├── step 2d  Coordinator::HandleKeyAtStage(PreEngine)
│             └── CommitUndoFeature (prio 20)  ← W3
└── step 3   HandlePreDispatch
              ├── line 1578-1582  hotkey ESC → TryEscRestoreRaw   ← W4a moves this OUT
              ├── line 1503-1535  English mode macro              ← W4b
              ├── line 1549-1562, 1617-1626  VN mode macro        ← W4b
              └── ...other dispatch logic...
```

After W4a, step 2d's PreEngine bucket contains 2 features: CommitUndoFeature (prio 20) → EscRestoreRawFeature (prio 40). CommitUndo runs first (state mutation + ESC exemption already encoded). If ESC reaches EscRestoreRawFeature, the FSM has already exempted it from cancel; raw restore can fire.

---

## 2. Task breakdown — TDD per task

### W4a.1 — `IEscRestoreRawExecutor` interface

**Files**:
- `src/core/pipeline/IEscRestoreRawExecutor.h` (new)
- `CMakeLists.txt` — add to NEXTKEY_CORE_SOURCES

**Skeleton**:
```cpp
// IEscRestoreRawExecutor.h
#pragma once
#include <cstdint>

namespace NextKey::Pipeline {

// Mirror of the relevant subset of HookEngine::KeyOutcome that
// TryEscRestoreRaw returns. Only Eat (key consumed) and Fallthrough
// (not handled, continue dispatch) are meaningful for ESC restore-raw.
// We reuse the Wave 3 CommitUndoOutcome enum's Eat/Fallthrough values
// to avoid yet another mirror enum — they have identical semantics here.
enum class EscRestoreOutcome : unsigned char {
    Eat         = 0,  // ESC consumed, raw input restored
    Fallthrough = 1,  // ESC did not apply (no live/primed composition,
                      //   or hotkey didn't match) — caller continues dispatch
};

class IEscRestoreRawExecutor {
public:
    virtual ~IEscRestoreRawExecutor() = default;

    // Process a VK_ESCAPE-class keystroke. The implementation reads
    // hotkey registry and live/primed-commit state internally and
    // dispatches via injector. Mirrors HandlePreDispatch's inline
    // logic at lines 1578-1582 of pre-W4a HookEngine.cpp.
    [[nodiscard]] virtual EscRestoreOutcome TryEscRestore(
        std::uint16_t vkCode, unsigned modifiers) = 0;
};

}  // namespace NextKey::Pipeline
```

**Why `modifiers` param**: today's hotkey match at line 1578 takes `(vkCode, currentMods, isDoubleTap, keyUp)`. For W4a single-tap key-down ESC, only vkCode + mods matter. Feature reads `ctx.shift/ctrl/alt/win` from KeyContext, packs into `unsigned` bitfield matching `HotkeyRegistry::Matches` mod format.

**Mods packing** (per HotkeyRegistry conventions — to be confirmed by code lookup in W4a.3 impl):
```cpp
unsigned mods = (shift ? kShift : 0) | (ctrl ? kCtrl : 0)
              | (alt   ? kAlt   : 0) | (win  ? kWin  : 0);
```

**TDD steps**: No standalone test (header-only interface). Exercised via W4a.2 mock.

**Commit**: `feat(pipeline): W4a.1 — IEscRestoreRawExecutor interface`

---

### W4a.2 — `EscRestoreRawFeature` plugin

**Files**:
- `src/core/pipeline/EscRestoreRawFeature.h` (new)
- `src/core/pipeline/EscRestoreRawFeature.cpp` (new)
- `tests/pipeline/EscRestoreRawFeatureTest.cpp` (new)
- `CMakeLists.txt` — add all three

**Skeleton**:
```cpp
// EscRestoreRawFeature.h
#pragma once
#include "core/pipeline/IFeature.h"
#include "core/pipeline/IEscRestoreRawExecutor.h"

namespace NextKey::Pipeline {

class EscRestoreRawFeature final : public IFeature {
public:
    explicit EscRestoreRawFeature(IEscRestoreRawExecutor& exec) noexcept
        : exec_(exec) {}

    [[nodiscard]] Stage    FeatureStage() const noexcept override { return Stage::PreEngine; }
    [[nodiscard]] int      Priority()     const noexcept override { return 40; }
    [[nodiscard]] GateMask Requires()     const noexcept override { return 0u; }  // always run

    [[nodiscard]] Result Try(const KeyContext& ctx, IntentSink& sink) override;

private:
    IEscRestoreRawExecutor& exec_;
};

}
```

```cpp
// EscRestoreRawFeature.cpp
#include "core/pipeline/EscRestoreRawFeature.h"
#include "core/pipeline/Intent.h"

namespace NextKey::Pipeline {

Result EscRestoreRawFeature::Try(const KeyContext& ctx, IntentSink& sink) {
    // Pack mods bitfield for the executor's hotkey check.
    // Bit layout matches HotkeyRegistry conventions (Shift|Ctrl|Alt|Win).
    constexpr unsigned kShift = 1u << 0;
    constexpr unsigned kCtrl  = 1u << 1;
    constexpr unsigned kAlt   = 1u << 2;
    constexpr unsigned kWin   = 1u << 3;
    const unsigned mods = (ctx.shift ? kShift : 0u)
                        | (ctx.ctrl  ? kCtrl  : 0u)
                        | (ctx.alt   ? kAlt   : 0u)
                        | (ctx.win   ? kWin   : 0u);

    const EscRestoreOutcome outcome = exec_.TryEscRestore(ctx.vk, mods);
    switch (outcome) {
        case EscRestoreOutcome::Eat:
            sink.Emit(Intents::ConsumeKey{});
            return Result::Handled;
        case EscRestoreOutcome::Fallthrough:
            return Result::Pass;
    }
    return Result::Pass;  // defensive
}

}
```

**Open question — mod packing**: Plan's `kShift/kCtrl/...` constants are a guess. Real values come from `HotkeyRegistry`. W4a.3 will confirm and reuse the actual symbols. If `HotkeyRegistry::Matches` takes `unsigned currentMods` with non-trivial bit layout, the feature might just pass through `ctx.modifiers_packed` (new ctx field). Simpler: have the executor accept individual bools (`bool shift, bool ctrl, bool alt, bool win`) — no packing ambiguity. Decide at W4a.3.

**TDD steps**:
1. **RED** — `EscRestoreRawFeatureTest.cpp` with 5 tests using `MockEscRestoreExecutor`:
   - `EatOutcomeEmitsConsumeKeyAndHandled`
   - `FallthroughOutcomeEmitsNothingAndPass`
   - `PassesVkToExecutor` — assert mock.last_vk_ == ctx.vk
   - `PassesModsToExecutor` — set ctx with ctrl=true, alt=true; assert exec receives mods bits.
   - `MetadataIsPreEngine_Prio40_NoGates`
2. **GREEN** — create source files per skeletons.
3. **REFACTOR** — `final`, `noexcept`, `[[nodiscard]]`, `override`.

**Verify**: 1956 → 1961 tests.

**Commit**: `feat(pipeline): W4a.2 — EscRestoreRawFeature thin wrapper`

---

### W4a.3 — HookEngine implements executor + remove HandlePreDispatch ESC

**Files**:
- `src/app/system/HookEngine.h` — add `IEscRestoreRawExecutor` to inheritance + override decl
- `src/app/system/HookEngine.cpp` — impl override + ctor registration + remove inline ESC check at line 1578-1582

**Override impl**:
```cpp
// HookEngine.cpp
NextKey::Pipeline::EscRestoreOutcome HookEngine::TryEscRestore(
    std::uint16_t vkCode, unsigned modifiers) {
    auto hotkeysSnap = hotkeys_.load(std::memory_order_acquire);
    if (!hotkeysSnap) {
        return NextKey::Pipeline::EscRestoreOutcome::Fallthrough;
    }
    const bool hotkeyMatch = hotkeysSnap->Matches(
        NextKey::Intent::CancelComposition,
        static_cast<DWORD>(vkCode),
        modifiers,
        /*isDoubleTap=*/false,
        /*keyUp=*/false);
    const bool hasLiveComposition = (engine_ && engine_->Count() > 0);
    const bool hasPrimedCommit =
        (commitUndoState_ == CommitUndoState::Primed) &&
        !commitStack_.empty() &&
        !commitStack_.back().rawInput.empty();
    if (hotkeyMatch && (hasLiveComposition || hasPrimedCommit)) {
        const KeyOutcome legacy = TryEscRestoreRaw();
        return (legacy == KeyOutcome::Eat)
            ? NextKey::Pipeline::EscRestoreOutcome::Eat
            : NextKey::Pipeline::EscRestoreOutcome::Fallthrough;
    }
    return NextKey::Pipeline::EscRestoreOutcome::Fallthrough;
}
```

**Note on `modifiers` type**: `HotkeyRegistry::Matches` takes a specific `Modifiers` type — W4a.3 implementer must look up the actual signature in `src/core/hotkey/HotkeyRegistry.h` and adjust the cast / bit-mapping accordingly. If the type is `Modifiers` enum bitmask matching the kShift/kCtrl/... constants in W4a.2, pass directly. Otherwise translate.

**Ctor registration** (after CommitUndoFeature):
```cpp
coordinator_.Register(
    std::make_unique<NextKey::Pipeline::EscRestoreRawFeature>(*this));
```

**Remove HandlePreDispatch ESC block** (lines 1578-1582):
```cpp
// DELETE THIS BLOCK — EscRestoreRawFeature handles ESC at step 2d now.
if (hotkeysSnap->Matches(Intent::CancelComposition, vkCode, currentMods,
                         /*isDoubleTap=*/false, /*keyUp=*/false)
    && (hasLiveComposition || hasPrimedCommit)) {
    return TryEscRestoreRaw();
}
```

If the surrounding `hasLiveComposition` / `hasPrimedCommit` locals (lines 1573-1577) become unused, delete them too. Verify by build.

**MOD-CANCEL site at line 1981 STAYS** — different trigger path (modifier double-tap / release in ProcessKeyUp), not in PreEngine dispatch flow. Out of W4a scope.

**TDD steps**:
1. Apply HookEngine changes.
2. Linux build — verify clean compile.
3. Linux gtest — 1961 pass (no new tests; integration verified by chaos at W4a.4).

**Verify**: 1961 tests pass on Linux.

**Commit**: `feat(pipeline): W4a.3 — wire EscRestoreRawFeature, remove inline ESC from HandlePreDispatch`

---

### W4a.4 — Verify Linux + Windows chaos

**Linux**: 1961/1961.

**Windows** (anh runs):
1. Build: `cmake --build build --target VKeyApp --config Release`
2. Chaos: same command as W3 with tag `w4a-release`. Target: ≥54/55 (Chrome 1.1+1.3 expected false-fail — omnibox autocomplete, unrelated to VKey).
3. Manual ESC scenarios:
   - Type "vit" → "vit" (no transform yet) → ESC → expect raw "vit" preserved.
   - Type "vieet" → "việt" → ESC → expect raw "vieet" restored.
   - Type "ca" + SPACE (commit) → BS → Primed state → ESC → expect raw "ca" restored from commit stack.
   - Type "vit" with modifier-as-cancel hotkey (alt-released double-tap if configured) → still works via line 1981 MOD-CANCEL path (NOT moved to feature in W4a — intentional).

**Commit**: `docs(pipeline): W4a.4 — verification log`

---

## 3. Risk analysis

| Risk | Severity | Mitigation |
|------|----------|-----------|
| `HotkeyRegistry::Matches` modifier type mismatch with W4a.2's mod packing | Medium | W4a.3 implementer reads the registry header and aligns. Plan uses placeholder bit constants. |
| Removing HandlePreDispatch ESC block leaves dead locals (`hasLiveComposition`, `hasPrimedCommit`) | Low | Build flags unused-var. Clean up if MSVC W4 warning fires. |
| ESC pressed at step 2d fires feature BEFORE HandlePreDispatch's other early returns | Medium | HandlePreDispatch step 3 logic continues only when no PreEngine feature consumed. CommitUndoFsm + EscRestoreRawFeature now both can short-circuit before HandlePreDispatch. Verify chaos covers normal typing flow. |
| Some hotkey config rebinds CancelComposition off ESC → feature doesn't fire on ESC | Low (correct behavior) | Feature respects the registry binding. If user mapped CancelComposition to Alt-X, feature fires on Alt-X. Same as before — behavior preserved. |
| MOD-CANCEL path at line 1981 + new PreEngine ESC feature double-fire | Low | MOD-CANCEL fires on modifier-release (ProcessKeyUp), PreEngine fires on key-down. Different vk paths; cannot collide for a single keystroke. |

---

## 4. Done definition

- [ ] 1961+ Linux gtests pass (1956 baseline + 5 new in W4a.2).
- [ ] Windows build succeeds.
- [ ] Chaos ≥ 54/55 (Chrome 1.1+1.3 false-fail expected).
- [ ] All 4 manual ESC scenarios pass on Windows.
- [ ] 5 W4a commits on `feat/architecture-review-v3.1`.
- [ ] Memory file updated with W4a SHIPPED + HEAD.

---

## 5. Out of scope for W4a

Deferred:
1. **MacroFeature** — Wave 4b (next).
2. **Lift `TryEscRestoreRaw` body** into the feature for single-owner state — future wave.
3. **MOD-CANCEL modifier-release path** at line 1981 — different trigger, not PreEngine flow. Separate wave if extracted.
4. **Real SpellCheckGate + ToneEscapeGate** — still W1 shells.

---

## 6. Verification log

(To be filled by W4a.4.)

---

**End of W4a plan.**
