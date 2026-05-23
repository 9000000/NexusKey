# Wave 7.1 — IEngineRule interface + EngineRuleRegistry (skeleton, no rule extracted)

**Date**: 2026-05-23
**Branch**: `feat/architecture-review-v3.1`
**Predecessor**: W5 wired ToneEscapeGate + SpellCheckGate live (no consumer yet); HEAD `c03f724`; Linux 1977/1977; Windows chaos 54/55 (Chrome 1.3 known false-fail).
**Goal**: Bring the same plugin pattern *inside* `TypingEngine` — add `IEngineRule` + `EngineRuleRegistry` plus a single dispatch site inside `PushChar`. **Zero rules registered.** Empty registry is a measured no-op on the 1µs Tier-1 hot path. No rule extracted. W7.2 will register the first real rule (ToneRule).

---

## 0. Strategic decision — "wrap, don't lift" applies to W7.1 too

W7.1 is the engine-internal analog of W1 (skeleton, no consumer). Mirrors what shipped:

| HookEngine layer (W1) | Engine layer (W7.1) |
|---|---|
| `IFeature`, `Stage`, `Result`, `GateMask` | `IEngineRule`, `EngineRulePhase`, `EngineRuleResult` |
| `KeyContext` (vk/keyChar/mods + session view) | `EngineRuleContext` (keyChar/lower/isUpper/action + engine view) |
| `Coordinator` (registry + dispatch) | `EngineRuleRegistry` (registry + dispatch) |
| `IntentSink` (Intent batch out) | (none — engine rules mutate engine state directly via a controlled handle; no out-of-process intent flow at this layer) |
| `gates/*Gate.h` (reads engine via const ref) | `Requires(GateId)` — **reuses the same GateMask** as upstairs; rules can require `SpellCheck`/`ToneEscape` gates. Gate evaluation kept separate; engine layer evaluates its own gates from engine-internal state (`engProt_.bias`, `escape_.isEscaped()`, `spellCheckDisabled_`). See §3.5. |

**Why a skeleton wave**:
- No behavior change → byte-identical Linux + chaos.
- Lets W7.2 ship ToneRule in isolation against a stable interface.
- Lets us measure the empty-dispatch cost on a perf-critical path *before* any rule lands. If the cost is non-trivial, we redesign before extracting (cheap pivot).
- Matches "lan từ lớn → bé → detail" cadence: framework lands first, content next.

---

## 1. Scope

### W7.1 **ships**:
1. `src/core/engine/rule/IEngineRule.h` — interface (phase, priority, requires-gate, Apply()).
2. `src/core/engine/rule/EngineRuleContext.h` — per-key context handed to rules.
3. `src/core/engine/rule/EngineRulePhase.h` — phase enum (`PreClassify`, `PostClassify`).
4. `src/core/engine/rule/EngineRuleResult.h` — Pass / Handled / Veto enum.
5. `src/core/engine/rule/EngineRuleRegistry.{h,cpp}` — owns rules per-phase, dispatches in priority order, filters by gate mask.
6. **Two dispatch calls** wired into `TypingEngine::PushChar` (Phase::PreClassify at the very top after rawInput/escRawHistory push, Phase::PostClassify after step 1 — see §4 for exact line).
7. New gtest file `tests/engine/EngineRuleRegistryTest.cpp` — 7 cases (see §7).
8. CMake entries.

### W7.1 explicitly does **NOT** ship:
- Any extracted rule (no `ToneRule`, no `ModifierRule`, no `QuickConsonantRule`).
- Any change to existing TypingEngine behavior — empty registry means dispatch is a single empty-vector loop.
- New gate types (`Requires()` reuses existing `Pipeline::GateMask`).
- Linkage between Coordinator's gate evaluation and engine's — engine evaluates its own gates internally (cheaper; avoids cross-layer dependency).
- Windows chaos run as a separate gate — W7.1 is Linux-only behavior change (no Win32 surface touched). Chaos verification deferred to **W7.5**.

---

## 2. Files added/modified

