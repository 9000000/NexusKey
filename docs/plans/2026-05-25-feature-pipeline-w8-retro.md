# Feature Pipeline Framework — W8 Retrospective + Architecture Decisions

**Date**: 2026-05-25
**Branch**: `feat/architecture-review-v3.1`, HEAD `0a181d2`
**Scope**: closure document for Wave 8 of the feature-pipeline framework rollout — ModifierProposal pattern extends W7's wrap-don't-lift down into modifier sub-handlers.

> **Purpose of this doc**: future-Anh / future-Claude touching modifier dispatch (W9+ extending Proposal pattern, or adding new modifier actions) should read this *first*. Pairs with `2026-05-23-feature-pipeline-w7-retro.md` (W7 retro — outer dispatch refactor) and `2026-05-25-engine-operations-contract-design.md` (W8 design brainstorm).

---

## 1. Wave history (W8 series)

| Wave | Ship date | Commit | Linux | Scope |
|---|---|---|---|---|
| **W8.0 audit** | 2026-05-25 | `1f2e76f` | 2048 | 8 probes for `WouldModifierRecoverOrEscape` Telex/UserDefined parity. Verdict: no user-visible bug; Telex branch is key-driven (vs VNI action-driven) but self-correcting via `HandleAdjacentCircumflex` targetBase check. |
| **W8.1 Adjacent** | 2026-05-25 | `e39a35b` | 2061 | First `ModifierProposal` — `AdjacentCircumflexProposal` wraps `HandleAdjacentCircumflex` (CircumflexA/E/O). `relocationKind() == Conditional`. |
| **W8.2 Horn** | 2026-05-25 | `3803f1d` | 2083 | `HornModifierProposal` wraps `HandleHornW` (P1-P8). `relocationKind() == HornVowel`. Scaffold-only after 8 probes confirmed no concrete repro for the TODO 2026-05-23 speculate-relocate mismatch hypothesis. |
| **W8.3 StrokeD** | 2026-05-25 | `b623e74` | 2090 | `StrokeDProposal` wraps `HandleStrokeD` (Telex `dd` / VNI `d9`). `relocationKind() == None`. **Re-scoped** from plan v3's "BreveProposal P7" (which sat inside HornW, already wrapped by W8.2). |
| **W8.4 Bracket** | 2026-05-25 | `ad451cc` | 2099 | `BracketProposal` wraps `HandleHornInsert` (Telex `[`/`]`). `relocationKind() == None`. |
| **W8.5 VNI + fix** | 2026-05-25 | `0a181d2` | 2118 | Three VNI proposals (Circumflex 6, Horn 7, Breve 8) + **real bug fix** for `ProcessVniVowelModifier` Pass 1: VNI mirror of c6369dd. Resolves TODO 2026-05-25. |
| **W8.6 retro** | 2026-05-25 | (this commit) | — | This doc. |

**Net code change** across W8:
- 12 new files in `src/core/engine/rule/` (3 interfaces/base + 7 proposal `.h+.cpp` pairs, minus 4 = 7 proposals).
- 7 new gtest files (~70 new tests; 2048 → 2118).
- 1 engine body change (`ProcessVniVowelModifier` Pass 1) — the only behavior fix in the series.
- Zero regressions across the W7 baseline (still 54/55 on 4 Windows hosts; W8 inherits without re-running chaos).

---

## 2. Architecture Decisions

### AD-1: ModifierProposal as DECLARATIVE METADATA, not enforcement

**Decision**: Each `ModifierProposal` class declares `relocationKind()` returning a value from `enum RelocationKind { None, TargetTone, HornVowel, Conditional }`. The value documents the engine body's actual `RelocateToneTo*` call pattern. Tests pin the value so drift surfaces in CI.

**Crucially, the metadata DOES NOT enforce anything.** The engine body (`HandleAdjacentCircumflex`, `HandleHornW`, etc.) remains the source of truth for the actual speculation/apply/relocate sequence. The `tryApply()` method delegates verbatim to the body via `IModifierSubExecutor`.

**Alternatives rejected**:
- **Compile-time enforcement** (v1 plan framing): require proposals to call into a templated speculation helper that knows how to RelocateTone. Rejected because (a) the body mutates engine private state and (b) the round-3 reviewer flagged this as overclaim — the methods are virtual + body delegation; nothing compile-time-enforced about it.
- **Single `RelocationKind` per proposal without `Conditional`**: forced AdjacentCircumflex to lie about its per-pre-state branching (Valid → no relocate vs ValidPrefix → relocate). `Conditional` honestly captures the union and signals "auditors read both branches".

