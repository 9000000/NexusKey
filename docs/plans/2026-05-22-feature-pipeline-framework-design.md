# Feature Pipeline Framework — Brain + Plugin Shape

**Date**: 2026-05-22
**Status**: Brainstorm output — design only, no code in this session
**Branch**: `feat/architecture-review-v3.1`
**Scope**: Define a feature-pipeline framework (Brain coordinator — `Coordinator` class in code — + plugin-shaped features) sitting **on top of** the thread-ownership foundation from `2026-05-19-architecture-review-design.md`. Resolves entanglement between Backward Edit, Commit-Undo, EnglishBias, Quick-Consonant rules, and Macro/AppOverride pathways that today share state through `HookEngine` directly.
**Triggering pain**: GH issue #178 + `hiệu → hiêj` bug observed in `[17:43:23.741] [PID:10068]` log. Architectural complaint from anh: features are wired into hook/engine, not isolated; on/off doesn't actually save cost; no standard flow → conflicts proliferate.

---

## 0. Context

### Anh's complaint (paraphrased)

1. **Backward edit nhúng thẳng vào hook + engine** — không tách được, conflict với commit-undo, macro, focus reset, injector swap.
2. **Thiếu khung chuẩn để đắp feature** — mỗi feature mới đẻ ra `if/else` cứng trong `ProcessKeyDown` / `PushChar`. Không có ordering manifest.
3. **Cần "brain" điều phối** — hook = tay/mắt, engine = trái tim, hiện thiếu não. Brain chia features thành lego brick.
4. **EnglishBias / auto-restore / cc→ch cần flow chuẩn** — không đan vào nhau gây bug.
5. **Tắt feature phải tiết kiệm thật** — không phải early-return inside function.
6. **Bug evidence**: `hiệu → hiêj` log — focus change interleave với `e + e + j` keystrokes; engine state reset giữa từ; output đã đẩy ra app, không undo được.

### Bug `hiệu → hiêj` decode

```
17:43:24.387  'e' → push e → states="hie", passthrough 'e'
17:43:24.532  FOCUS CHANGED (hwnd=0x405FA)  ← OS event, runs on main thread
17:43:24.533  'e' → push e → states="hiê", ReplaceComposition BS=2 send='ê' EATEN
17:43:24.543  ResetComposition (count=3, prev='hiê')  ← focus handler wipes engine
17:43:24.578  'j' → push 'j' → engine fresh, treated as new word, passthrough
              foreground app got: BS BS ê + j  =  "hiêj"
```

Root cause = Rule 11.3 violation: focus event mutates engine state from main thread, races mid-keystroke. **Architecture review 2026-05-19 Phase 2 (single-writer state + drain barrier) fixes this root cause directly.** Feature pipeline framework sits on that foundation.

### Relation to prior plans

| Plan | Concern | Relation |
|---|---|---|
| `2026-05-19-architecture-review-design.md` | Thread ownership, state RCU, replay harness | **Foundation.** This design depends on its Phase 1+2; reuses mailbox primitive |
| `2026-05-22-hookengine-degod-probe.md` | Atomic enum + ConfigSnapshotBuilder lift | Phase 0 shipped; Phase 1 verify folded into Wave 0 of this design |
| `feedback_design_philosophy.md` (memory) | "nhanh / gọn / nhẹ / mượt / plugin, không phân mảnh" | **Driving principle.** Plugin-shaped features but state stays single-owner |

---

## 1. The 6 conflict patterns

Sixteen+ features mapped to 6 recurring conflict shapes. Each pattern demands a framework primitive.