| Path | Status | Approx. LOC |
|---|---|---|
| `src/core/engine/rule/EngineRulePhase.h` | new | 18 |
| `src/core/engine/rule/EngineRuleResult.h` | new | 16 |
| `src/core/engine/rule/EngineRuleContext.h` | new | 50 |
| `src/core/engine/rule/IEngineRule.h` | new | 35 |
| `src/core/engine/rule/EngineRuleRegistry.h` | new | 45 |
| `src/core/engine/rule/EngineRuleRegistry.cpp` | new | 55 |
| `src/core/engine/TypingEngine.h` | modify | +4 (member + accessor) |
| `src/core/engine/TypingEngine.cpp` | modify | +12 (two DispatchAtPhase calls in `PushChar`) |
| `tests/engine/EngineRuleRegistryTest.cpp` | new | ~180 |
| `CMakeLists.txt` | modify | +8 (sources + test entry) |

**Total added**: ~415 LOC. **HookEngine.cpp**: untouched. **TypingEngine.cpp**: 1926 → ~1938 LOC.

Note: `tests/engine/` directory doesn't exist yet (engine tests today live flat in `tests/`). Create it. CMakeLists entry follows the `tests/pipeline/` precedent.

---

## 3. Interface sketch

### 3.1 `EngineRulePhase.h`

```cpp
namespace NextKey::EngineRule {

enum class Phase : unsigned char {
    PreClassify  = 0,  // before TypingAction is computed — quick consonant, P8 'w' rewrite
    PostClassify = 1,  // after TypingAction is computed — tone, modifier dispatch
};

inline constexpr std::size_t kPhaseCount = 2u;

}  // namespace NextKey::EngineRule
```

**Why two phases (not one) at W7.1**:
- `PreClassify`: where QuickConsonantRule (cc→ch, uu→ươ, quick start consonant) will land in W7.4. These run BEFORE the `lower`/`isUpper`/`action` resolution.
- `PostClassify`: where ToneRule (W7.2) + ModifierRule (W7.3) will land — they read `action` (computed by `ClassifyKey`).

W7.1 wires both calls but registers zero rules in either bucket. Phase enum exists so W7.2 doesn't have to extend the enum and re-prove dispatch ordering.

### 3.2 `EngineRuleResult.h`

```cpp
namespace NextKey::EngineRule {

enum class Result : unsigned char {
    Pass     = 0,  // rule not relevant — engine continues to next rule / fallthrough
    Handled  = 1,  // rule mutated engine state and committed the keystroke; stop this phase
    Veto     = 2,  // skip remaining phases AND skip the rest of PushChar — engine returns
};

}
```

**Veto semantics differ from upstairs**: in Coordinator, Veto stops cross-stage dispatch (HookEngine still executes the OS path). Here, Veto means the engine should not run the rest of `PushChar` — the rule fully handled the key (e.g., quick-consonant return). This matches existing early `return` statements inside `PushChar`.

### 3.3 `EngineRuleContext.h`

POD-like, built once per `PushChar` call, passed by `const&` for read fields and by non-const `TypingEngine&` for mutating ops. Rationale: the engine layer's primitive abstraction is **state mutation**, not intent emission.

```cpp
namespace NextKey::EngineRule {

struct EngineRuleContext {
    wchar_t      keyChar;           // raw keystroke (case-preserved)
    wchar_t      lower;             // towlower(keyChar)
    bool         isUpper;           // iswupper(keyChar)
    TypingAction action;            // PostClassify only; TypingAction::None in PreClassify
    bool         spellCheckDisabled;
    bool         allowEnglishBypass;
    bool         escapeActive;      // escape_.isEscaped()
    LanguageBias bias;              // engProt_.bias
    bool         isVniDigitSeq;

    const std::vector<CharState>& states;
    const std::vector<wchar_t>&   rawInput;
    const TypingConfig&           config;
};

}  // namespace NextKey::EngineRule
```

**Mutation surface**: rules don't get a dedicated mutable handle in W7.1 because no rule exists. W7.2 introduces `EngineRuleHandle` (or just narrows `TypingEngine&`) when ToneRule's mutation needs are concrete. For W7.1, `IEngineRule::Apply` takes `(const EngineRuleContext&, TypingEngine&)`.

