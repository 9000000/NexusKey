# Wave 8.1 — AdjacentCircumflexProposal — extract circumflex modifier sub-handler

**Date**: 2026-05-25
**Status**: Plan v3 — post round-3 review (anh committed to long-term W8 series; W8.0 audit split out)
**Branch**: `feat/architecture-review-v3.1`
**Predecessor**: W7.4 QuickConsonantRules (HEAD `d070a62`); +`c6369dd` (adjacent ValidPrefix branch fix). Linux 2040/2040; Windows chaos last verified 54/55 clean.
**Direct predecessor**: **W8.0 UserDefined audit PR** (planned, see §9). W8.1 only kicks off after W8.0 lands clean.
**Goal**: Extract `TypingEngine::HandleAdjacentCircumflex` (both adjacent + free-marking branches) into an engine-rule plugin using "wrap don't lift", **one atomic commit**, no behavior delta. First wave of W8 series that extends W7 pattern down into modifier sub-handlers.

## W8 series commitment (round 3)

Anh's direction (2026-05-25): *"dài hạn, kiến trúc cần đúng với triết lý thiết kế và coding rule"*. W8.1 alone is not justified per round-3 review — `+145 LOC` for a "named home" is poor ROI. The commitment is to **W8 series as a whole**:

| Wave | Scope | Est. effort |
|---|---|---|
| **W8.0** | UserDefined audit + fix (was STOP-1 in v2) — standalone bug-risk PR | ~0.5 day |
| **W8.1** | AdjacentCircumflexProposal (this plan) | ~1.5 day |
| **W8.2** | HornModifierProposal(s) — extends `RelocationKind::HornVowel`; uses `RelocationKind` enum extension TODO 2026-05-23 already sketched | ~2 days |
| **W8.3** | BreveModifierProposal (P7 standalone-a) | ~0.5 day |
| **W8.4** | StrokeDProposal + BracketProposal (`[`/`]`) | ~0.5 day |
| **W8.5** | VNI proposals (Circumflex 6, Breve 8, Horn 7) — mirrors Telex via parity tests | ~1 day |
| **W8.6** | retro + ADR doc (per W7 AD-2 retro pattern) | ~0.5 day |
| **Total** | | **~6.5 days** |

**If team cannot commit to W8.2+, fall back to Plan 1-5** (per-callsite incremental fix) from the parent design doc `2026-05-25-engine-operations-contract-design.md`. W8.1 alone is not a valid stopping point.

## Revision log

### v2 → v3 (2026-05-25, round-3 review)

| Round 3 feedback | Resolution |
|---|---|
| §11 `canonical location for metadata` could read as "metadata enforces" | Renamed to **"non-authoritative metadata"** + explicit "documentation only, behavior lives in body" |
| `RelocationKind::Conditional` too vague — without documenting the condition, metadata is "near worthless for audit" | Added full enum doc-comment: explicit listing of which branch relocates and which doesn't. Tests pin the mapping. |
| STOP-1 should be standalone PR before W8.1 | Promoted to **W8.0** in new sequencing table. AD-2 sub-feature split exception applies (independent bug-risk audit, not scaffold-half-baked). |
| W8.1 alone +145 LOC not justified | Added W8 series commitment table — W8.1 is foundation only if W8.2-5 follow. Doc explicit "fall back to Plan 1-5 if no W8.2+ commitment". |

### v1 → v2 (2026-05-25, teammate review)