| # | Pattern | Evidence | Primitive needed |
|---|---|---|---|
| A | State race across thread (focus/config/toggle vs keydown) | Bug `hiệu→hiêj`; "Focus poll reset loop" (memo 2026-05-21); Rule 11.3 violation | **Command mailbox** (already in review Phase 2) |
| B | Output race within hook thread (backward-edit vs macro vs commit-undo vs injector swap) | Memo `commit_undo_synth_guard` 2 cancel sites; chrome 5.3 root cause; char swallowing fix | **Output channel** — single sink, intent-based |
| C | Ordering ambiguity for same key (`w` = tone-escape/horn/HornInsertU/custom-keymap) | HDDLDD memo 2026-05-21; HornInsertO/U empty-buffer memo 2026-05-17 | **Stage × priority manifest** |
| D | Gate-everywhere flags (EnglishBias, spellCheck, toneEscape) | "manaager" memo; HDDLDD memo; quên check = bug | **Gate as predicate object** |
| E | Cross-feature shared state (rawInput_ + commitStack_ + previousComposition_ + synthEventsPending_ + engine.states_) | char swallowing fix 2026-03-23; chrome 5.3 chaos | **Single composition session view** |
| F | Disabled feature still costs (flag = early-return inside function) | spell-check off, macro off — code still runs | **Registration manifest** (OFF = unregistered = uncalled) |

---

## 2. Four primitives the framework provides

1. **Command Mailbox** (cross-thread → hook thread)
   - Already designed in `2026-05-19-architecture-review-design.md` Phase 2.
   - Focus/config/toggle/tickpoll = posted commands; drained at top of LL hook callback before any key processing.
   - **Eliminates Pattern A immediately.**

2. **Output Channel** (single sink, intent-based)
   - Wraps `IOutputInjector` (Sprint 2 T3).
   - Features emit `Intent` enum: `Backspace{n}`, `Text{wstring}`, `Reinject{vk}`, `NoOp`.
   - Channel serializes intents per keystroke, tracks `synthEventsPending_` internally.
   - Features **never** call `SendInput` or touch `synthEventsPending_` directly.

3. **Stage × Priority Manifest**
   - Each feature declares `(Stage, Priority, GateMask)` at registration.
   - Coordinator dispatches stages in fixed order; within a stage, by priority ascending.
   - Three stages: `PreEngine`, `Engine`, `PostEngine`. (Engine itself is one feature for now — fractal expansion in Wave 7.)

4. **Gates + Composition Session View**
   - Gate = predicate object: `IsBlocked(KeyContext) → bool`. Three concrete gates today: `EnglishBiasGate`, `SpellCheckGate`, `ToneEscapeGate`.
   - Coordinator checks `feature.requires()` GateMask against gates **before** calling feature. Feature can't forget.
   - Composition session = const view over engine state + rawInput + commit stack. Features read; never mutate directly.

---

## 3. Coordinator shape (the "brain" / não)

### One-keystroke flow

```
LL hook callback (TAY)
   │
   ▼
Coordinator.HandleKey(vk, char, mods)             ← single entry point
   │
   ├─ 1. DrainMailbox()                          ← Pattern A fix
   │      handles pending FocusChanged / ConfigApply / ToggleVN before
   │      this key is even examined.
   │
   ├─ 2. Build KeyContext { vk, char, mods, session_view }
   │
   ├─ 3. Evaluate all gates ONCE                 ← Pattern D fix
   │      gates = { EnglishBias.eval(ctx), SpellCheck.eval(ctx), ToneEscape.eval(ctx) }
   │
   ├─ 4. For stage in { PreEngine, Engine, PostEngine }:    ← Pattern C fix
   │       for feature in registry.at(stage) sorted by priority:
   │           if feature.requires() not satisfied by gates: skip
   │           result = feature.try_(ctx, intent_sink)
   │           if result == Handled: break stage
   │           if result == Veto:    skip remaining stages
   │
   ├─ 5. OutputChannel.flush(intent_sink)        ← Pattern B fix
   │
   └─ return Eat | Pass
```

### Feature interface

```cpp
class IFeature {
public:
    virtual Stage     stage()    const = 0;
    virtual int       priority() const = 0;
    virtual GateMask  requires() const = 0;
    virtual Result    try_(const KeyContext&, IntentSink&) = 0;
    virtual ~IFeature() = default;
};

enum class Stage  { PreEngine, Engine, PostEngine };
enum class Result { Pass, Handled, Veto };

struct IntentSink {
    void emit(Intent::Backspace);
    void emit(Intent::Text);
    void emit(Intent::Reinject);
};
```