### 3.4 `IEngineRule.h`

```cpp
namespace NextKey::EngineRule {

class IEngineRule {
public:
    virtual ~IEngineRule() = default;

    [[nodiscard]] virtual Phase                       RulePhase() const noexcept = 0;
    [[nodiscard]] virtual int                         Priority()  const noexcept = 0;
    [[nodiscard]] virtual NextKey::Pipeline::GateMask Requires()  const noexcept = 0;

    [[nodiscard]] virtual Result Apply(const EngineRuleContext& ctx,
                                       TypingEngine& engine) = 0;
};

}
```

**`Requires()` reuses `Pipeline::GateMask`** — same bits (`SpellCheck = 1`, `ToneEscape = 2`). No new enum. ToneRule (W7.2) declares `Requires() = GateMaskFor(GateId::ToneEscape)`.

### 3.5 `EngineRuleRegistry.h`

```cpp
namespace NextKey::EngineRule {

class EngineRuleRegistry {
public:
    EngineRuleRegistry();
    ~EngineRuleRegistry();

    EngineRuleRegistry(const EngineRuleRegistry&)            = delete;
    EngineRuleRegistry& operator=(const EngineRuleRegistry&) = delete;

    void Register(std::unique_ptr<IEngineRule> rule);

    [[nodiscard]] Result DispatchAtPhase(Phase phase,
                                         const EngineRuleContext& ctx,
                                         TypingEngine& engine);

    [[nodiscard]] std::size_t RuleCountAtPhase(Phase p) const noexcept;
    [[nodiscard]] std::size_t RuleCount() const noexcept;

private:
    [[nodiscard]] NextKey::Pipeline::GateMask EvaluateGates(
        const EngineRuleContext& ctx) const noexcept;

    std::array<std::vector<std::unique_ptr<IEngineRule>>, kPhaseCount> rules_;
};

}
```

**Why a separate `EvaluateGates` from the Coordinator's**:
- Engine-layer rules see *engine-internal* gate state mid-`PushChar` — Coordinator's snapshot is stale by then.
- 3 bit-ops over already-loaded locals. Cheaper than calling into a `Coordinator`.
- Keeps engine layer independent of Pipeline runtime (depends only on `GateId` enum — a leaf header).

`EvaluateGates` body (sketch):

```cpp
GateMask EngineRuleRegistry::EvaluateGates(const EngineRuleContext& ctx) const noexcept {
    GateMask raised = 0u;
    if (ctx.bias == LanguageBias::HardEnglish && !ctx.allowEnglishBypass)
        raised |= GateMaskFor(GateId::EnglishBias);
    if (ctx.spellCheckDisabled && ctx.config.spellCheckEnabled && !ctx.allowEnglishBypass)
        raised |= GateMaskFor(GateId::SpellCheck);
    if (ctx.escapeActive)
        raised |= GateMaskFor(GateId::ToneEscape);
    return raised;
}
```

---

## 4. TypingEngine integration

### 4.1 New member

In `TypingEngine.h` (after `escRawHistory_`, before `config_`):

```cpp
EngineRule::EngineRuleRegistry ruleRegistry_;
```

Default-constructed; no public accessor in W7.1. W7.2 adds `RegisterEngineRule(std::unique_ptr<IEngineRule>)` when there's a rule to register.

### 4.2 Build the context

Inside `PushChar`, BEFORE the existing step 0a (`quickStartConsonant`), after the two `push_back` calls:

```cpp
const wchar_t lowerLocal = towlower(keyChar);
const EngineRule::EngineRuleContext ruleCtx{
    keyChar,
    lowerLocal,
    static_cast<bool>(iswupper(keyChar)),
    TypingAction::None,
    spellCheckDisabled_,
    config_.allowEnglishBypass,
    escape_.isEscaped(),
    engProt_.bias,
    /*isVniDigitSeq*/ false,
    states_,
    rawInput_,
    config_,
};
```

Lift the existing `lower = towlower(keyChar)` computation (currently inside the quick-consonant block at line 154) up to here so we have one call site.

