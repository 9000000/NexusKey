# Wave 7.4 — QuickConsonantRule(s) — extract quick-consonant logic from PushChar

**Date**: 2026-05-23
**Branch**: `feat/architecture-review-v3.1`
**Predecessor**: W7.3 ModifierRule shipped (HEAD `3fe9c09`); Linux 1996/1996; Windows chaos 54/55 clean.
**Goal**: Extract the four "quick consonant" sub-blocks (0a quick-start, 0a-cont undo, 0b mid-word, 2c quick-end) from `TypingEngine::PushChar` into engine-rule plugins using "wrap don't lift", **one atomic commit**, no behavior delta.

---

## 0. Strategic decision — TWO rules, split by phase, no `keyChar` mutation in ctx

| Option | Verdict |
|---|---|
| A: ONE rule registered in both phases | Rejected — framework requires one `RulePhase()` per rule instance. Hack. |
| **B: TWO rules — `QuickStartConsonantRule` (PreClassify, prio 5) + `QuickEndConsonantRule` (PostClassify, prio 30)** | **Chosen.** Split along existing PreClassify/PostClassify axis. Start-rule owns 0a + 0a-cont + 0b. End-rule owns 2c. |
| C: Mutable `EngineRuleContext.keyChar` | Rejected — breaks POD-by-const-ref contract every other rule uses. Adds a write-back protocol nothing else needs. |

**0b state-mutation problem** (cc→ch mutates `keyChar` locals + falls through): solved by lifting the post-`ProcessChar` work (lines 290-321) into a new private helper `FinalizeRegularChar()`. The cc→ch path then `ProcessChar(replacement) + FinalizeRegularChar() + return Veto` — same net effect as today's mutate-and-fall-through.