### Engine boundary (decision)

Engine keeps state. Coordinator reads via const view. **No state duplication.** Decision rationale (anh 2026-05-22):
> "đồng bộ thông tin với tốc độ ánh sáng để không bị delay"

Single-owner state = zero marshalling cost = zero sync delay. Coordinator ↔ Engine = const reference, not copy. Intent flow is **one-way** (feature → OutputChannel); no back-pressure or feedback loop.

This means `TypingEngine` API stays largely as-is (`PushChar`, `Peek`, `Reset`). New: `engine.last_transform()` returns the `TypingAction` enum already shipped in Path G G-3, exposed for Coordinator consumption.

### Where lives what

| Concern | Hook callback | Coordinator | Engine | Feature plugins |
|---|---|---|---|---|
| OS key intake | ✓ | | | |
| Synthetic event filter (`dwExtraInfo`) | ✓ | | | |
| Mailbox drain | | ✓ | | |
| Gate evaluation | | ✓ | | |
| Stage/priority dispatch | | ✓ | | |
| Composition state (engine.states_, rawInput_) | | | ✓ | (read view only) |
| Commit stack | | | | CommitUndoFeature owns |
| Diff edit (prev vs next rendered) | | | | BackwardEditFeature owns |
| Macro lookup | | | | MacroFeature owns |
| Output to OS | | | | (emit intent) → OutputChannel does SendInput |

---

## 4. Concrete example — Backward Edit as plugin

### Before (today, scattered)

`HookEngine.cpp:3488 ReplaceComposition` body, plus six callers, plus tangled state (`previousComposition_`, `synthEventsPending_`, `hadSynthInWord_`, `injector_`, `reinjectVk`). Focus reset + commit-undo + macro all interact through these globals.

### After (one file ~80 LOC)

```cpp
class BackwardEditFeature : public IFeature {
public:
    Stage     stage()    const override { return Stage::PostEngine; }
    int       priority() const override { return 10; }
    GateMask  requires() const override { return Gate::Bit(GateId::EnglishBias); }

    Result try_(const KeyContext& ctx, IntentSink& sink) override {
        auto prev = ctx.session.previous_rendered();   // view, zero-copy
        auto next = ctx.session.engine_rendered();
        if (prev == next) return Result::Pass;

        auto [bs, text] = DiffEditScript(prev, next);  // pure function
        sink.emit(Intent::Backspace{ bs });
        sink.emit(Intent::Text{ text });
        if (ctx.reinject_vk) sink.emit(Intent::Reinject{ ctx.reinject_vk });
        return Result::Handled;
    }
};
```

### Test offline (Linux gtest)

```cpp
TEST(BackwardEdit, hie_plus_e_yields_hi_e_replace) {
    auto ctx = TestCtx().withPrev(L"hie").withNext(L"hiê");
    auto sink = TestSink{};
    BackwardEditFeature f;
    EXPECT_EQ(f.try_(ctx, sink), Result::Handled);
    EXPECT_EQ(sink.intents, std::vector{ BS(2), Text(L"ê") });
}
```

No Win32, no `SendInput` mock, no `HookEngine`.

### What gets out of HookEngine

- `ReplaceComposition` body → `BackwardEditFeature` + pure `DiffEditScript`.
- `previousComposition_` → `CompositionSession.previous_rendered()` view.
- `synthEventsPending_` → owned by `OutputChannel`.
- `reinjectVk` → `KeyContext.reinject_vk`.
- `hadSynthInWord_` → derived inside `OutputChannel`.
- EnglishBias check in `ReplaceComposition` → gone (gate filter).

Net: ~150 LOC out of HookEngine.cpp, 80 LOC into a new file. Backward edit is testable in isolation.

---

## 5. Roadmap — 7 waves