### 4.3 Dispatch — PreClassify

Insert at TypingEngine.cpp around line 113 (right after `qc_.onlyQC = false;`):

```cpp
{
    const auto result = ruleRegistry_.DispatchAtPhase(
        EngineRule::Phase::PreClassify, ruleCtx, *this);
    if (result == EngineRule::Result::Veto) return;
}
```

### 4.4 Dispatch — PostClassify

Insert after line 232 (`if (isVniDigitSequence) action = TypingAction::None;`), BEFORE the `effectiveSpellCheck` computation:

```cpp
{
    EngineRule::EngineRuleContext postCtx = ruleCtx;
    postCtx.action             = action;
    postCtx.isVniDigitSeq      = isVniDigitSequence;
    postCtx.spellCheckDisabled = spellCheckDisabled_;
    postCtx.escapeActive       = escape_.isEscaped();
    postCtx.bias               = engProt_.bias;

    const auto result = ruleRegistry_.DispatchAtPhase(
        EngineRule::Phase::PostClassify, postCtx, *this);
    if (result == EngineRule::Result::Veto) return;
}
```

**Why re-snapshot**: PreClassify rules (W7.4 QuickConsonantRule) may have mutated `escape_`/`engProt_`/`spellCheckDisabled_`. PostClassify rules must see current state.

### 4.5 Why two dispatch calls (not one)

A single late-stage dispatch would force every rule to handle phase-discrimination itself. Two calls give each rule a clean "I run before/after classification" contract. Mirror of Coordinator's `HandleKeyAtStage`.

---

## 5. Hot path cost

### Empty-registry dispatch cost (W7.1, zero rules registered)

`DispatchAtPhase` body (empty bucket):

```cpp
GateMask raised = EvaluateGates(ctx);   // 3 bit-ops over cached locals
auto& bucket = rules_[static_cast<size_t>(phase)];  // 1 indexed load
for (auto& rule : bucket) { ... }       // empty — 0 iterations
return Result::Pass;
```

Per call:
- 1 indexed array access
- 3 boolean checks + ORs (`EvaluateGates`)
- 1 vector begin/end compare
- 1 enum return

Estimated **< 5ns per dispatch call**. Two calls per `PushChar` → **< 10ns added** to a function that today takes ~700ns–1µs. **< 1% overhead** at the empty-registry stage.

### Allocation budget
- `ruleRegistry_` member: `std::array<std::vector<std::unique_ptr<IEngineRule>>, 2>`. **Zero heap allocations** until `Register` is called.
- `EngineRuleContext`: stack-only POD.
- `DispatchAtPhase`: no allocation.

**W7.1 adds zero heap allocation on the hot path.**

### Comparator vs Coordinator

Coordinator currently dispatches 4 features across 2 stages per key. Engine layer at W7.1: 0 rules across 2 phases per key. Strictly cheaper than the existing Coordinator overhead.

---

## 6. Dispatch wiring summary

| Engine event | TypingEngine.cpp line (today) | W7.1 insertion |
|---|---|---|
| `PushChar` entry | 106 | (no change) |
| After `rawInput_/escRawHistory_` push | 112 | **Insert ruleCtx build** |
| After `qc_.onlyQC = false` | 113 | **Insert DispatchAtPhase(PreClassify)** |
| Quick-start consonant block | 116-130 | unchanged — W7.4 target |
| Quick consonant cc→ch / uu→ươ | 147-206 | unchanged — W7.4 target |
| TypingAction classification | 225-232 | unchanged |
| After classification | 232 → 233 | **Insert DispatchAtPhase(PostClassify)** |
| ClearTone | 243-253 | unchanged — W7.2 target |
| Tone processing | 256-345 | unchanged — W7.2 target |
| HandleModifierAction | 348-350 | unchanged — W7.3 target |

Net: 2 dispatch calls inserted, body unchanged.

---

## 7. Tests (`tests/engine/EngineRuleRegistryTest.cpp`)

Mirror of Coordinator tests. Mock rules implement `IEngineRule` + record invocation order.