**0a-cont fall-through** (clears + re-processes, doesn't return): rule returns `Result::Pass` after the work; PushChar continues to PostClassify dispatch + step 3 — same as today's fall-through.

---

## 1. Scope

### Ships
- `src/core/engine/rule/IQuickConsonantExecutor.h` — executor port (2 methods).
- `src/core/engine/rule/QuickStartConsonantRule.{h,cpp}` — PreClassify prio 5.
- `src/core/engine/rule/QuickEndConsonantRule.{h,cpp}` — PostClassify prio 30.
- TypingEngine inherits `IQuickConsonantExecutor`; ctor registers both rules.
- Private helper `TypingEngine::FinalizeRegularChar()` — extracts post-`ProcessChar` work.
- PushChar lines 140-231 + 275-287 deleted.
- 2 new gtest files (13 cases total).

### Does NOT ship
- Changes to `qc_`/`quickStartKey_` ownership (stays on TypingEngine — Backspace/Reset still touch them).
- Changes to `EngineRuleContext` shape.
- W5 gate consumption — Requires=0 for both (see §7).

---

## 2. Files

| Path | Status | LOC |
|---|---|---|
| `src/core/engine/rule/IQuickConsonantExecutor.h` | new | ~32 |
| `src/core/engine/rule/QuickStartConsonantRule.h/.cpp` | new | ~63 |
| `src/core/engine/rule/QuickEndConsonantRule.h/.cpp` | new | ~60 |
| `src/core/engine/TypingEngine.h` | modify | +12 |
| `src/core/engine/TypingEngine.cpp` | modify | net −10 (−105 inline +95 executor/helper/register) |
| `tests/engine/QuickStartConsonantRuleTest.cpp` | new | ~180 |
| `tests/engine/QuickEndConsonantRuleTest.cpp` | new | ~110 |
| `CMakeLists.txt` | modify | +6 |

---

## 3. Interface sketch

### IQuickConsonantExecutor

```cpp
class IQuickConsonantExecutor {
public:
    virtual ~IQuickConsonantExecutor() = default;

    // PreClassify entry. Tri-state because 0a-cont fall-through path returns Pass.
    [[nodiscard]] virtual Result HandleQuickStartConsonant(
        wchar_t keyChar, wchar_t lower, bool isUpper) = 0;

    // PostClassify entry. Bool — matches W7.2/W7.3 pattern.
    [[nodiscard]] virtual bool HandleQuickEndConsonant(
        wchar_t keyChar, wchar_t lower, bool isUpper) = 0;
};
```

### QuickStartConsonantRule

```cpp
class QuickStartConsonantRule final : public IEngineRule {
public:
    explicit QuickStartConsonantRule(IQuickConsonantExecutor& exec) noexcept : exec_(exec) {}
    Phase    RulePhase() const noexcept override { return Phase::PreClassify; }
    int      Priority()  const noexcept override { return 5; }
    GateMask Requires()  const noexcept override { return 0u; }
    Result   Apply(const EngineRuleContext& ctx, TypingEngine&) override {
        return exec_.HandleQuickStartConsonant(ctx.keyChar, ctx.lower, ctx.isUpper);
    }
private:
    IQuickConsonantExecutor& exec_;
};
```

### QuickEndConsonantRule

```cpp
class QuickEndConsonantRule final : public IEngineRule {
    // PostClassify, prio 30, Requires=0.
    // Apply pre-guards: !config.quickEndConsonant → Pass;
    //                   states.empty() || !states.back().IsVowel() → Pass;
    //                   lower != g/h/k → Pass;
    //                   else delegate to executor; Veto on true / Pass on false.
};
```

The 4-condition pre-guard in `Apply` is the hot-path optimization (§8) — for ~95% of keys this returns Pass without calling the executor.

---

## 4. Wiring

### ctor (TypingEngine.cpp):
```cpp
ruleRegistry_.Register(std::make_unique<EngineRule::ToneRule>(*this));
ruleRegistry_.Register(std::make_unique<EngineRule::ModifierRule>(*this));
ruleRegistry_.Register(std::make_unique<EngineRule::QuickStartConsonantRule>(*this));
ruleRegistry_.Register(std::make_unique<EngineRule::QuickEndConsonantRule>(*this));
```

### PushChar lines REMOVED
| Lines | What |
|---|---|
| 140-231 | 0a + 0a-cont + 0b (92 LOC) |
| 275-287 | 2c (13 LOC) |
| **Total** | **105 LOC deleted from PushChar** |

### PushChar lines KEPT
- 117-138: rawInput push + escRaw push + qc_.onlyQC=false + ctx build + PreClassify dispatch
- 233-243: `isVniDigitSequence` detection
- 245-257: action classification
- 261-270: PostClassify dispatch
- 289-321: step 3 — but lines 290-321 collapsed to `ProcessChar(keyChar); FinalizeRegularChar();`

### `FinalizeRegularChar()` private helper
Lifts lines 290-321 verbatim (`RelocateToneToTarget` + `ApplyAutoUO` + `UpdateSpellState` + `CheckEnglishBias` + P8 revert + `CheckZwjfInitialBias`). Called from:
- PushChar step 3 (after `ProcessChar(keyChar)`)
- `HandleQuickStartConsonant` 0b cc→ch path (after `ProcessChar(newKey, ...)`)

Pure refactor — no behavior change.

---

## 5. Executor body sketch

`HandleQuickStartConsonant` consolidates 0a + 0a-cont + 0b. Lifted verbatim with one change: cc→ch path no longer mutates caller's locals — calls `ProcessChar(newKey, ...) + FinalizeRegularChar() + return Veto`.

`HandleQuickEndConsonant` is verbatim 2c body — `if (lower==g/h/k) ProcessChar(first); ProcessChar(second); UpdateSpellState(); return true; else return false;`.

State (`qc_`, `quickStartKey_`) accessed directly — executor body IS a TypingEngine method.

---

## 6. State ownership

`qc_` and `quickStartKey_` stay on TypingEngine. Backspace (line 1547-1581), Reset (line 1678-1680), `IsRestoreCandidate` (line 1615-1624), and the auto-restore path (line 1636-1639) all read/write them. Pulling them into the rule would force passing the rule to Backspace/Reset — strictly worse encapsulation.

---

## 7. Gate decision — Requires=0

Verified by code read of lines 116-130 / 134-145 / 147-231 / 275-287:
- No `spellCheckDisabled_` check anywhere
- No `engProt_.bias` check
- No `escape_.isEscaped()` check (`qc_.escaped` is a different flag set by Backspace)

**Requires=0 is the truthful end-shape.** W5 SpellCheckGate stays unconsumed at engine layer; W7.5 retro decides whether to keep or remove. Forcing `Requires=SpellCheck` here would be a behavior change (quick-consonant currently fires when spell-check disabled — gating would suppress it).

---

## 8. Hot path cost

QuickConsonantRule runs on **every key** — unlike Tone/Modifier rules.

| Path | Cost |
|---|---|
| PreClassify QuickStart (no fire) | ~11ns (vcall + branches in executor) |
| PostClassify QuickEnd (no fire) | ~3ns (Apply pre-guard returns Pass without vcall) |
| **Total added per PushChar** | **~14ns** (~1.5-2% of 700ns-1µs PushChar) |

Zero new heap allocations. Within W7.1's per-rule budget.

---

## 9. Tests

### QuickStartConsonantRuleTest.cpp — 8 cases
1. Metadata: PreClassify, prio 5, Requires=0
2. Apply delegates Pass result
3. Apply delegates Veto result
4. Integration: 'f' → states=['p','h'], quickStartKey_=='f'
5. Integration: 'j','t' → states=['j','t'] (undo path)
6. Integration: 'c','c' → states=['c','h'], qc_.lastKey=='c'
7. Integration: 'l','u','u' → states ends in [ư,ơ]
8. Registry order: PreClassify dispatch hits start-rule, doesn't touch ToneRule mock

### QuickEndConsonantRuleTest.cpp — 5 cases
1. Metadata: PostClassify, prio 30, Requires=0
2. Pre-guard: no vowel tail → no executor call
3. Pre-guard: key not g/h/k → no executor call
4. Apply: states=[a], 'g' → states=[a,n,g], Veto
5. Registry order: Tone(10) → Modifier(20) → QuickEnd(30) priority chain

**Total**: 13 new tests. 1996 → 2009.

---

## 10. Commit plan

**ONE commit.** No scaffold-then-activate.

```
feat(engine): W7.4 — extract quick-consonant logic into QuickStart/QuickEnd rules

Two new rules (PreClassify:5 + PostClassify:30). Wraps 0a + 0a-cont + 0b +
2c via IQuickConsonantExecutor. PushChar shrinks ~105 lines. Adds private
FinalizeRegularChar helper (pure refactor lifting step-3 tail) so 0b's
cc→ch can finalize without leaking keyChar mutation via ctx. State (qc_,
quickStartKey_) stays on TypingEngine. Requires=0 — no W5 gate maps to
any quick-consonant sub-block by code read. 13 new gtests; 1996 → 2009.
```

---

## 11. Risks / open questions

| # | Item | Severity | Recommendation |
|---|---|---|---|
| R1 | 0b cc→ch path order: today `mutate locals → fall through to step 3`. New: `ProcessChar(newKey) → FinalizeRegularChar() → Veto`. Same function calls, same order. | Medium | Run full engine gtest after change. |
| R2 | 0a-cont rawInput_ bookkeeping: clears + re-pushes savedKey + pushes keyChar. Today same. | Low | Test #5 verifies `rawInput_=[f,t]` (size 2). |
| R3 | `FinalizeRegularChar` extraction spans English-bias + P8 + ZWJF. Re-order risk. | Medium | Diff lines 290-321 character-by-character vs new function body. |
| R4 | `qc_.escaped/lastKey` bookkeeping happens unconditionally in executor body; Pass return path still clears them — same as today. | Low | Test re-trigger scenario. |
| R5 | QuickEnd prio 30 after Modifier prio 20. Today 2c only runs when modifier didn't fire (because modifier returned). Same logic. | Low | Test #5 verifies dispatch order. |
| **Q1** | **Rule split: TWO rules (PreClassify start + PostClassify end) vs forcing one?** | — | **TWO.** Confirm before commit. |
| **Q2** | **Requires=0 leaves SpellCheckGate unconsumed at engine layer?** | — | **Yes per code read.** Confirm; W7.5 retro decides gate fate. |
| Q3 | `FinalizeRegularChar` extraction is pure refactor — same commit OK? | — | Yes — pure code motion, no behavior change, minimum scaffolding for 0b. |

---

## 12. Done

- 2009/2009 Linux gtests pass
- Windows chaos 54/55+ maintained
- One commit on `feat/architecture-review-v3.1`
- Smoke: typing `chuyenje`, `dd→đ`, `aa→â`, `cc→ch`, `uu→ươ`, `vng→vông`, `lh→lnh`, f/j/w-start preserves byte-identical
- p99 within ±2% baseline
- Q1 + Q2 confirmed by anh

---

## 13. W7.5 inherits

- PreClassify bucket: 1 rule (QuickStartConsonantRule)
- PostClassify bucket: 3 rules (Tone:10, Modifier:20, QuickEnd:30)
- All four extractable PushChar sub-blocks now plugins
- PushChar body = framework + ctx build + classify + step 3 only
- Unconsumed gates: SpellCheckGate, EnglishBiasGate — W7.5 retro decides

W7.4 is the **last extraction wave**. W7.5 = chaos + retro + gate cleanup.