| Finding | Resolution |
|---|---|
| `RelocationKind::TargetTone` không uniform — adjacent có conditional relocate, free-marking unconditional | Demote `acceptanceOnValid()` + `relocationKind()` to **declarative metadata** (not enforcement). Document explicitly. Tests pin values. Future W8.x may revisit when patterns stabilize. |
| "Compile-time enforced" overclaim | Doc reframes as "declarative metadata + test-pinned". Removed "compile-time" claims. |
| Dispatch location is `ProcessModifier:698`, not `HandleModifierAction:486` | §4 corrected. Plan now replaces case at `ProcessModifier`. |
| CMake refs use `NEXTKEY_ENGINE_SOURCES` (L27) + `NEXTKEY_TEST_SOURCES` (L534), not VKeyApp/VKeyLite directly | §2 corrected. |
| `acceptanceOnValid = Reject` too coarse (`aa` from `{a}` Valid still accepts to `â`) | Removed `acceptanceOnValid()` from contract. Replaced with internal comment explaining behavior. The actual reject-vs-accept decision lives in `WouldBeValidSyllable` mod-only path on Valid pre-state, NOT in proposal-class-level constant. |
| `HandleAdjacentCircumflex` doesn't need to be public for override | Stays private. `IModifierSubExecutor` declares pure virtual; TypingEngine overrides as private member. Verified C++ legal. |
| Open question 5 (UserDefined `ActionToVowel` vs `WouldModifierRecoverOrEscape`'s `lower`) | Promoted from "open question" to **§9 stop condition** with explicit test requirement. |

---

## 0. Strategic decision

### TWO branches, ONE proposal class (not two)

| Option | Verdict |
|---|---|
| A: Split adjacent + free-marking into 2 proposal classes | Rejected — branches share the same `targetBase` resolution, same `ShouldRejectModifier`/`WouldBeValidSyllable` plumbing, same exit (`last.mod = Circumflex; [optional] RelocateToneToTarget()`). Splitting fragments shared state-detection logic. |
| **B: ONE `AdjacentCircumflexProposal` with both branches inside** | **Chosen.** Both branches answer the same question ("given an a/e/o modifier key, mutate the right vowel in the right way") just with different scan strategies (`last` vs `backward-scan`). Same proposal contract (acceptanceOnValid, relocationKind) applies uniformly. |
| C: Split per `TypingAction` (CircumflexA/E/O × 3) | Rejected — these only differ by `targetBase` lookup. No structural difference. |

### Wrap-don't-lift target

Extract via `IModifierSubExecutor` (new interface), NOT by lifting body into the proposal class. Body stays on `TypingEngine` because:
- It mutates `states_`, `escape_`, calls `WouldModifierKeyMatchExclusion` (private), `WouldBeValidSyllable` (private), `RelocateToneToTarget` (private).
- Per W7 retro AD-1: friending the plugin would bloat engine public surface; executor interface IS the contract.

### NEW: ModifierProposal shape (declarative metadata only)

> **v2 note:** Original v1 framed proposal methods as compile-time-enforced
> contract. Teammate review correctly flagged this as overclaim — methods
> are virtual runtime + `tryApply()` delegates to engine body, so the
> proposal class doesn't actually drive the speculation/apply path. v2
> reframes the methods as **declarative metadata** (documentation +
> test-pinning), not enforcement.

`ModifierProposal` base exposes:
1. **`tryApply(action, c) → bool`** — the only enforcement-bearing method. Delegates to executor (engine body).
2. **Optional metadata methods (DOCUMENTATION ONLY):**
   - `relocationKind()` describing the modifier's *intent* (Adjacent: conditional/None or TargetTone depending on pre-state; FreeMarking: TargetTone; future Horn: HornVowel).
   - **`acceptanceOnValid()` removed in v2** — too coarse (e.g. `aa` from `{a}` Valid still accepts to `â`). Real acceptance decision lives in `WouldBeValidSyllable` mod-only path validation on Valid pre-state, not as a class-level constant.

`relocationKind()` for AdjacentCircumflex returns a **`Conditional`** variant indicating "depends on pre-state — see body". This is honest about the v1 problem the teammate flagged. Tests verify the value matches body behavior across both branches.

The invariant ad09f15 ("speculate path mirrors apply path") still lives in `HandleAdjacentCircumflex` body's branch logic (per-state if/else) plus `WouldBeValidSyllable` comment block. v2 does **not** claim the proposal abstraction enforces this — that's still a comment-level invariant. The contribution of W8.1 is **organizational** (a name + a place for future proposals to land), not enforcement.

---

## 1. Scope

### Ships

- `src/core/engine/rule/IModifierSubExecutor.h` — executor port for modifier sub-handlers (1 method initially: `HandleAdjacentCircumflex`).
- `src/core/engine/rule/ModifierProposal.h` — abstract base class with `acceptanceOnValid()` + `relocationKind()` contract.
- `src/core/engine/rule/AdjacentCircumflexProposal.{h,cpp}` — first concrete proposal. Wraps adjacent + free-marking branches.
- TypingEngine inherits `IModifierSubExecutor`; ctor wires the proposal into `HandleModifierAction` dispatch.
- `HandleAdjacentCircumflex` body in TypingEngine.cpp: stays. Becomes the executor method body.
- 1 new gtest file (8-10 cases covering both branches × pre-state × acceptance).

### Does NOT ship

- Other modifier sub-handlers (HornW P1-P8, StrokeD, VniCircumflex, etc.) — those are W8.2-W8.5.
- Changes to ModifierRule (W7.3) — it still dispatches by TypingAction; only the body of `HandleModifierAction` for CircumflexA/E/O routes through the proposal.
- Removal of `WouldBeValidSyllable` mod-only path — still needed for proposals that opt into `relocationKind() == None`.

---

## 2. Files

| Path | Status | LOC | Note |
|---|---|---|---|
| `src/core/engine/rule/ModifierProposal.h` | new | ~50 | Abstract base + enums |
| `src/core/engine/rule/IModifierSubExecutor.h` | new | ~25 | Executor port |
| `src/core/engine/rule/AdjacentCircumflexProposal.h` | new | ~35 | Concrete proposal |
| `src/core/engine/rule/AdjacentCircumflexProposal.cpp` | new | ~20 | Delegates to executor |
| `src/core/engine/TypingEngine.h` | modify | +5 | Inherit IModifierSubExecutor (private override OK) + 1 proposal member |
| `src/core/engine/TypingEngine.cpp` | modify | ~0 net | Replace `HandleAdjacentCircumflex(...)` direct call inside `ProcessModifier` (L698) with `adjacentCircumflexProposal_.tryApply(...)`. Body stays. |
| `tests/engine/AdjacentCircumflexProposalTest.cpp` | new | ~200 | 10-12 cases incl. UserDefined remap |
| `CMakeLists.txt` | modify | +4 | Add proposal sources to `NEXTKEY_ENGINE_SOURCES` (L27); test source to `NEXTKEY_TEST_SOURCES` (L534) |

**Net code change:** +145 LOC infra, ~0 LOC engine body delta (body stays put per wrap-don't-lift).

---

## 3. Interface sketch

### ModifierProposal (abstract base, v2)

```cpp
namespace NextKey::EngineRule {

// NON-AUTHORITATIVE METADATA — documents the modifier's relocation intent.
// NOT enforced at the class level. Behavior lives in the executor body
// (TypingEngine::HandleXxx). Tests pin these values to detect copy-paste
// drift between future proposals. Audit value depends on these doc-comments
// matching what the body actually does — keep them in sync.
enum class RelocationKind : uint8_t {
    // Runtime never calls any RelocateToneTo* function after applying the
    // modifier. Tone stays on its current vowel. Example: Breve P7
    // (standalone-a → ă) in HandleHornW.
    None,

    // Runtime unconditionally calls RelocateToneToTarget() after applying
    // the modifier. Examples:
    //   - Free-marking circumflex (HandleAdjacentCircumflex L1057).
    //   - Adjacent circumflex when pre-state is ValidPrefix (post-c6369dd
    //     L962, gated by needsRelocate).
    //   - VNI vowel modifier (ProcessVniVowelModifier L1928).
    TargetTone,

    // Runtime calls RelocateToneToHornVowel() — a DIFFERENT function from
    // RelocateToneToTarget (separate target-finding logic for horn cluster).
    // Example: Horn P5/P6 in HandleHornW (W8.2 will land this).
    HornVowel,

    // Runtime decides per pre-state, spanning multiple kinds above.
    // Used today only by AdjacentCircumflexProposal because its body has
    // two branches with different relocation behavior:
    //   - Adjacent branch (L905-963): TargetTone IFF pre-state == ValidPrefix
    //     (per c6369dd needsRelocate gate); None IFF pre-state == Valid or
    //     Invalid (mod-only typo guard).
    //   - Free-marking branch (L984-1057): always TargetTone after apply.
    // Auditors checking "speculate mirrors apply" should read both branches.
    // Future refactor MAY split AdjacentCircumflex into 2 proposals (one per
    // branch) to eliminate Conditional, but v3 keeps them unified per §0
    // decision.
    Conditional,
};

class ModifierProposal {
public:
    virtual ~ModifierProposal() = default;

    // Declarative metadata — DOCUMENTATION + test-pinning only. Not enforced.
    [[nodiscard]] virtual RelocationKind relocationKind() const noexcept = 0;

    // The only behavior-bearing method. Delegates to executor; engine body is
    // source of truth for the actual speculation/apply/relocate sequence.
    [[nodiscard]] virtual bool tryApply(TypingAction action, wchar_t c) = 0;
};

}  // namespace NextKey::EngineRule
```

**v2 design note:** `acceptanceOnValid()` removed (v1 → v2). The reject-vs-accept decision on Valid pre-state is not a class-level constant: it's a per-call result of `WouldBeValidSyllable` mod-only validation, which depends on whether applying the modifier produces a Valid or Invalid syllable. Encoding it as a constant would force every concrete proposal to lie about edge cases (`aa` from `{a}` → accept; `aa` from `{c,ủ,a}` → reject). Better to let the body decide.

### IModifierSubExecutor (port to TypingEngine)

```cpp
class IModifierSubExecutor {
public:
    virtual ~IModifierSubExecutor() = default;

    // Adjacent-circumflex handler (existing body, unchanged).
    [[nodiscard]] virtual bool HandleAdjacentCircumflex(TypingAction action, wchar_t c) = 0;

    // Future W8 waves add: HandleHornW, HandleStrokeD, HandleVniCircumflex, ...
};
```

### AdjacentCircumflexProposal

```cpp
class AdjacentCircumflexProposal final : public ModifierProposal {
public:
    explicit AdjacentCircumflexProposal(IModifierSubExecutor& exec) noexcept : exec_(exec) {}

    // Metadata: adjacent branch is CONDITIONAL on pre-state (ValidPrefix → relocates;
    // Valid → mod-only no relocate). Free-marking always relocates. Single
    // class spans both branches → Conditional captures the union.
    RelocationKind relocationKind() const noexcept override { return RelocationKind::Conditional; }

    bool tryApply(TypingAction action, wchar_t c) override {
        return exec_.HandleAdjacentCircumflex(action, c);
    }

private:
    IModifierSubExecutor& exec_;
};
```

### TypingEngine wiring

```cpp
// In TypingEngine.h — IModifierSubExecutor override stays PRIVATE
class TypingEngine final : public EngineRule::IModifierSubExecutor,
                           public EngineRule::IModifierExecutor,
                           public EngineRule::IToneExecutor,
                           public EngineRule::IQuickConsonantExecutor {
    // ... existing public surface unchanged
private:
    // Existing private; now also overrides IModifierSubExecutor.
    bool HandleAdjacentCircumflex(TypingAction action, wchar_t c) override;
    EngineRule::AdjacentCircumflexProposal adjacentCircumflexProposal_{*this};
};

// In ProcessModifier (TypingEngine.cpp:691+), replace L698:
//   case TypingAction::CircumflexO: return HandleAdjacentCircumflex(action, c);
// With:
//   case TypingAction::CircumflexO: return adjacentCircumflexProposal_.tryApply(action, c);
//
// (Same edit for CircumflexA + CircumflexE cases at L696-697.)
```

The `tryApply()` call returns true/false identical to the old direct call. **Behavior delta: zero.**

---

## 4. Before / after pattern comparison

### Before W8.1 (current `c6369dd`)

```cpp
// TypingEngine.cpp:691 — ProcessModifier dispatch
bool TypingEngine::ProcessModifier(TypingAction action, wchar_t c) {
    switch (action) {
        case TypingAction::CircumflexA:
        case TypingAction::CircumflexE:
        case TypingAction::CircumflexO:
            return HandleAdjacentCircumflex(action, c);  // direct call to private
        // ... more cases
    }
}
```

Speculate/apply invariant lives only in comments at `HandleAdjacentCircumflex` body + at `WouldBeValidSyllable`.

### After W8.1

```cpp
// TypingEngine.cpp:691 — ProcessModifier dispatch
bool TypingEngine::ProcessModifier(TypingAction action, wchar_t c) {
    switch (action) {
        case TypingAction::CircumflexA:
        case TypingAction::CircumflexE:
        case TypingAction::CircumflexO:
            return adjacentCircumflexProposal_.tryApply(action, c);  // via proposal
        // ... more cases
    }
}
```

`HandleAdjacentCircumflex` body stays unchanged on TypingEngine (private virtual override).

### What this wave DOES contribute (v2 realistic claim)

W8.1 is **organizational scaffolding**:
- New file location for circumflex-modifier logic dispatch (`AdjacentCircumflexProposal.{h,cpp}`).
- Declarative metadata (`relocationKind() == Conditional`) makes intent visible.
- Pattern slot for W8.2 (Horn), W8.3 (Breve), W8.5 (VNI) to extend.

### What this wave does NOT contribute (v2 acknowledged)

- **Not** compile-time enforcement of speculate-mirrors-apply. That invariant still lives in comments + body branch logic.
- **Not** a behavior change. Tests pass byte-identical pre/post.
- **Not** a fix for the underlying mismatch between adjacent (conditional relocate) and free-marking (unconditional relocate). Both still live in same body. v2 acknowledges this honestly via `RelocationKind::Conditional`.

---

## 5. Test plan

### `tests/engine/AdjacentCircumflexProposalTest.cpp`

10-12 cases covering behavior + metadata + UserDefined edge case:

| Case | Input | Pre-state | Expected | Verifies |
|---|---|---|---|---|
| `Vijeet_Adjacent_PromotesViaValidPrefix` | `vijeet` | ValidPrefix | `việt` | adjacent + ValidPrefix branch |
| `Cuara_Adjacent_RejectsTypoOnValid` | `cuara` | Valid (`của`) | `cua` + literal `a` | adjacent + Valid → reject (mod-only validator catches) |
| `Susata_FreeMarking_PromotesViaValidPrefix` | `susata` | ValidPrefix | `suất` | free-marking + ValidPrefix branch |
| `Aa_Standalone_AcceptsOnValid` | `aa` | Valid `{a}` | `â` | proves "acceptanceOnValid=Reject" v1 framing was wrong — Valid pre-state CAN accept |
| `Caa_PrefixedAdjacent` | `caa` | Valid `{c,a}` | `câ` | with consonant prefix |
| `Vieetj_Canonical_NoToneAtTime` | `vieetj` | ValidPrefix at ee | `việt` | canonical "tone last" path |
| **`UserDefined_QMapsToCircumflexA_FollowsAction`** | UserDefined: `q → CircumflexA`. Buffer `{c,a}`. Press `q`. | Valid | `câ` | **Stop-condition test** — verifies `HandleAdjacentCircumflex`'s `ActionToVowel(action)` lookup works when `q` is non-vowel keyChar. Must NOT escape via `WouldModifierRecoverOrEscape` (which uses `lower == 'q'` ≠ 'a' target). |
| **`UserDefined_QMapsToCircumflexA_NoEscapeOnDoublePress`** | UserDefined: same map. Press `q` then `q`. | Valid `{c,â}` (after first q) | depends on intended UserDefined escape behavior | Pins current behavior; flags if dispatch-vs-escape gate diverges. |
| `Relocation_Conditional_Metadata` | n/a | n/a | n/a | `proposal.relocationKind() == RelocationKind::Conditional` (metadata pin) |
| `Identical_Output_Pre_Vs_Post_Refactor` | replay 15-20 inputs from existing fixtures | n/a | byte-identical | golden test sanity |

**v2 changes:**
- Removed `Acceptance_Reject_Property` test (no `acceptanceOnValid()` in v2 contract).
- Renamed `Relocation_TargetTone_Property` → `Relocation_Conditional_Metadata` (honest about adjacent branch's conditional nature).
- **Added 2 UserDefined cases** — directly addresses teammate Finding 7. Without these the W8.1 commit cannot land per §9.

### Existing tests that MUST still pass (regression guard)

- `TelexEngineTest::Word_Viet` (`vieetj`)
- `TelexEngineTest::ToneMidSmartAccentTest::*` (4 cases incl. `Vijeet_ToneBeforeSmartAccent`, `Ngufoon_*`)
- `TelexEngineTest::ToneMidSmartAccentTest::Cuarw/Hoaw/Muaw_*` (3 horn cases — must NOT regress; they go through HandleHornW, not adjacent circumflex, but cross-check that W8.1 didn't accidentally touch the horn path)
- `CircumflexFreeMarkSpellOnTest::SuatPlusA_PromotesToSuat` (ad09f15)
- `PhonotacticsValidatorVCPairTest::AdjacentHeuristic_*` (6 cases)

Total regression surface: ~15-20 tests touch this path. All must pass byte-identical.

---

## 6. Verification

### Linux (build + test)

```bash
cmake --build build-linux --target VKeyTests -j 4
./build-linux/tests/VKeyTests --gtest_filter="*AdjacentCircumflex*:*TelexEngineTest*:*PhonotacticsValidator*"
./build-linux/tests/VKeyTests  # full suite — must hit 2040+ pass
```

### Windows chaos (post-merge)

Run `run-chaos.ps1` on 4 hosts (Notepad, Word, Chrome, Edge). Expected: 54/55 PASS (matches W7.4 baseline).

### Atomic commit verification

`git show <W8.1 commit>` must contain:
- ALL new files
- ALL modified files
- ZERO TODO/FIXME markers
- Test results pasted in commit body

Per W7 retro AD-2: no intermediate "scaffold then activate" — single coherent commit.

---

## 7. Atomic commit shape

```
refactor(engine): W8.1 lift AdjacentCircumflex into ModifierProposal pattern

Extends W7 wrap-don't-lift pattern down into modifier sub-handlers (W7.3
landed at HandleModifierAction outer dispatch; sub-handlers like
HandleAdjacentCircumflex were left as god-method branches).

W8.1 introduces:
  - ModifierProposal abstract base with acceptanceOnValid() +
    relocationKind() contract.
  - IModifierSubExecutor port (TypingEngine implements).
  - AdjacentCircumflexProposal (first concrete proposal).

Behavior delta: zero. HandleAdjacentCircumflex body stays on TypingEngine
unchanged from c6369dd. Only dispatch route changes from direct call to
proposal.tryApply.

Why now: the speculate/apply invariant (ad09f15) currently lives in
comments at WouldBeValidSyllable. Reframing the per-modifier policy as
class-level constants (AcceptanceOnValid::Reject + RelocationKind::TargetTone
for circumflex) makes it compile-time visible. Future W8.2-W8.5 (HornW,
VNI, etc.) follow the same shape — each proposal declares its policy in
one place, can't drift across callsites.

Tests: 2050/2050 pass. 10 new in AdjacentCircumflexProposalTest.cpp.
Windows chaos: 54/55 (4 hosts, all clean).

Refs:
  - docs/plans/2026-05-25-feature-pipeline-w8.1-adjacent-circumflex-proposal-plan.md
  - W7 retro: docs/plans/2026-05-23-feature-pipeline-w7-retro.md AD-1
```

---

## 8. Open questions before kickoff (v2 — Q5 promoted to §9)

1. **`ModifierProposal` namespace:** `NextKey::EngineRule` (alongside existing rule classes) or new `NextKey::ModifierProposal`? Lean toward `EngineRule` for cohesion with W7 outputs.

2. **Proposal storage:** member of TypingEngine (sketched here) or registered via a `ModifierProposalRegistry`? For W8.1 (1 proposal), member is sufficient. Revisit at W8.3 when 3+ proposals exist.

3. **`IModifierSubExecutor` granularity:** 1 interface with N methods (one per sub-handler) OR N small interfaces? W7 chose granular (`IToneExecutor`, `IModifierExecutor`, `IQuickConsonantExecutor`). Lean toward 1 `IModifierSubExecutor` because the methods share semantics; revisit if a sub-handler needs distinct interface for testing.

4. **Property test value:** With v2 metadata-only framing, the `Relocation_Conditional_Metadata` test verifies `relocationKind() == Conditional`. Trivially true post-construction. Keep as documentation of intent + future copy-paste catch.

5. ~~Ordering with `WouldModifierRecoverOrEscape` audit.~~ → **PROMOTED TO §9 STOP CONDITION** per teammate review. Audit required before commit, not after.

6. **Effort estimate refinement:** v1 estimated ~1-1.5 day. v2 simplification (drop `acceptanceOnValid`) shaves some plan complexity but adds 2 UserDefined tests + audit task. Net: ~1.5 days unchanged.

---

## 9. Stop conditions (when to abort W8.1) — v2 strengthened

### STOP-1: ~~UserDefined audit~~ → Now standalone W8.0 PR (round 3 decision)

v2 framed UserDefined audit as a STOP condition inside W8.1. Round-3 review
flagged this should be **a separate PR landing before W8.1**, justified by
W7 retro AD-2 sub-feature-split exception (independent bug-risk audit, not
scaffold-half-baked).

**W8.0 outline (separate plan to write):**

| Item | |
|---|---|
| **Scope** | Audit `WouldModifierRecoverOrEscape` (`TypingEngine.cpp:583-621`) vs `HandleAdjacentCircumflex` target-resolution (`TypingEngine.cpp:900-903`) in UserDefined mode. |
| **Mismatch hypothesis** | UserDefined remap `q → CircumflexA`. `HandleAdjacentCircumflex` uses `ActionToVowel(action) = 'a'`. `WouldModifierRecoverOrEscape` uses `IsVowelChar('q') = FALSE → escape gate skipped`. Recovery semantics may diverge. |
| **Probe tests** | `UserDefinedAuditTest::QMapsToCircumflexA_TargetResolution` — verify Telex `caa → câ` works equivalent to UserDefined `caq → câ`. `UserDefinedAuditTest::QMapsToCircumflexA_EscapeBehavior` — verify `qq` double-press behavior matches Telex `aa` semantics OR documents intentional divergence. |
| **Fix shape (if bug)** | `WouldModifierRecoverOrEscape` switches to action-target resolution for UserDefined mode (or both modes — TBD by probe results). |
| **Stop conditions for W8.0 itself** | If probes show no bug, ship commit titled `test(engine): pin UserDefined-vs-Telex action-target parity` with just the tests. If probes show bug, `fix(engine): WouldModifierRecoverOrEscape uses action target for UserDefined mode`. |
| **Effort** | ~0.5 day |

W8.1 cannot start until W8.0 lands. If W8.0 probes reveal a bug, W8.1 also
needs to incorporate the action-target resolution change into proposal contract.

### STOP-2: Metadata test reveals body inconsistency

If `Relocation_Conditional_Metadata` test passes trivially but the **`Identical_Output_Pre_Vs_Post_Refactor`** golden test (replay 15-20 inputs) shows ANY byte delta → **PAUSE**. Refactor was supposed to be behavior-neutral.

### STOP-3: Commit grows past ~500 LOC

W7 retro AD-2 caps atomic-commit scope. If W8.1 + 2 UserDefined tests + audit fix balloons past 500 LOC → split per W7 AD-2 exception. Sub-feature split allowed (e.g. UserDefined `WouldModifierRecoverOrEscape` fix can be PR-before-W8.1).

### STOP-4: Adjacent vs free-marking shape diverges enough to need 2 proposals

If during implementation it becomes clear the 2 branches really shouldn't share a class (e.g. test setup for free-marking is markedly different from adjacent) → revisit §0 Option A vs B. v1 chose B (one class). v2 keeps B but acknowledges this is a judgment call.

---

## 10. Why W8.1 first (not Horn / VNI)

| Reason | |
|---|---|
| Fix `c6369dd` just landed here; freshest in head | ✓ |
| Smallest scope (1 sub-handler, 2 branches inside it) — good baseline for proposal pattern | ✓ |
| 17-20 existing regression tests = safety net | ✓ |
| If pattern doesn't fit cleanly, abort here costs least | ✓ |
| Horn (W8.2) needs `RelocationKind::HornVowel` infrastructure — W8.1 establishes the enum (incl. `Conditional`) without forcing horn to land same day | ✓ |

## 11. v2 reality check (post-review)

What W8.1 ACTUALLY delivers vs v1's claim:

| v1 claim | v2 reality |
|---|---|
| "Compile-time enforced contract" | Declarative metadata, not enforcement |
| "Speculate path mirrors apply path becomes class invariant" | Still a comment-level invariant in body; class doesn't enforce |
| "Per-modifier acceptance policy in class constants" | `acceptanceOnValid()` removed; reject-vs-accept decision stays in `WouldBeValidSyllable` mod-only path |
| Single `RelocationKind` per proposal | `RelocationKind::Conditional` introduced to honestly describe adjacent's per-pre-state behavior |

**v3 honest framing:** W8.1 contributes **organizational structure**, not new enforcement. The file `AdjacentCircumflexProposal.{h,cpp}` becomes a documented home for **non-authoritative metadata** about circumflex modifier dispatch — `RelocationKind::Conditional` plus doc-comments describing the body's actual behavior. **The metadata does not enforce anything.** Behavior continues to live in `HandleAdjacentCircumflex` body. Tests pin the metadata-to-body mapping; if either drifts, tests catch it.

Future W8.2-5 follow the same shape — each proposal exposes documented metadata + tests pin. The cumulative win is across the whole W8 series:
- Centralized audit surface (`grep RelocationKind:: src/core/engine/rule/` shows the modifier policy map at-a-glance).
- Parity tests across Telex/VNI proposals enforced by class structure (W8.5 must declare same metadata as W8.1 for circumflex; mismatch surfaces in code review as a 2-line class diff).
- Onboarding cost: future contributors read 7 proposal classes instead of one 280-LOC god-method.

**If team cannot commit to W8.2+** (per round-3 framing), fall back to **Plan 1-5** in the parent design doc `2026-05-25-engine-operations-contract-design.md` — per-callsite incremental patch under the existing pattern. **W8.1 alone is not a valid stopping point** per round-3 review.