| # | Test name | Assertion |
|---|---|---|
| 1 | `EmptyRegistry_DispatchIsNoOp_ReturnsPass` | No rules → `Result::Pass` for both phases |
| 2 | `Register_SortsByPriorityAscending_StablePreservesOrder` | Priorities 30,10,20 invoked as 10,20,30; equal priority preserves registration order |
| 3 | `Dispatch_RunsOnlyMatchingPhase` | PreClassify rule fires on PreClassify only |
| 4 | `Handled_StopsThisPhase_NextPhaseStillRuns` | Handled by R1 stops R2 in same phase; next phase rule R3 still runs |
| 5 | `Veto_StopsThisPhase_CallerReturnsImmediately` | Registry returns Veto; caller honors |
| 6 | `GateMask_SkipsRulesWithRaisedGate` | Rule requires ToneEscape; escapeActive=true → skip; false → fire |
| 7 | `EngineRuleContext_ReadsLiveBufferRefs` | ctx.states is a ref, not a copy |

**Linux test count target**: 1977 → 1984 (+7).

---

## 8. Commit plan

W7.1 = **2 commits**:

### Commit 1: `feat(engine): W7.1.1 — IEngineRule + EngineRuleRegistry skeleton`
- Add 6 new files in `src/core/engine/rule/`.
- Add `tests/engine/EngineRuleRegistryTest.cpp` with 7 tests.
- CMakeLists updates.
- TypingEngine unchanged. Run tests: 1977 → 1984 pass.

### Commit 2: `feat(engine): W7.1.2 — wire empty registry into TypingEngine::PushChar`
- Add `ruleRegistry_` member.
- Add ctx build + 2 `DispatchAtPhase` calls in `PushChar`.
- No new tests (behavior byte-identical). 1984 still pass.

---

## 9. Risks / open questions

| # | Item | Severity | Recommendation |
|---|---|---|---|
| R1 | Mutation interface deferred — W7.2 ToneRule needs to mutate `escape_`, `states_[i].tone`. | Medium | Defer to W7.2; let ToneRule's actual needs shape the façade. |
| R2 | Engine's `EvaluateGates` reads mid-`PushChar` state; Coordinator's reads HandleKey-entry state. They can disagree. | Low (intended) | Document in `IEngineRule.h`. |
| R3 | Is 2 phases right? | Medium | Yes — Pre needs `lower`, Post needs `action`. Cheap to flatten later. |
| R4 | Empty-registry benchmark might regress on some compilers (cache miss on `rules_`). | Low | Measure, then maybe flatten to one vector. Don't pre-optimize. |
| R5 | Future risk: ToneRule + a Pipeline ToneFeature both fire on same key. | High (future) | Out of W7.1 scope. Document the convention at W7.2: tone resolution lives engine-side. |
| **Q1** | Engine-local gate evaluation vs cross to Pipeline? | — | **Engine-local.** Confirm. |
| **Q2** | `DispatchAtPhase` direct or via TypingEngine method? | — | **Direct member access.** |
| **Q3** | Add `RuleCount()` alongside `RuleCountAtPhase`? | — | **Yes.** Trivial sum. |
| **Q4** | Linux-only verification, or include Windows build? | — | **Both build, no chaos run.** Chaos at W7.5. |

---

## 10. Done definition

- [ ] 1984/1984 Linux gtests pass.
- [ ] Windows build succeeds (Debug + Release) — no chaos run.
- [ ] 2 commits on `feat/architecture-review-v3.1`.
- [ ] `EngineBenchmarkTest` p99 within ±2% of pre-W7.1 baseline (advisory).
- [ ] No new MSVC /W4 /WX warning.
- [ ] Q1-Q4 resolved before W7.2.

---

## 11. What W7.2 needs from W7.1

1. Stable `IEngineRule` interface (RulePhase / Priority / Requires / Apply).
2. Stable `EngineRuleContext` field set.
3. Mechanism for the eventual `EngineRuleHandle` (R1) — W7.2 introduces.
4. Phase enum with `PostClassify` slot — W7.2 ToneRule registers there.
