# Feature Pipeline Framework — W7 Retrospective + Architecture Decisions

**Date**: 2026-05-23
**Branch**: `feat/architecture-review-v3.1`, HEAD `d070a62`
**Scope**: closure document for Waves 1-7 of the feature-pipeline framework rollout

> **Purpose of this doc**: future-Anh / future-Claude touching engine-rule code (W8+) should read this *first*. It's the "why" behind the framework. Wave plan docs (`2026-05-22-feature-pipeline-w*.md`, `2026-05-23-feature-pipeline-w*.md`) are the "what".

---

## 1. Wave history (recap)

| Wave | Ship date | Commit | Linux | Windows chaos | Scope |
|---|---|---|---|---|---|
| W1 skeleton | 2026-05-22 | (pre-summary) | — | — | `IFeature` + `Coordinator` + `KeyContext` + `IntentSink` + `Stage` enum |
| W2 BackwardEdit | 2026-05-22 | (pre-summary) | — | 54/55 | First HookEngine feature (PostEngine prio 10), wrap-don't-lift via executor |
| W3 CommitUndo | 2026-05-22 | (pre-summary) | — | 54/55 | PreEngine prio 20 |
| W4a EscRestoreRaw | 2026-05-23 | (pre-summary) | — | 54/55 | PreEngine prio 40 |
| W4b Macro | 2026-05-23 | (pre-summary) | — | 54/55 | PreEngine prio 30 |
| W5 gates wired | 2026-05-23 | `c03f724` | 1977 | (no consumer, no chaos) | SpellCheckGate + ToneEscapeGate live reads via engine_ ref |
| W7.1 engine skeleton | 2026-05-23 | `8bdac55` | 1984 | (no consumer) | `IEngineRule` + `EngineRuleRegistry` + Pre/PostClassify phases |
| W7.2 ToneRule | 2026-05-23 | `63436b7` | 1990 | 44/44 real hosts | First engine rule. First consumer of ToneEscape gate. |
| W7.3 ModifierRule | 2026-05-23 | `3fe9c09` | 1996 | 54/55 ("PASS: all hosts clean") | Wrap `HandleModifierAction` |
| W7.4 QuickStart + QuickEnd | 2026-05-23 | `d070a62` | 2010 | 54/55 ("PASS: all hosts clean") | Last extraction. 2 rules. `FinalizeRegularChar` helper. |
| **W7.5 retro** | 2026-05-23 | (this commit) | — | — | This doc + small cleanup |

> W6 (replay harness for focus/config/injector interleave) **was skipped** — anh chose W7 fractal apply directly. W6 remains a viable future wave; see §4.

**Net code change** across all waves:
- HookEngine: 4 new features lift out of inline dispatch (~250 LOC moved to plugin classes).
- TypingEngine: `PushChar` shrunk from ~210 LOC to **~60 LOC skeleton**. All 4 extractable sub-blocks (0a quick-start, 0a-cont undo, 0b mid-word, 2c quick-end) + tone + modifier are now plugins.
- 9 new engine-rule files (interfaces + rules).
- ~50 new gtests (1956 → 2010).
- Zero behavior regression on 4 real Windows hosts across 4 chaos verifications.

---

## 2. Architecture Decisions

### AD-1: Wrap-don't-lift via per-feature executor interface

**Decision**: Each feature lifts to a plugin class that delegates the actual work to an `IXxxExecutor` interface, which `HookEngine` / `TypingEngine` implements as a method on itself.