| Wave | Scope | Depends on | Risk | Verify gate |
|---|---|---|---|---|
| **0** | Architecture review **Phase 1** (per-stage histogram) + **Phase 2** (single-writer state + command mailbox + 2-phase focus) + **de-god probe Phase 1 verify** (ConfigSnapshotBuilder Windows build pass + 1903 gtest pass — closes probe) | — | Medium — touches thread ownership | Histogram baseline captured 5×11 hosts; chaos 55/55 PASS; bug `hiệu→hiêj` killed by mailbox drain alone |
| **1** | Define **skeleton types**: `IFeature`, `Stage`, `Priority`, `GateMask`, `KeyContext`, `IntentSink`, `Intent`, `Coordinator` class (empty registry, dispatch loop), `OutputChannel` wrapping `IOutputInjector`, `CompositionSession` view | Wave 0 | Low — types + ~200 LOC Coordinator.cpp, no behaviour change | gtest registry, dispatch ordering, gate filter |
| **2** | Extract **BackwardEditFeature** (first plugin) | Wave 1 | Medium — must be byte-identical with `ReplaceComposition` | Chaos 55/55; 1903 gtest pass; manual `hiệu` regression test |
| **3** | Extract **CommitUndoFeature** (FSM + stack at `Stage::PreEngine`, prio 20) — **fixes memo `commit_undo_synth_guard_exemption` 2-cancel-site bug as side effect** | Wave 2 | Medium | Commit-undo gtest suite + chaos undo scenarios |
| **4** | Extract **MacroFeature** + **EscRestoreRawFeature** (both `Stage::PreEngine`) | Wave 3 | Low | Macro test suite + ESC variant memo coverage |
| **5** | Architecture review **Phase 3** (off-hook config reload via worker + RCU `ConfigSnapshot`) — can run in parallel with 3-4 | Wave 0 | Medium | `config_reload` stage disappears from hook-thread histogram |
| **6** | Architecture review **Phase 4** (replay harness for focus/config/injector interleave) | Waves 2-5 | Low | +30 gtest, chaos `--inject-focus-flap` / `--inject-config-reload` scenarios pass |
| **7** | **Fractal apply inside Engine** — `Stage::EngineInner` × { QuickStartConsonant, QuickConsonant, Tone, Modifier, FreeMarking, AutoUO, CustomKeyMap } as `IEngineRule`. Same shape, smaller scope. **CONDITIONAL** | Wave 4 + perf data | High — touches 1µs Tier-1 hot path | Engine benchmark p99 unchanged; 1903+ gtest pass |

### Architecture review Phase 5 (HookEngine class split) — **vaporized**

After Waves 2-4 extract three large features (Backward Edit, Commit-Undo, Macro), `HookEngine.cpp` drops naturally from ~3563 LOC to estimated ~1800 LOC (thin orchestration + mailbox host + LL hook install/uninstall). Class split as a separate phase is no longer needed — extraction is the split.

### Sequencing principle (anh 2026-05-22)

> "framework cần đi từ lớn tới bé, từ rộng tới hẹp, từ chung tới detail"

Outer pipeline (Hook-level Coordinator) first → Engine-internal rules last. Waves 1-6 are outer; Wave 7 is inner.

---

## 6. Ship-first decision

**Wave 0 alone fixes the `hiệu→hiêj` bug** (the user-visible pain that prompted this brainstorm). Wave 0 = histogram + mailbox + de-god probe close. Can ship without any framework extraction.

This means the framework skeleton (Wave 1) and the first extraction (Wave 2 BackwardEdit) can be paced calmly without user pain pressure. **Recommended ship cadence**: Wave 0 this week; Wave 1 + 2 next; one feature per wave thereafter.

---

## 7. Out of scope for this design

These are real architectural concerns anh raised, deferred to separate brainstorms:

1. **File layout — UI ↔ config co-location.** Today `src/app/ui/settings/` and `src/core/config/` are far apart. Reasonable refactor, but mechanical and orthogonal to feature pipeline. → Brainstorm after Wave 2 ships.
2. **TSF DLL parallel pipeline.** `EngineController.cpp` (608 LOC) duplicates parts of `HookEngine`. Could share Coordinator, but TSF lifecycle/edit-session constraints differ. → Decide after Wave 6 replay harness covers Hook side.
3. **Per-feature perf budget.** Once histogram (Wave 0) is up, each feature in the manifest can declare a budget (e.g. `<2µs at p99`). Coordinator enforces / logs. → Defer to Wave 6.

---

## 8. Open follow-ups

All four chốt 2026-05-22 — no follow-ups remain pre-implementation.

| # | Question | Resolution |
|---|---|---|
| 1 | Coordinator class lives where? | **`src/core/pipeline/`** — engine-portable; TSF DLL can reuse later when EngineController parallel pipeline is brainstormed |
| 2 | Feature registration: compile-time array vs runtime register-on-Init | **Runtime register-on-Init.** Tắt feature trong config = không register = coordinator không loop qua. Matches Pattern F (Pattern F = "OFF must mean 0 cost"). Virtual call overhead amortized by gate filter |
| 3 | Wave 7 engine internal rules split into `IEngineRule` | **Split, confirmed** |
| 4 | First feature after Backward Edit | **Commit-Undo** — highest conflict count in memos (`commit_undo_synth_guard_exemption` 2-cancel-site, `commit_undo_space` MapVirtualKey), single-feature ownership fixes both as side effect |

---

## Appendix A — Verified code locations

| Claim | File:Line | Verified |
|---|---|---|
| `ReplaceComposition` scattered | `HookEngine.cpp:3488` + 6 callers | ✓ |
| `ProcessKeyDown` dispatch already partially decomposed (H1 outcome enum) | `HookEngine.cpp:973-1052` | ✓ |
| `PushChar` 8-10 step if/return chain | `TypingEngine.cpp:106-380` | ✓ |
| Focus reset wipes engine | `HookEngine.cpp:3429 OnFocusChanged` | ✓ |
| Bug `hiệu→hiêj` log | Issue #178 comment 4516291282 | ✓ |
| `TypingAction` enum already exists | `TypingAction.h` (Path G G-3) | ✓ |
| `IOutputInjector` already exists (foundation for Output Channel) | `src/app/output/IOutputInjector.h` | ✓ |
| 6 conflict patterns + memos cross-reference | Memory `project_*` files | ✓ |

## Appendix B — How this differs from review 2026-05-19

| Review 2026-05-19 axis | This design axis |
|---|---|
| Thread ownership + state RCU | Feature ownership + plugin shape |
| Single-writer composition state | Single-owner state still applies — engine remains owner; feature plugins read via view, never mutate |
| Class split (Phase 5) as deliberate refactor | Class split as natural emergence from feature extraction |
| Replay harness for concurrency | Reused as-is; tests feature plugins under interleaving |
| Per-stage histogram | Reused as-is; per-feature budget proposed for Wave 6 |

**Compose, do not replace.** Wave 0 of this roadmap = Phase 1+2 of review 2026-05-19. Wave 5 = Phase 3. Wave 6 = Phase 4. Phase 5 (class split) is absorbed into Waves 2-4.

## Appendix C — Decisions confirmed by anh 2026-05-22

1. De-god probe Phase 1 verification standard: Windows build pass + gtest pass (chaos run not required to close probe).
2. Sóng 7 (fractal apply inside engine) included in roadmap, not deferred to "future possibility".
3. Engine keeps state ownership; Coordinator reads through const view; intent flow one-way to OutputChannel. Sync cost = 0.
4. Coordinator class location: `src/core/pipeline/` (engine-portable, no Win32 dependencies — testable on Linux gtest; TSF DLL can reuse later).
5. Feature registration: runtime register-on-Init driven by config. Tắt feature = không register = coordinator không loop qua. Matches Pattern F (OFF = 0 cost).
6. Extraction order after BackwardEditFeature: CommitUndoFeature is #2 (resolves `commit_undo_synth_guard_exemption` + `commit_undo_space` memos as side effect of single-owner state).