**Why declarative metadata wins**:
- Centralised audit surface: `grep RelocationKind:: src/core/engine/rule/` shows the policy map in 7 lines.
- Parity tests across Telex/VNI proposals: VniCircumflexProposal must declare TargetTone (matches HandleAdjacentCircumflex free-marking branch's behavior); mismatch = a 2-line class diff in review.
- Zero machinery cost — the proposal is a 15-LOC wrapper.

**Tradeoff acknowledged**: the ad09f15 / c6369dd invariant ("speculate path mirrors apply path") still lives in body comments + branch logic. Future contributors can drift it. Mitigation: tests pin metadata-to-body parity for each modifier, so drift is caught.

### AD-2: Single `IModifierSubExecutor` port (not N small ports)

**Decision**: One interface with one method per sub-handler:
```cpp
class IModifierSubExecutor {
    virtual bool HandleAdjacentCircumflex(...) = 0;
    virtual bool HandleHornW(...) = 0;
    virtual bool HandleStrokeD(...) = 0;
    virtual bool HandleHornInsert(...) = 0;
    virtual bool HandleVniCircumflex(...) = 0;
    virtual bool HandleVniHorn(...) = 0;
    virtual bool HandleVniBreve(...) = 0;
};
```

**Alternatives rejected**:
- **N small interfaces** (W7.4 style with `IToneExecutor` / `IModifierExecutor` / `IQuickConsonantExecutor`): rejected because all 7 methods share the same `(TypingAction, wchar_t) → bool` signature and "modifier sub-handler" semantic. Splitting buys nothing.

**Tradeoff acknowledged**: each W8.x wave that adds a new method requires stub additions in **all existing test mocks**. The W8 series added 6 methods after W8.1; cumulative mock-update cost was ~30 LOC across 4 test files. Acceptable.

If a future modifier type needs distinct test fixturing (e.g., different mock state) **then** consider splitting the port.

### AD-3: Wrap-don't-lift (inherited from W7 AD-1)

Body of every `HandleXxx` method stays on `TypingEngine`, declared `override` in the private section. The proposal's `tryApply()` calls `exec_.HandleXxx(action, c)` via the `IModifierSubExecutor` reference, which the engine implements as private virtual overrides.

**Why preserved from W7**: same reasoning — bodies mutate private state (`states_`, `escape_`, `engProt_`), and friending each proposal would bloat the engine's public surface.

### AD-4: Atomic per-wave commits (inherited from W7 AD-2)

Each W8.x wave is one commit with all of: interface change, new proposal `.h+.cpp`, TypingEngine inheritance + dispatch route, CMakeLists update, mock updates in existing tests, new test file. No "scaffold then activate" splits — the proposal layer is only useful when it's wired up.

**Exception that was applied**: W8.0 audit landed as a separate `test(engine)` commit before W8.1 because it's standalone diagnostic value (per W7 retro AD-2 sub-feature split exception).

---

## 3. Deviations from plan v3 (and why)

### D-1: W8.2 ships scaffold-only (NO speculation parity widening)

**Plan v3** estimated ~2 days for W8.2 bundling the HornModifierProposal + extending `WouldBeValidSyllable` with `RelocationKind` enum param (TODO 2026-05-23 hypothesis).

**Reality**: 8 probes (`HornW_SpeculateParityProbeTest`) showed no concrete repro. P5/P6 fire only for STANDALONE u/o (no UA/UO/OA pair); with a single horned vowel, `RelocateToneToHornVowel` is a no-op. The existing T5 tone-stop-coda recovery absorbs grave+p/c/ch/t edges.

Per memory `feedback_defer_with_promise` ("don't widen helper without repro") and the TODO's own discipline ("validate, don't trust intent"), W8.2 shipped as proposal scaffold only. Probes stay as regression guards for the day a real input surfaces the mismatch.

**Effort: ~0.5 day instead of ~2.**

### D-2: W8.3 re-scoped from "BreveProposal P7" to "StrokeDProposal"

**Plan v3** listed W8.3 as `BreveModifierProposal (P7 standalone-a)`. Telex P7 standalone-a is **inside** `HandleHornW`, which W8.2 already wraps. Creating a second proposal for the same handler doesn't match the W7 pattern (one proposal per ProcessModifier dispatch case) and would require restructuring HornW — conflicting with `project_typing_engine_stability` ("rule layer stable, refactor only operations").

**Re-scoped W8.3 to StrokeDProposal** — a real separate dispatch case with clean wrappable shape. This also populated `RelocationKind::None` in the audit surface, leaving only `TargetTone` for W8.5 to close.

### D-3: W8.5 included a REAL bug fix (not just scaffold)

**Plan v3** assumed VNI proposals would be scaffolding similar to W8.2. **Reality**: probe `vi5e6t → việt` FAILED on the engine (raw uncomposed output). This was the concrete repro TODO 2026-05-25 was waiting for. Applied the c6369dd template (ValidPrefix-gated speculate-relocate) to `ProcessVniVowelModifier` Pass 1. Probes pass; full suite green.

Also corrected the TODO's transcription error: the VNI mirror of Telex `ngufoon` is `ngu2o6n` (single `o`), not `ngu2oo6n` — VNI's `6` is an explicit modifier; Telex's `oo` is one literal `o` plus `o` acting as CircumflexO.

---

## 4. Resolved TODOs

- ✅ **`WouldBeValidSyllable` speculation parity — VNI vowel modifier (2026-05-25)** → resolved by W8.5 (`0a181d2`). Apply path now matches speculate path via `needsRelocate = preState == ValidPrefix` gating.
- ⏸️ **`WouldBeValidSyllable` speculation parity for Horn paths (2026-05-23)** → still open. W8.2 probes ran, no repro. Documented in `HornW_SpeculateParityProbeTest.cpp` header: "if any probe begins failing, that input is the concrete repro the TODO 2026-05-23 was waiting for".

---

## 5. Pattern slot for future work

Adding a new modifier action follows this template:

1. **New IModifierSubExecutor method**: `HandleNewModifier(TypingAction, wchar_t)`.
2. **New `XxxProposal.h+cpp`** in `src/core/engine/rule/`, inheriting `ModifierProposal`. Pick `relocationKind()` based on whether the body calls `RelocateToneTo*`.
3. **TypingEngine**: declare HandleNewModifier as `override` in private section, add `XxxProposal newProposal_{*this};` member.
4. **ProcessModifier dispatch**: route the new TypingAction case via `newProposal_.tryApply(action, c)`.
5. **CMakeLists**: add proposal `.h+.cpp` to `NEXTKEY_ENGINE_SOURCES` and test file to `NEXTKEY_TEST_SOURCES`.
6. **Update existing mocks**: each `tests/engine/*ProposalTest.cpp` has a `StubExec` / `RecordingExec` that needs the new method stub.
7. **New test file**: `XxxProposalTest.cpp` with behaviour cases + metadata pin + mock delegation. ~7-15 cases.
8. **Single atomic commit** per W7 AD-2.

**Friction point**: step 6 grows linearly with the number of existing proposals. If the count exceeds ~10, revisit AD-2 and consider splitting `IModifierSubExecutor`.

---

## 6. What W8 did NOT contribute

Per `2026-05-25-feature-pipeline-w8.1-adjacent-circumflex-proposal-plan.md` §11 honest framing:

- **Not** compile-time enforcement of speculate-mirrors-apply. That invariant still lives in comments + body branch logic.
- **Not** elimination of body-level branching. AdjacentCircumflex still has its per-pre-state if/else block; W8.1 wraps but doesn't simplify.
- **Not** a full operations-layer refactor. The Telex/VNI dispatch split in `HandleModifierAction` (W7.3) and the per-action handlers still live in their original god-method shape; W8.x is a wrapper layer above them.

The W8 contribution is **organisational**: a documented home for each modifier action's policy, with metadata for at-a-glance auditing and a slot for future per-action refactoring.

---

## 7. References

- W7 retro: `docs/plans/2026-05-23-feature-pipeline-w7-retro.md`
- W8 design brainstorm: `docs/plans/2026-05-25-engine-operations-contract-design.md`
- W8.1 plan v3: `docs/plans/2026-05-25-feature-pipeline-w8.1-adjacent-circumflex-proposal-plan.md`
- Commits: `1f2e76f`, `e39a35b`, `3803f1d`, `b623e74`, `ad451cc`, `0a181d2`
- Project memory: `project_adjacent_circumflex_validprefix_2026-05-25` (c6369dd template), `feedback_defer_with_promise`, `feedback_probe_before_theorize`, `project_typing_engine_stability`