**Alternatives rejected**:
- **Full lift**: move feature body into the plugin class. Rejected because the body mutates engine private state (escape_, qc_, states_, engProt_); friending the plugin to the engine would bloat the engine's public surface, and a "narrow handle" interface adds machinery nothing else uses today.
- **Free functions** taking engine ref: rejected because the namespace/visibility story is murky (private methods can't be called from non-friends).

**Why wrap-don't-lift wins**:
- One commit per feature (anh's "đừng refactor tới lui" rule).
- Reverts cleanly: drop the plugin class, restore the inline call.
- The executor interface IS the contract. Plugin tests can stub the executor; integration tests use the real `TypingEngine`.
- Engine private members stay private; the executor method is the controlled public surface.

**When to break this rule**: if a plugin's body would mutate engine state in a way the engine doesn't already expose, AND the mutation is sufficiently complex that the executor signature would balloon (>5 args, or returning a structured intent), **then** full-lift with a narrow handle is justified. **Today**, no feature requires this.

---

### AD-2: Atomic single-commit waves — no "scaffold then activate"

**Decision**: Each wave ships in 1 atomic commit. No intermediate states where the plugin exists but returns `Pass`-only.

**Origin**: anh's feedback during W7.2 planning: *"đừng để phải refactor kiến trúc tới lui nhiều"*. The earlier plan had 3 commits (extract method → add plugin returning Pass → flip to Veto). Rejected.

**Alternatives rejected**:
- **Scaffold then activate** (3-commit sequence): half-baked intermediate state, doubles review burden, easier to introduce bugs in transitional commits no one reads carefully.
- **Multi-PR waves**: same problem at PR scope.

**Why this wins**:
- Each commit is a coherent "end shape". You can `git checkout` any commit and the engine works.
- Bisect-friendly.
- "Code looks like the architecture intended" at every point in history.

**Cost**: bigger commits. Acceptable — W7.4 was the largest at ~930 LOC including tests, and it's still a single coherent unit.

**Exception**: legitimately independent sub-features within a wave can split (e.g., W4a Esc + W4b Macro were 2 commits — different features). The rule prohibits *scaffold→activate of the same feature*, not all multi-commit waves.

---

### AD-3: Engine-layer gate evaluation is local, not via Coordinator

**Decision**: `EngineRuleRegistry::EvaluateGates` reads engine state directly from `EngineRuleContext` fields (bias, spellCheckDisabled, escapeActive). It does **not** call into `Pipeline::Coordinator::EvaluateGates`.

**Alternatives rejected**:
- **Share Coordinator's gate eval**: rejected because Coordinator's snapshot is taken at HookEngine `HandleKey` entry. By the time `TypingEngine::PushChar` runs (post-classify, post-PreClassify rule dispatch), the engine state may have drifted (e.g., PreClassify QuickStartConsonantRule may have flipped `engProt_.bias`). Engine-layer rules MUST see fresh state.
- **Pass `IInputEngine&` to rules** for them to re-evaluate gates per-rule: rejected because (a) duplicates the per-key cost (each rule re-reads), (b) breaks the "rule declares Requires, framework filters" contract.

**Why local eval wins**:
- Cheap: 3 bit-ops over fields already loaded into ctx.
- Fresh: reflects state as of dispatch time, not HookEngine entry time.
- Decoupled: engine layer doesn't depend on Pipeline::Coordinator at runtime (only on the `GateId` enum, a leaf header).

---

### AD-4: Reuse `Pipeline::GateMask` at engine layer (no new enum)

**Decision**: Engine rules declare `Requires()` using `Pipeline::GateMask` + `Pipeline::GateId` from the existing HookEngine-layer headers. No engine-specific gate enum.

**Alternatives rejected**:
- **`EngineRule::GateMask` separate enum**: rejected — semantic gates (EnglishBias, SpellCheck, ToneEscape) are the same concept at both layers. Two enums would mean two places to add a new gate.
- **Lift `GateMask` to a shared root namespace** (e.g., `NextKey::Gates`): rejected — would force renaming all Pipeline-layer code with no value-add today. If a third layer ever needs gates, revisit.

**Why reuse wins**:
- One source of truth.
- Adding a new gate = add 1 enum value, used by both layers.
- Engine layer's runtime dependency on `core/pipeline/GateMask.h` is leaf-only (no `.cpp` link, no `Coordinator` link).

---

### AD-5: `EngineRuleContext` is POD-by-const-ref — no mutable carrier

**Decision**: Rules receive `EngineRuleContext const&`. They cannot mutate ctx fields. To mutate engine state, the rule calls executor methods (or directly mutates via the `TypingEngine&` passed to `Apply`).

**Alternatives rejected**:
- **Mutable `keyChar` in ctx**: rejected during W7.4 planning — would have let 0b's cc→ch path communicate the rewritten keyChar back to PushChar. But it breaks the contract every other rule (Tone, Modifier) relies on (`const&`). One feature's convenience would tax every other reader.
- **Output channel struct**: rejected — added machinery nothing else needs.

**Why immutable ctx wins**:
- Reads are obvious: `ctx.keyChar` is what the user typed, period.
- Mutations are explicit: the rule calls a named executor method, which makes the side-effect surface visible.
- Easy to reason about: ctx is a snapshot, not a black box.

**Knock-on effect**: W7.4's `FinalizeRegularChar` helper exists precisely because of this decision. 0b's cc→ch path couldn't mutate ctx.keyChar, so the rule does `ProcessChar(newKey) + FinalizeRegularChar() + return Veto` — equivalent net effect, with mutation isolated to the executor body.

---

### AD-6: Two phases (PreClassify, PostClassify) — sufficient

**Decision**: `EngineRule::Phase` has exactly two values. Rules running before `TypingAction` is computed go in `PreClassify`; rules running after go in `PostClassify`.

**Alternatives rejected**:
- **Single-phase flat dispatch**: rejected — would force every rule to peek at `ctx.action == TypingAction::None` or similar to decide whether it should run. Cleaner to use the phase boundary directly.
- **N phases (PreInput / PreClassify / PostClassify / PreCommit / PostCommit)**: rejected — speculation. The current 4 rules + the planned ones all fit cleanly in 2 phases.

**When to add a phase**: if a future rule legitimately needs to run *between* PostClassify dispatch and step-3 `ProcessChar` (e.g., a rule that mutates `action` based on engine state — unlikely), then a third phase is justified. Until then, 2 phases is the truthful end-shape.

---

### AD-7: State (qc_, quickStartKey_, escape_, engProt_) stays on `TypingEngine`

**Decision**: Engine-internal state stays on the engine. Rules access it via the engine ref passed to executor methods.

**Alternatives rejected**:
- **Move per-feature state into the rule class**: e.g., put `qc_` into `QuickStartConsonantRule`. Rejected — `Backspace`, `Reset`, `IsRestoreCandidate` (non-rule code paths) all touch `qc_`. Pulling it into the rule would force those methods to take a rule pointer. Strictly worse encapsulation.
- **Engine state as ctx fields by-value**: rejected — would force a snapshot copy on every PushChar. Reference-back-to-engine is cheaper.

**Why this wins**:
- Single state owner (TypingEngine), single mutation surface.
- Rules are stateless plugins — easier to reason about, easier to test.

---

### AD-8: Sub-block split along phase boundary (W7.4 specific)

**Decision**: Quick-consonant logic split into TWO rules: `QuickStartConsonantRule` (PreClassify) for 0a/0a-cont/0b, `QuickEndConsonantRule` (PostClassify) for 2c.

**Alternatives rejected**:
- **One rule registered in both phases**: framework requires `RulePhase()` to return one value. Would need either hack-registering the same class twice (with a phase parameter), or a new "MultiPhase" enum value. Rejected — bad framework citizen.
- **One rule with internal phase branching**: rejected — Apply() body becomes a maze of "if I'm in PreClassify do X else do Y". Splitting along the boundary the framework already uses is cleaner.

**Why split wins**:
- Each rule has one phase, matches framework shape exactly.
- Each rule's Apply() body is short and single-purpose.
- File names self-document where in the dispatch pipeline they run.

**Lesson for future rules**: if a feature's semantic territory crosses a phase boundary, it should split. Don't try to unify across phases — phases are the framework's existing seams.

---

### AD-9: Keep unused engine-layer gate bits (no premature cleanup)

**Decision**: `EngineRuleRegistry::EvaluateGates` continues to compute the EnglishBias and SpellCheck bits even though no current rule declares `Requires=` either. The bits remain in the GateMask.

**Alternatives rejected**:
- **Remove the unused bit assignments**: would be "honest code" today but creates a dilemma: if W8 adds a rule that needs them, we'd `git revert` this commit. The "refactor tới lui" cost outweighs the ~3ns per-dispatch savings.

**Why keep**:
- Cost is paid (3 bit-ops in a function that already runs per dispatch).
- Removal is reversible but adds churn; reversing the removal is also churn.
- The GateMask enum is shared with HookEngine layer, where SpellCheck and EnglishBias gates DO have consumers (SpellCheckGate / EnglishBiasGate read by `Coordinator::EvaluateGates`). Removing engine-side eval would create lopsided semantics.
- The `EngineRuleRegistry.cpp` `EvaluateGates` function carries a comment block explaining the kept-but-unused bits (added W7.5).

---

### AD-10: `FinalizeRegularChar` extraction pattern

**Decision**: When wrap-don't-lift needs to call the "tail" of a function (e.g., the step-3 post-`ProcessChar` work in PushChar), extract that tail into a private method on the engine. Both the original site and the executor body call the new method.

**Origin**: W7.4 needed it because 0b's cc→ch path historically `mutated keyChar and fell through to step 3`. Pure lift would have needed either a mutable ctx (rejected by AD-5) or duplicating step-3 inline in the executor (rejected — fragmentation).

**Alternatives rejected**:
- **Duplicate the code inline**: violates "không code phân mảnh".
- **Make step 3 a public method**: wider visibility than needed.

**Why extraction wins**:
- One source of truth for the tail logic.
- The extraction itself is pure code motion (verified by diffing).
- Method name documents intent (`FinalizeRegularChar` > a bare comment).

**General lesson**: if a wrap-don't-lift wave finds it can't faithfully reproduce behavior without a tail refactor, the tail extraction should be in the SAME commit as the wave (per AD-2 atomic ship).

---

## 3. Final architecture state (engine layer)

```
TypingEngine::PushChar  (~60 LOC skeleton)
├─ rawInput_.push_back + escRawHistory_.push_back + qc_.onlyQC = false
├─ build EngineRuleContext (keyChar, lower, isUpper, ...)
├─ ruleRegistry_.DispatchAtPhase(PreClassify)
│    └─ QuickStartConsonantRule (prio 5, Requires=0)
│         └─ HandleQuickStartConsonant — 0a / 0a-cont / 0b (Veto on consume, Pass on fall-through)
├─ VNI digit sequence detection
├─ ClassifyKey → TypingAction action
├─ ruleRegistry_.DispatchAtPhase(PostClassify)
│    ├─ ToneRule (prio 10, Requires=ToneEscape)
│    │    └─ HandleToneFsm — Clear/ApplyTone* actions
│    ├─ ModifierRule (prio 20, Requires=0)
│    │    └─ HandleModifierAction — Telex / VNI / UserDefined modifiers
│    └─ QuickEndConsonantRule (prio 30, Requires=0)
│         └─ HandleQuickEndConsonant — 2c g→ng / h→nh / k→ch
└─ ProcessChar(keyChar, lower, isUpper) + FinalizeRegularChar()
```

**Gate consumers**:
- ToneEscape: ToneRule ✓
- SpellCheck: none at engine layer (HookEngine `SpellCheckGate` at Pipeline layer has live read but no Feature consumer either today)
- EnglishBias: none at engine layer (logic embedded inside `HandleToneFsm` / `HandleModifierAction` as direct `engProt_.bias` checks, not as gate-skip)

---

## 4. Open follow-ups

| # | Item | When |
|---|---|---|
| F1 | **W6 — replay harness for focus/config/injector interleave**. Skipped to ship W7 first. Test infrastructure, not user-facing. Worth picking up if/when a chaos.toml expansion needs deterministic replay. | Defer |
| F2 | **Pipeline-layer gate cleanup**: SpellCheckGate, EnglishBiasGate are live reads but have no Feature consumer. Same situation as engine-layer (AD-9). Keep until a Feature lands that needs them, or W9+ cleanup wave. | Defer |
| F3 | **Engine rule mutation surface formalization**: today rules mutate via `TypingEngine&` cast as a wide friend. If a future rule needs only a narrow subset (e.g., "just push a CharState"), a `IEngineRuleHandle` façade could narrow the surface. Premature today. | Defer until needed |
| F4 | **Per-rule perf budget**: Coordinator has no per-feature timing. Engine rules don't either. If chaos p99 ever regresses, add `PerfHistogram` instrumentation per rule. | Defer until needed |
| F5 | **Documenting the HookEngine-layer pattern** in the same style: this retro covers engine layer in detail. HookEngine layer (W1-W4) has its own design doc (`2026-05-22-feature-pipeline-framework-design.md`) but no equivalent ADR. Consider writing one if the HookEngine framework ever needs maintenance. | Defer |

---

## 5. Recipe — adding a new engine rule (W8+)

Follow this checklist. Match W7.2/W7.3/W7.4's shape exactly. Atomic single commit per AD-2.

### Step 1 — Decide the rule's contract
- **Phase**: PreClassify or PostClassify? (AD-6)
- **Priority**: ToneRule=10, ModifierRule=20, QuickEnd=30. Pick a slot. Leave gaps for future rules.
- **Requires**: which gates? Be honest — only declare what you actually need (AD-9 says don't speculate).
- **What the rule consumes**: which `EngineRuleContext` fields does it read? Any new fields needed? (Avoid adding new ctx fields — prefer reading via engine ref.)

### Step 2 — Create the executor interface
Path: `src/core/engine/rule/IXxxExecutor.h`. Pattern from `IToneExecutor.h`:
```cpp
class IXxxExecutor {
public:
    virtual ~IXxxExecutor() = default;
    [[nodiscard]] virtual <ReturnType> HandleXxx(<args>) = 0;
};
```
Return type: `bool` for simple Veto-on-consume rules; `EngineRule::Result` if you need tri-state (Pass / Veto / Handled).

### Step 3 — Create the rule class
Path: `src/core/engine/rule/XxxRule.{h,cpp}`. Pattern from `ToneRule`:
```cpp
class XxxRule final : public IEngineRule {
public:
    explicit XxxRule(IXxxExecutor& exec) noexcept : exec_(exec) {}
    Phase    RulePhase() const noexcept override { return Phase::<chosen>; }
    int      Priority()  const noexcept override { return <chosen>; }
    GateMask Requires()  const noexcept override { return <gate mask>; }
    Result   Apply(const EngineRuleContext& ctx, TypingEngine& engine) override;
private:
    IXxxExecutor& exec_;
};
```
Apply body: short-circuit on non-relevant actions, then delegate to `exec_.HandleXxx(...)` and map return to `Result::Veto` / `Result::Pass`.

### Step 4 — Add the executor method to TypingEngine
- Add `public EngineRule::IXxxExecutor` to TypingEngine's base list.
- Declare `HandleXxx(...) override` in TypingEngine.h (public).
- Implement body in TypingEngine.cpp. **Lift verbatim** from PushChar (or wherever the inline logic lives today).
- If the lift requires a "tail" refactor (like W7.4's `FinalizeRegularChar`), extract the tail into a private helper IN THE SAME COMMIT (AD-10).

### Step 5 — Register the rule + remove the inline dispatch
- In TypingEngine ctor: `ruleRegistry_.Register(std::make_unique<EngineRule::XxxRule>(*this));`
- In PushChar (or wherever): delete the inline `if (HandleXxx(...)) return;` block. The PostClassify (or PreClassify) dispatch already exists — it'll route to your new rule automatically once registered.

### Step 6 — Write tests
Path: `tests/engine/XxxRuleTest.cpp`. Pattern from `ToneRuleTest.cpp`:
- Mock executor (`CountingXxx`) implementing `IXxxExecutor`.
- 4-6 cases minimum: metadata, non-target-action Pass, target-action Veto, target-action Pass-on-false (if executor can return false), gate-skip if Requires!=0, registry priority order vs adjacent rules.

### Step 7 — CMakeLists
Add new sources to `NEXTKEY_ENGINE_SOURCES` and test file to `NEXTKEY_TEST_SOURCES`.

### Step 8 — Verify
- Linux: `cmake --build build-linux --target VKeyTests -j$(nproc) && ./build-linux/tests/VKeyTests` — all pass.
- Windows chaos (if rule touches hot path): anh runs `tools\run-chaos.ps1` on 4 real hosts (notepad / notepadpp / discord / gpt). Chrome's 1.3-escape-bs-aa-b false-fail is documented; ignore.

### Step 9 — Commit
1 commit, atomic. Commit message includes phase / priority / gate / what was lifted. See W7.2/W7.3/W7.4 commits for the canonical shape.

### Step 10 — Update memory
- `project_feature_pipeline_framework_2026-05-22.md` — add the new wave's section at the top.
- `MEMORY.md` — update the framework entry's HEAD + test count.

**Don't**: ship intermediate "plugin returns Pass-only" commits (AD-2). Don't add mutable fields to `EngineRuleContext` (AD-5). Don't lift engine state into the rule (AD-7).

---

## 6. Closing notes

The framework is **done at the extraction level**. PushChar is the skeleton anh asked for. New behavior can land as new rules; existing behavior is plugin-shaped. The pattern proved stable across 4 atomic waves with 0 regression.

The discipline that mattered most:
1. **"Đừng refactor tới lui"** (AD-2). 4 waves × 1 commit each. No half-baked states.
2. **"Không code phân mảnh"**. Wrap-don't-lift instead of half-extract. Tail helper extraction when needed (AD-10), not inline duplication.
3. **Honest gates**. Requires=0 when no gate truly applies (AD-9). Don't fake-consume to validate the framework.

W7.5 closes the extraction phase. Future work (W8+) adds new rules using the recipe above.
