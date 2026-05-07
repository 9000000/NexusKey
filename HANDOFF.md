# NexusKey Refactor — handoff (T5 `cafcs→các` next; H1 closed)

## 2026-05-07 — H1 ProcessKeyDown decompose CLOSED (3 PRs, Main `659b910`)

**Pickup for teammate:** HookEngine refactor backlog effectively closed. Sequencing rule satisfied. Next active item is **T5** (`cafcs → các` tone replacement bug, 1-2h, HIGH).

### What H1 delivered

ProcessKeyDown went from a 561-LOC god-method to a **79-LOC orchestrator** (-86%) via 3 byte-identical PRs:

| PR | Method extracted | Steps | LOC |
|---|---|---|---|
| #143 H1a `e820876` | `HandleCommitUndo` | 2d (Idle/Ready/Primed FSM) | ~190 |
| #144 H1b `3c06499` | `RunTopGuards` | 0/0b/1/1b/1c (TSF/modifier/toggle/excluded-app) | ~50 |
| #145 H1c `659b910` | `HandlePreDispatch` + `DispatchKeyAction` | 3/3a-3d + 4b-10 | ~107 + ~158 |

Plus shared `enum class KeyOutcome : uint8_t { Eat, Pass, Fallthrough }` for outcome-based dispatch from ProcessKeyDown's helper switches.

Verification across all 3 PRs: Linux 1515/1515 PASS, Windows MSVC `/WX` clean (NextKeyApp + NextKeyTests), chaos.toml 5×11 = 55/55 PASS post each PR. Behavior byte-identical confirmed empirically.

### What H1 did NOT do

- **No perf gain.** Helpers may even add ns from extra call frames (LTO likely inlines anyway). Chaos perf delta = noise.
- **No new feature, no bug fix.** Pure refactor.

### Bonus this session

- **#142 H5 follow-ups** — fix C4244 narrowing in `tests/MacroCaseTest.cpp:145` (Linux GCC silent narrowing, Windows MSVC `/WX` block) + REFACTOR_STATUS refresh post-H5.

### Pickup for next session

1. **T5 — `cafcs → các`** — tone replacement blocked on already-toned syllable. 1-2h, HIGH. Test-first per project rule: write failing engine test reproducing the bug, then fix.
2. **T6 retest** — Issue #117 `Lỗi → Lôĩ` (fast-typing chaos timing). Anh hypothesis 2026-05-07: H1 hot-path restructure may have shifted timing → race no longer reproduces. Retest first, then decide.
3. **T2/T3** — Phonotactics deepening (onset agreement + N-group vowel-coda rules), 2-3h each, MEDIUM correctness.

User-visible alternatives (out of refactor scope):
- **G-5/G-6** — keymap UI (Sciter dialog + per-user TOML files).
- GitHub Issues triage.

**See `docs/REFACTOR_STATUS.md`** for full inventory and decision log.

---

## 2026-05-07 — H5 Macro extract MERGED (PR #141, Main `a141548`)

### What H5 delivered

- `HookEngine::TryExpandMacro` 217 LOC body → ~50 LOC orchestrator delegating to `Macro::Plan` + `ExpandEscapesForClipboard` + `BuildSegments` (3 free helpers in new `src/core/MacroCase.h/.cpp`, Linux-portable).
- DI seam via `Macro::CaseMapper` abstract class — production wraps `CharUpperBuffW`/`CharLowerBuffW` in `Win32CaseMapper.h`; tests use `AsciiCaseMapper`.
- HookEngine.cpp shrinks from 3582 → 3428 LOC (-154 — partly reclaimed by H1 verbose helper headers).
- 38 new gtests on Linux (1,477 → 1,515 PASS); chaos.toml 55/55 PASS across notepad/notepad++/chrome/discord/gpt.
- Behavior byte-identical to Main `3257758` per spec NF2.

**Spec & plan:** `docs/superpowers/specs/2026-05-07-h5-macro-extract-design.md`, `docs/superpowers/plans/2026-05-07-h5-macro-extract.md`.

---

## 2026-05-07 — Refactor sequencing rule (SATISFIED post-H1)

**Source of truth:** [`docs/REFACTOR_STATUS.md`](docs/REFACTOR_STATUS.md) — living inventory of all refactor work (done / in-flight / TODO / dead code / vital signs).

**Sequencing rule (anh decision 2026-05-07):** Complete the HookEngine refactor backlog (REFACTOR_STATUS §C: H1-H6) BEFORE picking up TypingEngine TODO items (§D: T1-T4). **Status post-H1c: SATISFIED.** §C now contains only H6 + H8 (roadmap-scale, multi-week, defer).

**Rationale (historical):** HookEngine was the highest-risk file. Architectural decomposition was the priority. Now ProcessKeyDown is a 79-LOC orchestrator with named helpers, and HookEngine.cpp gets verbose helper headers but the call graph is clear.

**Recommended next:**
1. **T5** `cafcs → các` — tone replacement bug, 1-2h, HIGH.
2. **T6 retest** — Issue #117 may be auto-resolved by H1 timing shift; verify before scheduling deep work.
3. **T2/T3** — Phonotactics deepening, 2-3h each.

**2026-05-07 shipped (8 PRs + tooling commit):** PR #138 (Path G G-4 customKeyMap), PR #139 (cleanup H2+H7+T1+T4), PR #140 (H3 atomic migration), PR #141 (H5 Macro extract), PR #142 (H5 follow-ups), PR #143 (H1a HandleCommitUndo), PR #144 (H1b RunTopGuards), PR #145 (H1c HandlePreDispatch + DispatchKeyAction), `2e30f82` (chaos harness graceful shutdown + lôĩ stress test). Main HEAD: `659b910`.

---

## 2026-05-07 — Sprint 3 Path G G-4 COMPLETE (CURRENT PICKUP NOTE)

**Branch:** `sprint-3/path-g-customkeymap`. **Goal:** engine-side per-key user override layer.

**Commits:**
1. G-4.1 — `customKeyMap` field on TypingConfig (default-init all `None`).
2. G-4.2 — G1 regression tests (default-empty parity, 3 cases).
3. G-4.3 — Dispatch hook at `TypingEngine.cpp:235` + G2 user-wins tests (2 cases).
4. G-4.4 — G3-G7 tests (22 cases): gap-fill, ASCII boundary, digit-sequence interaction, sentinel, all-actions parametric.

**Final dispatch shape (G-4):**

```
PushChar(c)
  └─ lower = towlower(c)
  └─ if (lower < 128 && customKeyMap[lower] != None)
        action = customKeyMap[lower]            ← user override wins
     else
        action = ClassifyKey(lower, isTelex, isVni)
  └─ if (isVniDigitSequence) action = None       ← literal-digit guard post-applies
  └─ ProcessModifier(action, c) → 6 Handle*
```

**Test counts:** 1450 (post-G-3) + 27 (G-4 new) = **1477 cases on Linux**.

**Files touched:** `src/core/config/TypingConfig.h` (+1 field, +2 includes), `src/core/engine/TypingEngine.cpp` (+4 LOC at line 235), `tests/CustomKeyMapTest.cpp` (NEW, ~250 LOC), `CMakeLists.txt` (+1 test source).

**Out of scope (deferred to G-5):** TOML schema, Sciter dialog, per-user `keymap_<name>.toml` files, active-method selector. ConfigManager untouched in G-4.

### NEXT — G-5 keymap files + UI

Wire ConfigManager to load per-user `keymap_<name>.toml` files into `TypingConfig.customKeyMap`. Add Sciter dialog for editing. Add `[input].active_user_method` field to select which keymap is loaded. Reuse the existing 7-file split pattern (see `ConfigManager.cpp`).

---

## 2026-05-07 — Sprint 3 Path G G-3 COMPLETE (CURRENT PICKUP NOTE)

**Main HEAD:** `56f48ed` (PR #137 merged). All Path G G-1..G-3.6 shipped via 4 PRs:

| PR  | Merge sha | Scope |
|-----|-----------|-------|
| #134 | `b73b6d2` | G-1 (Phonotactics class) + G-2.1..G-2.3 (FindToneTarget delegation, namespace flip) + G-2.4 (dead code) + G-3.1 (TypingAction enum + ClassifyKey) + G-3.2 (PushChar action dispatch) |
| #135 | `fa4ee91` | G-3.3 (extract HandleHornInsert{O,U} + HandleAdjacentCircumflex) + G-3.4 (symmetric VNI ProcessVniModifier) |
| #136 | `a1039d7` | G-3.4 polish (parallel-agent review fixes) + G-3.5 (unified ProcessModifier; Telex+VNI dispatchers collapsed; uniform Handle* sig; 5 wrappers) |
| #137 | `56f48ed` | G-3.6 (inline ProcessWModifier/ProcessDModifier/ProcessVniHornModifier into HandleHornW/StrokeD/VniHorn — drops 3 thin wrappers) |

**Final dispatch shape:**

```
PushChar(c)
  └─ ClassifyKey(lower, isTelex, isVni)  → TypingAction          (TypingAction.h)
     └─ ProcessModifier(action, c)        → switch(TypingAction)  (TypingEngine.cpp)
        ├─ HandleHornInsert(action, c)        ← Telex `[`/`]`     (action picks bracket+vowel)
        ├─ HandleAdjacentCircumflex(action,c) ← Telex aa/ee/oo + cross-vowel free-marking
        ├─ HandleHornW(action, c)             ← Telex w (P1-P8)
        ├─ HandleStrokeD(action, c)           ← Telex dd / VNI 9
        ├─ HandleVniHorn(action, c)           ← VNI 7 (uo pair + standalone)
        ├─ HandleVniCircumflex(action, c)     ← VNI 6 (wraps ProcessVniVowelModifier)
        └─ HandleVniBreve(action, c)          ← VNI 8 (wraps ProcessVniVowelModifier)
```

Six native handlers + two thin VNI vowel wrappers (kept so the cross-vowel scan in ProcessVniVowelModifier isn't duplicated per Modifier). 18 user-mappable TypingAction values; tone keys (ClearTone + 5 tones) handled inline in PushChar.

**Verification:** Linux GTest 1450/1450 PASS through every PR. Windows MSVC clean per @phatMT97 local verify.

**Test counts on Main:** 1440 baseline (pre-G-1) + 25 PhonotacticsTest (G-1) + 10 TypingActionClassifyKey (G-3.1) = **1450 cases**.

**TypingEngine.cpp LOC:** 1645 (pre-G-3) → 1657 (post-G-3.6). Brainstorm "~1300 LOC" target unmet — refactor reorganises rather than reduces. Net +12 LOC reflects the unified ProcessModifier dispatch + 6 Handle\* signatures + TypingAction.h include.

### NEXT — G-4 customKeyMap (start here)

Add `customKeyMap: array<TypingAction, 256>` field in `TypingConfig`. Wire override layer in PushChar BEFORE `ClassifyKey`:

```cpp
// At PushChar's classification step (currently TypingEngine.cpp around line 225):
TypingAction action = config_.customKeyMap.empty()
    ? ClassifyKey(lower, IsTelexMode(), IsVniMode())
    : config_.customKeyMap[static_cast<uint8_t>(lower)];
```

Or fold the override into ClassifyKey. Decide during planning.

**Path G remaining phases (per brainstorm):**

| Phase | Goal | Notes |
|---|---|---|
| **G-4** | `customKeyMap` field in TypingConfig + override layer in PushChar | Foundation in place — engine code path unchanged, just remap key→action. ConfigManager changes for backward-compat schema. |
| **G-5** | Sciter UI dialog + per-user `keymap_<name>.toml` files + conflict warnings + active-method selector | Reuse 7-file split pattern from existing ConfigManager. |
| **G-6** | Import/export Unikey + EVKey format compatibility | Unikey config: `S=DAU_SAC F=DAU_HUYEN ...` flat key=action. Maps cleanly to TypingAction. |

### Pickup commands (next session)

```bash
git checkout Main
git pull origin Main
# Confirm at 56f48ed
git log --oneline -1
# Branch fresh
git checkout -b sprint-3/path-g-customkeymap Main
```

Read this section + `_bmad-output/brainstorming/brainstorming-session-2026-05-07-0250.md` (Idea #11 = per-user keymap files) before planning G-4.

---

## 2026-05-07 (HISTORICAL) — Sprint 3 Path G, G-1 LANDED

Branch `sprint-3/path-g` off Main `54e379b`. Sprint 3 FSM rewrite cancelled; Path G replaces it (refactor TypingEngine + `Phonotactics` class + custom keymap layer). Brainstorm `_bmad-output/brainstorming/brainstorming-session-2026-05-07-0250.md` signed off; full Path G plan + scope locks below.

### G-1 — IPhonotactics interface + Phonotactics impl + GTest (DONE)

| Deliverable | File |
|---|---|
| Interface header | `src/core/engine/IPhonotactics.h` — namespace `NextKey::Phonology`, methods `TonePosition` / `IsValidSyllable` / `CanComplete`, `Tone` enum redeclared in-namespace for layer isolation |
| Concrete impl | `src/core/engine/Phonotactics.cpp/h` — anonymous-namespace helpers (Decompose, ParsedSyllable, IsClosedVowelSeq, IsPendingVowelSeq, IsKnownOnset/Coda…), reuses `NextKey::kDiphthongClassic/Modern` + `NextKey::IsTriphthong` from `VietnameseTables.h` (no duplication) |
| Tests | `tests/PhonotacticsTest.cpp` — 25 cases / 3 fixtures: `PhonotacticsTonePosition` (12), `PhonotacticsIsValidSyllable` (7 incl. parametric coda+tone matrix), `PhonotacticsCanComplete` (5 + 1 nonsense reject) |
| CMake | `CMakeLists.txt` — added 3 sources to `NEXTKEY_ENGINE_SOURCES`, added test to `NEXTKEY_TEST_SOURCES` |

Linux GTest **1434 / 1434 PASS** (1409 baseline + 25 new). Build clean. Windows MSVC verification pending — anh rebuild from WSL when picking up.

### Rules encoded in G-1 (per `docs/RuleTiengViet_Summary.md`)

- **Tone position priority** (mirrors `EngineHelpers::FindToneTargetImpl`): P1 last horn vowel → P2 first non-horn modified vowel (â/ê/ô/ă) → P3 modern triphthong middle | diphthong table (rule 1=FIRST, 2=SECOND, 3=coda-aware) → P4 rightmost.
- **Closed vowels (28)** reject any coda — `ai, ao, au, ay, âu, ây, eo, êu, ia, iu, oi, ôi, ơi, ui, ưa, ưi, ưu, iêu, uôi, uyu, ươi, ươu, oai, oay, uây, uya, oeo, oao`.
- **Pending vowels (10)** require coda — `ă, â, iê, oă, uâ, uô, oo, ôô, ươ, uyê`.
- **Stop-final coda** (`c, ch, p, t`) restricts tone to `Acute (sắc)` or `Dot (nặng)`.
- **CanComplete parser** splits partial → onset / vowels / coda / leftover, rejects post-coda leftover (covers `gacha`, `gachw` auto-exclusion case from `spell_exclusions`).

### Out of G-1 scope (deferred to G-2 or later)

- **Onset agreement** (c/k/qu, g/gh, ng/ngh) — `IsValidSyllable` currently ignores `onset` arg. Add when G-2 wires Phonotactics into TypingEngine and we see real call patterns.
- **N1/N2/N3 vowel-coda compatibility** — currently only closed/pending checks. Tighter group rules (N1 vowels can pair with C1+C3 only, not C2; etc.) deferred.
- **Shifted-3-vowel typo rule** in `EngineHelpers::FindToneTargetImpl` (e.g. "hoaa", "gaoi" with vowel-repeat coda) — couples to CharState concept of which vowel carries the modifier; revisit when G-2 has the call sites.
- **Issue #117 (`Lỗi → Lôĩ` fast typing)** — explicitly out of Path G; separate Sprint 1/2 timing-class follow-up.

### G-2.1 — DI plumbing (DONE 2026-05-07)

`Phonotactics::Default()` static accessor returns process-wide stateless singleton. `TypingEngine` gains 2-arg ctor `(const TypingConfig&, const Phonology::IPhonotactics&)`; existing 1-arg ctor delegates with `Phonotactics::Default()` so all 30+ existing call sites (EngineFactory, tests, dialogs) compile unchanged. Member `phonotactics_` stored as `const IPhonotactics&` — reference, not value, for swappability per CODING_RULES §4.2.

3 new tests: `TypingEngineDI.AcceptsCustomPhonotactics`, `TypingEngineDI.SingleArgCtorBindsDefaultPhonotactics`, `PhonotacticsDefault.ReturnsStableSingleton`.

### G-2.2 — Replace FindToneTarget* with phonotactics_ (DONE 2026-05-07)

`TypingEngine::FindToneTarget()` rewired to delegate to `phonotactics_.TonePosition(...)`. The classic/modern split moves into Phonotactics (driven by `config_.modernOrtho` bool); `FindToneTargetClassic`, `FindToneTargetModern`, and the per-class `FindToneTargetImpl` member are removed. The 5 external call sites (`TypingEngine.cpp:240,263,460,491,1093`) stay untouched.

Wrapper handles the structural translation:
1. Iterate `states_`, skip cluster consonants (gi/qu) via `IsClusterConsonant`,
2. Compose each nucleus state with `tone=None` and `isUpper=false` so Phonotactics' Decompose sees the canonical lowercase modifier+base char,
3. Track `vowelStateIdx[k] = state index` for later mapping,
4. Build `coda` from any post-last-vowel states,
5. Call `phonotactics_.TonePosition(vowelSeq, coda, modernOrtho)`,
6. Map the returned vowel-sequence index back to a `states_` index.

Phonotactics extensions to match `EngineHelpers::FindToneTargetImpl` semantics:
- **Shifted-3-vowel rule** ported into `ComputeTonePosition`: when count ≥ 3 and the first 2 of the last 3 vowels have a diphthong rule, shift the (firstPos, lastPos) pair onto them. Vowel-repeat at end forces FIRST (typo cases like "hoaa", "oaa"). Otherwise rule-3 coda-aware uses `(lastPos + 1 < count) || !coda.empty()` to decide FIRST vs SECOND.
- **Vowel array cap raised** from 4 → 16 to keep all of `máaaaaaaa`-style typos in scope; P1/P2 must scan the full sequence to find any horn/modifier no matter where the user typed it, and P4-rightmost must point at the actual last vowel for `IsToneRelocBlockedByP4` to drift-block correctly.

3 new Phonotactics tests: `ShiftedThreeVowelTypoAoi`, `ShiftedThreeVowelRepeatHoaa`, `ClassicOaiHasRemainderRule`.

Linux GTest **1440 / 1440 PASS**. The `EngineHelpers::FindToneTargetImpl` free function now has zero callers — dead code, kept for G-2.4 cleanup.

### G-2.3 — Namespace unification under Phonology (DONE 2026-05-07)

**G-2.3.A** added `Phonology::SyllableState` + `Phonology::ValidateSyllableState<CharStateT>` as aliases over `SpellCheck::*` and routed production callers (`EngineHelpers::UpdateSpellState`, `TypingEngine::Commit` ValidPrefix branch, `TypingEngine::WouldBeValidSyllable`) through the new entry point.

**G-2.3.B** flipped the underlying namespace: `SpellChecker.{h,cpp}` now lives under `NextKey::Phonology` directly (no aliases). `enum class Result` → `enum class SyllableState`; `template Validate` → `template ValidateSyllableState`. The structural validator and the wstring-based `IPhonotactics` interface now share one namespace, one rule engine. Filenames retained as `SpellChecker.{h,cpp}` until a follow-up commit consolidates the validator under a Phonotactics-prefixed name (G-2.4 cleanup).

Test files migrated: `tests/SpellCheckerTest.cpp` + `tests/FeatureOptionsTest.cpp` (18 references). Zero `SpellCheck::` references remain in the codebase.

Linux GTest **1440 / 1440 PASS**. Zero behavior change — the underlying validator code is byte-identical, only namespace + names flipped.

### NEXT — G-2.4 (cleanup) and beyond

| Step | Goal |
|---|---|
| G-2.4 | Delete `EngineHelpers::FindToneTargetImpl` free function (zero callers post-G-2.2). Optionally rename `SpellChecker.{h,cpp}` files to `PhonotacticsValidator.{h,cpp}` via `git mv` (preserves history). Audit `kDiphthong*` table consumers — keep tables in `VietnameseTables.h` (still used by `IsToneRelocBlockedByP4` + `EnglishProtection`). |
| G-3 | Internal handler dispatch: `TypingAction` enum + ~20 extracted handlers + single dispatch table. PushChar uses `kHandlers[action]`. TypingEngine drops to ~1300 LOC. |
| G-4 | `customKeyMap` field in `TypingConfig`. PushChar checks override before default Telex/VNI dispatch. |
| G-5 | Sciter UI dialog + per-user `keymap_<name>.toml` files + conflict warnings + active-method selector. |
| G-6 | Import/export Unikey + EVKey format compatibility. |

### Path G remaining phases (per brainstorm sign-off)

| Phase | Goal | Sessions |
|---|---|---|
| **G-1** | Phonotactics class + interface + tests | DONE |
| **G-2** | Refactor TypingEngine `FindToneTarget*` → `Phonotactics::TonePosition`. Spell check → `IsValidSyllable`. Auto-exclusion via `CanComplete`. | 1-2 |
| **G-3** | Internal handler dispatch: `TypingAction` enum + ~20 extracted handlers + single dispatch table. TypingEngine drops to ~1300 LOC. | 1-2 |
| **G-4** | `customKeyMap` field in `TypingConfig`. PushChar checks override before default Telex/VNI dispatch. | 1 |
| **G-5** | Sciter UI dialog + per-user `keymap_<name>.toml` + conflict warnings + active-method selector. | 1-2 |
| **G-6** | Import/export Unikey + EVKey format compatibility. | 1 |

### Coding rules adherence (per `docs/CODING_RULES/`)

- §1 namespace: `NextKey::Phonology::` sub-namespace ✓
- §1.2 include order: own → system → STL → project ✓
- §4.1 interface-based: `IPhonotactics` virtual interface ✓
- §9 naming: PascalCase class/methods, camelCase locals, `kCamelCase` table constants (matching `VietnameseTables.h` precedent), no Hungarian, no single-letter non-loop vars ✓
- §10 documentation: WHY-comments where non-obvious; minimal doc on stable rule logic ✓

### Pickup commands (next session)

```bash
git checkout sprint-3/path-g
cmake --build build-linux --target NextKeyTests
./build-linux/tests/NextKeyTests --gtest_brief=1   # 1440 PASS

# G-2.4 first steps:
grep -rn "FindToneTargetImpl" src/   # only definition left at EngineHelpers.h:491
# Optional: git mv src/core/engine/SpellChecker.{h,cpp} → PhonotacticsValidator.{h,cpp}
# Update CMakeLists + the 4 #include "SpellChecker.h" sites.
```

### Open items for anh

- **Windows MSVC verification pending.** Anh rebuild from WSL after pulling — most-likely warning surface is `[[nodiscard]]` placement on virtual `noexcept` overrides under `/W4 /WX`, plus `const IPhonotactics& phonotactics_` member-init order. Em fixes inline.
- **G-2 scope decision** — DI locked per brainstorm sign-off. G-2.1 plumbing landed; G-2.2 is the substantive swap.

---

## Sprint 3 FSM — D-1 MERGED TO MAIN (2026-05-06) — superseded by Path G above

Main is at `0600f34`. Three PRs merged this session 2026-05-05/06 night:

| PR | Title | Effect |
|---|---|---|
| **#131** | design: FSM engine refactor (Sprint 3) + implementation plan | Adds `docs/plans/2026-05-05-fsm-engine-refactor-design.md` (642 lines) + `docs/plans/sprint-3-fsm-engine-plan.md` (687 lines). Locks 8 brainstorm decisions; license audit (gonhanh.org BSD-3 ✅, PHTV AGPL-3 ❌, xkey MIT skip). |
| **#132** | tooling: FSM codegen tool (Sprint 3 D-1) | Standalone Python pkg `tools/fsm-codegen/` — parser → NFA → DFA → Hopcroft minimize → overlay → emit C++. 16 commits, 8 tasks. 179/179 pytest PASS. Generates 10 C++ headers (~288 KB source / ~30 KB runtime). Engine code untouched. |
| **#133** | feat(classic): import / export spell-check exception list | Adds Nhập / Xuất buttons to `ClassicSpellExclusionsDialog`. UTF-8 BOM-safe import with replace/append, sort + dedup. Plus README contributor add (Shzr0). |

### Pipeline numbers (real Vietnamese rules + Telex full)

```
NFA:     55,801 states  /  1.14s  build
DFA:     55,727 states  /  0.70s  subset construction
Min:     55 states      /  0.44s  Hopcroft (1014× reduction)
Overlay: 1,483 actions  /  0.50s
TOTAL:   2.78s end-to-end on real rules

Memory dense post-min: ~30 KB resident across 3 methods
                       (target was 50-80 KB per method — 5× under budget).
```

### Generated headers in `tools/fsm-codegen/codegen/emitter.py`

```
abstract_input.h           1,860  bytes (shared enum)
action_table_<method>.h    ~5 KB  (3 methods)
fsm_table_<method>.h      ~80 KB  (3 methods)
keymap_<method>.h           ~7 KB  (3 methods)
TOTAL SOURCE:            288,464  bytes
```

CLI: `python -m codegen.cli --rules rules/ --output /tmp/gen` produces all 10.

---

## NEXT — D0 (engine refactor begins)

**Branch to create**: `sprint-3/fsm-engine` from Main.

**Task list (per `docs/plans/sprint-3-fsm-engine-plan.md` Tasks 11-16)**:

1. **Task 11 — CMake codegen integration** (~2-3 hours)
   - `CMakeLists.txt`: `find_package(Python3 REQUIRED COMPONENTS Interpreter)`.
   - `add_custom_command` to run `tools/fsm-codegen/codegen/cli.py` at configure time → output to `${CMAKE_BINARY_DIR}/generated/`.
   - Add include path so source files can `#include "abstract_input.h"` etc.

2. **Task 12 — Pre-generated headers committed** (~30 min)
   - Run codegen, commit output to `src/core/engine/generated/*.h` (7 headers per method × 3 methods + shared = 10 files).
   - CMake picks committed dir if Python unavailable (headless build fallback).
   - CI step: re-run codegen, `git diff` should be empty (no drift).

3. **Task 13 — `FsmDispatcher` class skeleton** (~3-4 hours)
   - Create `src/core/engine/FsmDispatcher.{h,cpp}`.
   - Forward-decl `FsmTable`, `Keymap`, atomic shared_ptr members for RCU.
   - `ProcessKey(rawKey) -> HookVerdict` stub (returns PASSTHROUGH).
   - `ProcessBackspace()` stub.
   - Setter accessors with `std::atomic_store`.
   - Add to CMake source list. Linux compile clean. **NOT wired to hook yet.**

4. **Task 14 — `HistoryRingBuffer<Record, 256>`** (~2 hours)
   - Lock-free SPSC, single-thread (hook callback). `Push`, `PopLast`, `RewindToPrevSyllable`, `Clear`.
   - `tests/HistoryRingBufferTest.cpp`: 256-record fill+wrap, push-pop matched, rewind boundary.

5. **Task 15 — `CommitUndoSM`** (~1-2 hours)
   - Port 3-state Idle/Primed/Ready logic from `HookEngine.cpp:556-594`.
   - Tests: state transitions on space/punct/Enter + BS + non-BS input.

6. **Task 16 — D0 commit** (~30 min)
   - All scaffolding committed atomically. Engine wiring NOT touched.
   - Linux GTest 1409+ PASS.
   - Windows MSVC compile clean.
   - Commit message: `Sprint 3 D0: FSM scaffolding (codegen output, FsmDispatcher, HistoryRingBuffer, CommitUndoSM)`.

After D0 → D1 (FsmDispatcher impl + diff harness 31,757 cases) → D2 (fix to diff=0) → D3 (plugin bus + SyllableValidator) → D4 (hook swap + chaos 55/55 PASS) → D5 (delete TypingEngine + SpellChecker, ~1800 LOC) → D6 (baseline + PR).

### Pickup commands (next session)

```bash
git checkout Main && git pull origin Main           # 0600f34 expected
git checkout -b sprint-3/fsm-engine                 # new branch from Main

# Verify codegen tool still works (ship-state baseline):
cd tools/fsm-codegen
python3 -m venv /tmp/fsm-venv
/tmp/fsm-venv/bin/pip install pytest
/tmp/fsm-venv/bin/python -m pytest                  # 179/179 PASS
/tmp/fsm-venv/bin/python -m codegen.cli \
    --rules ../../rules/ --output /tmp/gen          # 10 headers, 288 KB

# Then start Task 11 — CMake integration.
```

### Key open items flagged

- Plan doc Task 7b (full 20,504 syllable enumeration verifier) deferred — current 35 curated cases sufficient gate for D-1.
- `docs/RuleTiengViet_Summary.md` lists `ôô` (line 48 + 73) but anh confirmed not real Vietnamese — TOML now corrects it; doc itself NOT updated. Anh decides if upstream-fix worth doing.
- License attribution: gonhanh.org BSD-3 cited in codegen output headers + emitter docstring; need to also add `LICENSE-3RD-PARTY.md` at repo root before D-1 PR if anh's repo policy requires it (currently not required — codegen output already attributes inline).

---

## Sprint 3 FSM — D-1 COMPLETE (previous session note, kept for trail)

Earlier this session 2026-05-05 night the codegen-tool work was on a feature branch. The above section reflects post-merge state.

- Branch: `sprint-3/codegen-tool` (now merged + deleted)
- Design branch: `design/fsm-engine-refactor` (now merged + deleted)

### What landed this session (D-1 Tasks 1-8 — ALL DONE)

| Task | Commit | What |
|---|---|---|
| 1  | `96ef1a7` | Python pkg skeleton — `tools/fsm-codegen/` (CLI, parser/nfa/dfa/overlay/verifier stubs, 4 smoke tests) |
| 2a | `1cf5504` | Rule TOML data — `rules/{vietnamese-phonotactics,telex-keymap,vni-keymap}.toml` |
| 2a (fixes) | `34987aa`, `5a5e6d2`, `62a4e03`, `c4b0a9c` | anh review: remove ôô, add `]/[`/Telex Simple, classic-default, VNI `uo+7/6` combos, auto-horn `huơ` edge case (h/th/kh) |
| 2b | `0a62474` | `RuleParser` impl + 23 tests |
| 3a | `aea0cae` | NFA test design (23 tests, locks API surface) |
| 3b | `ade8beb` | `Nfa.from_rules()` impl — 9 layers (cons / vowel / modifier / tone / coda / 2-vowel / uo edge / escape) |
| 4  | `5b03427` | DFA subset construction (Aho/Sethi/Ullman §3.7) + 22 tests |
| 5  | `15a3dd4` | Hopcroft minimization + 17 tests (407× state reduction) |
| 6  | `922a725` | Tone/modifier overlay + Action emission + 32 tests (Vietnamese tone diacritic table 12 vowels × 5 tones, render_state, compute_action diff) |
| 7  | `bb6de6f` | Exhaustive verifier + 5 NFA bug fixes revealed by 35 curated cases (multi-char cons `tr`, MOD_D_BAR, coda chaining `n→ng/nh`, layer order, etc.) + perf O(n²)→O(1) state-by-id index |
| 8  | `bb9c9e2` | Emit C++ tables (`fsm_table_<method>.h`, `keymap_<method>.h`, `action_table_<method>.h`, shared `abstract_input.h`) for telex/telex-simple/vni. Each header g++ -fsyntax-only verified. CLI wired end-to-end. |

### Pipeline numbers (real Vietnamese rules + Telex full keymap)

```
NFA:     55,801 states  /  1.14s  build
DFA:     55,727 states  /  0.70s  subset construction
Min:     55 states      /  0.44s  Hopcroft (1014× reduction)
Overlay: 1,483 actions  /  0.50s  text-emission Actions per transition
TOTAL:   2.78s end-to-end on real rules

pytest: 179/179 PASS in 70s (was 163 pre-Task-8; +16 emitter tests
                              with g++ syntax-check overhead)
Memory dense (post-min):  ~7 KB  (target was 50-80 KB — 10× under budget)
```

### Generated headers (per method × 3 methods + shared)

```
abstract_input.h           1,860  bytes (shared enum)
action_table_telex.h       5,196  bytes
action_table_telex_simple  5,198  bytes
action_table_vni.h         4,657  bytes
fsm_table_telex.h         84,844  bytes
fsm_table_telex_simple.h  84,894  bytes
fsm_table_vni.h           79,004  bytes
keymap_telex.h             7,598  bytes
keymap_telex_simple.h      7,610  bytes
keymap_vni.h               7,603  bytes
TOTAL SOURCE:            288,464  bytes
RUNTIME DATA estimate:   ~30 KB resident (3 methods + shared) —
                         comments inflate source 10×; actual constexpr
                         data is just states × inputs × 4 bytes per cell.
```

### What's NEXT — open PR + start D0

**Step 1**: Open PR `tooling: FSM codegen tool (Sprint 3 D-1)` from
`sprint-3/codegen-tool` to Main. Body should reference design doc +
plan doc + chaos-baseline preservation (D-1 standalone, no engine touch).
Merge after review.

**Step 2**: Start D0 on new branch `sprint-3/fsm-engine` from Main:
1. CMake integration (run codegen at configure-time).
2. Commit pre-generated `src/core/engine/generated/*.h` so headless
   build works without Python.
3. New `FsmDispatcher` class skeleton + `HistoryRingBuffer` +
   `CommitUndoSM`. NOT wired to hook callback yet.
4. Linux GTest 1409+ PASS.

D0 is plan-doc Task 11-16. Estimate ~1 day.

### Pickup commands

```bash
git checkout sprint-3/codegen-tool
cd tools/fsm-codegen
python3 -m venv /tmp/fsm-venv && /tmp/fsm-venv/bin/pip install pytest
/tmp/fsm-venv/bin/python -m pytest                  # 179/179 PASS
/tmp/fsm-venv/bin/python -m codegen.cli \
    --rules ../../rules/ --output /tmp/test-emit/   # 10 headers, 288 KB source

# To prep the PR:
git checkout sprint-3/codegen-tool
git push -u origin sprint-3/codegen-tool
gh pr create --base Main --title "tooling: FSM codegen tool (Sprint 3 D-1)" \
    --body "..."  # link design + plan docs
```

### Key open items flagged

- `docs/RuleTiengViet_Summary.md` lists `ôô` (line 48 + 73) but anh confirmed not real Vietnamese — TOML now corrects it; doc itself NOT updated (separate scope; anh decides if upstream-fix worth doing).
- Curated case list 35 covers core happy paths but not edge cases like `lèeeeee` (extended vowel for chat). Verifier extension to full ~20K syllables (Task 7b) deferred.
- License audit done — gonhanh.org BSD-3 ✅ compatible with NexusKey GPL-3. PHTV AGPL-3 SKIP. Memory: `reference_external_vn_typing_repos.md`.

---

## Post-T3 status (2026-05-05 PM) — previous pickup note

Today's session shipped 7 PRs (#122-128) on top of the post-T3 cleanup
(PR #121, c19239c). Main is now at `32fceb7`. Two production fixes,
one piece of shared tooling, three small follow-ups, one baseline
capture.

### Landed today

| PR | Subject | Effect |
|---|---|---|
| #122 | Pre-T3 Minor 2: lock-free hot path in `QuickSyncFromSharedState` | Hot path no longer acquires `stateMutex_` on common case. Slow path (configGeneration bumped) still locks but is user-paced. Closes the highest-impact open audit item from the Pre-T3 review. |
| #123 | ChannelTraits cleanup | `isElectronApp_` + `needBaitChar_` atomic flags moved from HookEngine onto `IOutputInjector` as `HasMultiProcessRenderer()` / `NeedsBaitCharPrefix()` virtual methods. `HandleAlphaKey` now reads via 1 `injector_.load()` snapshot + 2 virtual calls instead of 2 separate atomic loads. `SplitDispatchInjector` gained a 3rd ctor param to distinguish Electron (multi-process renderer) from Console. |
| #124 | `tools/run-chaos.ps1` harness | Single-command driver replacing the per-host manual workflow. Loops over 5 hosts (notepad / notepadpp / chrome / discord / gpt), launches NexusKey + target, drives the runner, watches stdout for the "stop NexusKey" prompt, kills + Enter, parses each report. Exit 0 iff every host clean. |
| #125–127 | Three quick follow-ups on the harness | `pwsh` → `powershell` invocation (PS 7+ vs 5.1), strip non-ASCII chars from the script (Win-1252 misdecode of em-dash terminated string literals on PS 5.1), and correct the default `-RunnerExe` path to match CMake's `RUNTIME_OUTPUT_DIRECTORY`. |
| #128 | `perf-baseline-channeltraits-chaos` | Full chaos verify of #123 via the new harness — 55/55 PASS, no regression vs T3 baseline. ChatGPT improved -11 ms p99 (consistent with the saved atomic load); other hosts flat or slightly improved. |

### Verified gates

- Linux GTest **1409 / 1409 PASS**.
- Audit `tools/audit/check_hook_thread_no_mutex.sh` **5 / 5 PASS** (Check 5 added in #122 enforces lock-free hot path; Check 3's `ATOMIC_BOOLS` list pruned in #123 to drop the deleted names).
- Windows chaos sweep **55 / 55 PASS** across all 5 hosts (`docs/baselines/perf-baseline-channeltraits-chaos.md`).

### Worst-case p99 vs T3 baseline

| Host | T3 | ChannelTraits | Δ |
|---|---|---|---|
| Notepad Win11 RichEdit | 17 ms | **15 ms** | -2 ms |
| Notepad++ | 17 ms | **16 ms** | -1 ms |
| Chrome omnibox | 17 ms | **15 ms** | -2 ms |
| ChatGPT (Chromium textarea) | 33 ms | **22 ms** | -11 ms |
| Discord | 27 ms | **28 ms** | +1 ms (flat) |

### Items pending — pickup priority for next session

1. **Pre-T3 Minor 1 — `LowLevelMouseProc` race on `cachedFocusedHwnd_`.** Investigate-first: read the exact write set in the mouse callback, then decide between (a) atomic migration, (b) defer to `MainThreadWorker`, (c) document as benign. Detailed in `docs/TODO.md` "Review (2026-05-05)" section.

2. **M3 — dual-route `TrackedSendInput` unification.** Bundles cleanly with the output area refactored in #123. Route the 4 VB6/clipboard sites + reinjectVk through `Internal::TrackedSendInput`, then delete the `HookEngine::TrackedSendInput` member. ~5 call sites, non-trivial.

3. **Typing bug `cafcs → các`** (spell-check tone-replacement gate). User-facing. Investigation-first via `nexuskey-typing-bugs` skill before reading code. Repro + 3 hypotheses captured in `docs/TODO.md`.

4. **M2 — constants naming convention (`kFoo` vs `UPPER_SNAKE`).** Needs 3-collaborator decision before code: update Rule 9.1 to formalise the k-prefix convention, or rename ~10 codebase constants. Recommendation in TODO is to update the rule.

### Repo cleanup landed in this same PR

- All 30 D1–D5 chaos artefacts (`perf-d{1-5}-*.csv`, `report-d{1-5}-*.xml`) moved from repo root into `docs/baselines/` so `perf-baseline-t3-final.md`'s "Companion files in this directory" note is accurate. `docs/baselines/` is now the canonical location for chaos-capture artefacts (T3, ChannelTraits, and the D-day predecessors).
- 13 untagged + harness-smoke trial files (`perf-{host}.csv`, `report-{host}.xml`, `*-harness-smoke-notepad.*`) deleted — no doc references them; they were trial captures from prior sessions.

### Key references

| Doc | Use for |
|---|---|
| `docs/TODO.md` (top) | Pending items, audit follow-ups, deferred work |
| `docs/baselines/perf-baseline-channeltraits-chaos.md` | Latest chaos baseline + comparison vs T3 |
| `docs/CODE_GOVERNANCE.md` Part 1 | 5-question gate before architectural proposals |
| `docs/CODING_RULES/11-hook-system-rules.md` | Rule #11 (1 ms budget, contention law) |
| `docs/plans/sprint-1-single-owner-refactor.md` D6 | RCU pattern reference (Pre-T3 Minor 2 used it) |
| `tools/run-chaos.ps1` + `tools/README.md` | Chaos harness — for verifying every future hook-engine change |

### Memory (auto-loaded each session)

Already-captured rules that future-em should follow without re-deriving:
- `feedback_no_coauthor` — no `Co-Authored-By` in commits
- `feedback_review_discipline` — review = report only, never auto-fix
- `feedback_vietnamese_response` — reason in English, respond in Vietnamese
- `feedback_role_split` — anh verifies feature/UI/test results; em decides architecture, codes, runs tests
- `project_test_first` — failing test before implementation; concurrency bugs only caught by tests
- `project_three_questions` — right place / impact / better way before code
- `project_core_philosophy` — Nhanh / Nhẹ / Mượt / Mở rộng-không-ảnh-hưởng-perf

---

## TL;DR

NexusKey's hook engine has long-standing race-condition bugs (x2 space, ghost
key, tone misplacement under fast typing). Phase 0a built the test harness;
Sprint 1 (this branch) brought the hook into compliance with the
just-committed Rule #11 (no mutex on hook hot path) via single-owner refactor.

**Headline result (D12, 2026-05-05):** chaos verdict flipped from **5 / 11 PASS → 11 / 11 PASS** on Win11 New Notepad (`RichEditD2DPT`). All 6 stable FAILs (`vieejt nam`, `xin chao ban`, `hello vieejt`, `truongwf`, `uongs`, `binhf thuongwf`) plus the cross-word edit case (`5.3`) now pass byte-exact. Root cause was *not* a TelexEngine state-machine bug as D4 hypothesized (engine reproducer PASSED at HEAD — see D12.5 row); it was a **WinUI 3 async-render race** in the hook's output channel: `RichEditD2DPT` lags caret behind queued `WM_KEYDOWN`, and mixing sent `EM_REPLACESEL` with posted `SendInput` (passthrough alpha + `SendBackspaces` + commit-trigger physical) reorders under burst load — sent messages pre-empt the posted queue, EM_REPLACESEL runs on stale caret, then the posted BS / chars drain afterwards and corrupt the result. Fix routes **every** output channel through `EM_REPLACESEL` when `useEditMsgPath_` is set (alpha disabled-passthrough, commit triggers eaten + sent, BS via `TryEditMessagePaste`, commit-undo BS via same), with a 30 ms caret-lag retry loop. EVKey passes the same corpus on the same app because it uses pure SendInput throughout — never mixes sent/posted. NexusKey added `EM_REPLACESEL` for the WinUI 3 flicker fix, and that addition required the all-or-nothing rule to be safe under chaos load.

**Cross-app smoke (D12 follow-up, 2026-05-05):** chaos.toml re-run with `target_app` switched to Chrome (address bar / textarea — non-`useEditMsgPath_` host) gave **10 / 11 PASS**. Only `5.3-cross-word-bs-vieejt-nam-bs4-s` still fails, and the corruption shape **changed**: Notepad-pre-fix `etết` → Notepad-post-fix `viết` (PASS) → **Chrome `việts`**. Chrome takes the original SendInput-batch / split-Electron path (Fix C is gated on `useEditMsgPath_`, which is false here), so the new failure is a different bug than the one D12 closed. See `docs/baselines/perf-baseline-d12-chrome-cross-app.md` for full analysis. Treat as Sprint 2 follow-up — does not block Sprint 1 PR for the Notepad / RichEditD2DPT chaos win.

**Where we are right now (2026-05-05 morning — D11 resolved, Phase D
started):** Phase B is committed clean through D7 (`9f77412`). D11
(`recursive_mutex` → `std::mutex`) committed (this commit). The run-1 5.1
FAIL captured at the prior pause was **heisenbug noise, not a D11
regression**: re-running chaos on the same uncommitted binary produced
byte-identical 5.1 PASS `Giả`. 5.1's lifetime now reads PASS / PASS /
PASS / PASS / PASS / PASS / FAIL / **PASS** across D3 / D4 / D5 / D5.1 /
D5.2 / D6 / D11r1 / D11r2 — single FAIL after 6 PASSes, then immediate
return. L1 chaos worst p99 dropped to **14 ms (1.1)** — the lowest in
the Sprint 1 series (D4 17 → D5 18 → D5.1 16 → D5.2 16 → D6 18 →
D11 14). Sustained verification was elected-skipped for D11 (sustained
has been byte-identical 3 PASS / 0 FAIL with forward 5/0.41 %, edits 0/0 %
across all 8 prior captures D1–D6; D11 changes lock semantics only —
no data flow / no engine logic change — so the realistic-pace property
is fully baselined). **D12.5 was reframed when engine-layer reproducer
PASSED**: a unit test driving `truongwf` directly through `TypingEngine`
(no hook, no SendInput, no threads) produces `trường` correctly at HEAD.
The D4 plan's "engine state-machine bug" framing for 3.3 was wrong —
the corruption only emerges through the hook → engine → SendInput
replay loop at 500 µs inter-key. The 2 protective unit tests
(`D12_5_Truongwf_FullCodaThenWThenTone`, `D12_5_Truongw_NoTone`) are
kept as engine regression guards. The chaos 3.3 verdict flip is
deferred to **D8** (MainThreadWorker — async queue for hook → main
work, also closes the pre-existing Rule #11 violation where
`ProcessKeyDown → QuickSync` acquires `stateMutex_` on the hook hot
path), which directly attacks the hook integration where 3.3 actually
breaks.

| Layer | Status | Reference |
|---|---|---|
| Project philosophy + Rule #11 | ✅ Committed `0e5322e` | `docs/PHILOSOPHY.md`, `docs/CODING_RULES/11-hook-system-rules.md` |
| Sprint 1 plan (14-day, A0→A→B→C→D→E) | ✅ Committed `ec9798e` | `docs/plans/sprint-1-single-owner-refactor.md` |
| D0: NextKeyTestRunner extensions (text field, --convert, edit_distance) | ✅ Committed `a372a27` + Windows fix `3459642` | `tools/NextKeyTestRunner/` |
| D1: Sustained forward baseline locked | ✅ **0.41 % error** at master | `docs/baselines/perf-baseline-3459642-sustained-forward.{csv,xml,md}` |
| D2: Sustained edit baseline locked | ✅ **0.00 % error** on both edit cases at master | `docs/baselines/perf-baseline-3459642-sustained-edit.{csv,xml,md}` |
| D3: Pre-spike snapshot locked | ✅ Sustained zero-drift; chaos heisenbug-bounded | `docs/baselines/perf-baseline-a28f1ea-pre-spike-{chaos,sustained}.{csv,xml,md}` |
| D4: Spike (3 hook-thread mutex acquisitions commented out) | ✅ **Outcome B** — mutex not the bug source | `docs/baselines/perf-baseline-d4-spike-{chaos,sustained}.{csv,xml,md}` |
| D5: `vietnameseMode_` → `std::atomic<bool>` (incremental, N=1) | ✅ DoD met — sustained byte-identical, chaos heisenbug envelope preserved, L1 worst p99 18 ms | `docs/baselines/perf-baseline-d5-atomic-vnmode-{chaos,sustained}.{csv,xml,md}`, `tests/HookEngineAtomicTests.cpp` |
| D5.1: `currentMethod_` → `std::atomic<InputMethod>`, `isTsfApp_` → `std::atomic<bool>` (19 sites) | ✅ DoD met — sustained byte-identical (p99 −3 ms vs D5), chaos 1 PASS gain via 1.2 flip, no PASS regress, L1 worst p99 16 ms (improvement) | `docs/baselines/perf-baseline-d5.1-atomic-method-tsf-{chaos,sustained}.{csv,xml,md}` |
| D5.2: 15 remaining hook-read primitives → `std::atomic` (8 per-app cached + `excludedPid_` DWORD + 6 config-derived; ~60 sites) | ✅ DoD met — sustained byte-identical, chaos verdicts preserved (1.1+1.2+1.3+5.1+5.2 byte-identical PASS, 2.1+2.2 byte-identical FAIL), L1 worst p99 16 ms (cap unchanged), 3.3 engine-stress p99 −5 ms | `docs/baselines/perf-baseline-d5.2-atomic-rest-{chaos,sustained}.{csv,xml,md}` |
| D6: RCU `shared_ptr<const TypingConfig>` for `config_` (7 sites + 3 RCU GTest cases) | ✅ DoD met — sustained byte-identical (forward p99 −1, edits −3 ms vs D5.2), chaos stable PASS preserved, stable FAIL {2.1, 2.2, 3.3} byte-identical, L1 worst p99 18 ms (run 2; run 1 hit 22 ms = heisenbug, dropped to 14 ms on re-run); 5.2 flipped FAIL (flip-prone per HANDOFF) | `docs/baselines/perf-baseline-d6-rcu-config-{chaos,sustained}.{csv,xml,md}`, `tests/TypingConfigRCUTests.cpp` |
| D7: audit script `tools/audit/check_hook_thread_no_mutex.sh` + CI integration (4 checks: D4 spike comment integrity, no `stateMutex_` reachable from LL hook entries, atomic fields use `.load`/`.store`, RCU `config_` likewise) | ✅ DoD met — script exits 0 on current tree, wired into `.github/workflows/build.yml` as fail-fast pre-build step | `tools/audit/check_hook_thread_no_mutex.sh`, `.github/workflows/build.yml` |
| D11: `recursive_mutex` → `std::mutex` (type swap + `ApplyConfig` self-lock removal + `Start` defensive lock + `FocusPollTimerProc` lock-region split) | ✅ DoD met — sustained skipped (byte-identical baseline across 8 prior captures, D11 changes lock semantics only); chaos run 2 5 PASS / 6 FAIL with stable PASS {1.1, 1.3, 5.1} byte-identical and 5.2 flip-PASS; L1 worst p99 14 ms = lowest in series. Run 1's 5.1 anomaly attributed to heisenbug. | `docs/baselines/perf-baseline-d11-plain-mutex-chaos.{csv,xml,md}` |
| D12.5: engine-level fix for chaos 3.3 `truongwf` → `tờương` (originally framed as engine state-machine bug per D4 plan §A) | ✅ **engine-clear** — pure-engine reproducer (`TelexEngineTest.D12_5_Truongwf_*`) PASSES at HEAD; bug is hook-integration-only. Two protective unit tests added; chaos 3.3 flip deferred to D8 side effect. | `tests/TelexEngineTest.cpp`, `docs/baselines/perf-baseline-d4-spike-chaos.md` §"D12.5 finding (revised)" |
| D8: MainThreadWorker scaffolding (start/stop only, portable cv-based) | ✅ DoD met — 7 lifecycle tests on Linux (start/stop < 100 ms, idempotent start/stop, RAII destructor, 50× rapid cycle stress); Windows MSVC build clean. No runtime wiring — pure scaffolding per plan §C | `src/app/system/MainThreadWorker.{h,cpp}`, `tests/MainThreadWorkerTests.cpp` |
| D9: MainThreadWorker handles config-changed event | ✅ DoD met — Signal/SetWorkHandler API + 7 dispatch tests (within-50ms wake, coalescing, pre-Start latch, post-Stop no-op, handler swap, exception isolation). main.cpp + main_lite.cpp `hookReloadCallback_` rewired so cross-process Settings → main → `worker.Signal()` instead of direct main-thread `SyncConfigFromSharedState()`. Worker handler runs `SyncConfig` on its own thread, pre-empting hook QuickSync slow path. **Note:** plan §C Q1 hinted Win32 ConfigEvent HANDLE wait, but the existing `WM_NEXUSKEY_HOOK_RELOAD` PostMessage path already lands on main thread; a portable cv-Signal there is functionally equivalent and Linux-testable. | `src/app/main.cpp`, `src/app/main_lite.cpp`, `src/app/system/MainThreadWorker.{h,cpp}` |
| D10: MainThreadWorker handles heartbeat + CJK layout poll | ✅ DoD met — `SetTickHandler` / `SetTickInterval` on the worker (6 new tests: cadence, no-tick when interval=0, no-crash when handler unset, signal+tick coexist, mid-run interval change, tick exception isolation). `HookEngine::FocusPollTimerProc` retired; body migrated to public `OnTickPoll()` driven from worker tick at 200 ms. `SetTimer(nullptr, 0, 200, FocusPollTimerProc)` and the matching `KillTimer` removed from `HookEngine::Start`/`Stop`; main.cpp + main_lite.cpp wire `g_mainThreadWorker.SetTickHandler/SetTickInterval(200ms)` after Start. **Note:** plan §C D10 also mentioned a heartbeat thread — none existed in the tree (raw-input self-heal in `RawInputWndProc` is event-driven, not timer-driven, and untouched). | `src/app/system/HookEngine.{h,cpp}`, `src/app/main.cpp`, `src/app/main_lite.cpp`, `src/app/system/MainThreadWorker.{h,cpp}` |
| D11: `recursive_mutex` → `std::mutex` | ✅ already committed `3a60bc3` (re-listed in D11 row above) | — |
| D12 / D13: full corpus gate run + PR prep | D12 ✅ chaos 11/11 PASS on Win11 New Notepad RichEditD2DPT (was 5/11 baseline). All 6 stable FAIL flipped (1.2, 2.1, 2.2, 2.3, 3.3, 5.2, 5.3, 6.1). Root-caused: WinUI 3 RichEditD2DPT async-render race — caret-stale `EM_GETSEL` while physical `WM_KEYDOWN` queued + sent `EM_REPLACESEL` pre-empts posted `SendInput` BS. Fix C routes ALL output through `EM_REPLACESEL` for `useEditMsgPath_` apps (alpha, commit triggers, BS, commit-undo BS) + 30 ms caret-lag retry. D13 PR prep next. | `docs/plans/sprint-1-single-owner-refactor.md` §D/§E, `src/app/system/HookEngine.cpp` |
| D12 / D13: full corpus gate run + PR prep | pending | `docs/plans/sprint-1-single-owner-refactor.md` §D/§E |

## Branch state

- Branch: `refactor/phase-1-single-owner` (4 commits ahead of master `43fb4c1`)
- Cross-platform tests: **281 / 281** pass on Linux (Windows MSVC verified)
- Tool location: `tools/NextKeyTestRunner/` (now with `--convert`, `text` field, `edit_distance` verdict)
- Frozen baselines:
  - `docs/baselines/perf-baseline-43fb4c1.{md,csv,xml}` — chaos corpus (11 cases, 5 PASS / 6 FAIL)
  - `docs/baselines/perf-baseline-3459642-sustained-forward.{md,csv,xml}` — sustained forward (1 case, 0.41 % error)
- Branch adds docs + test infra only. **No production code touched yet.** The single-owner refactor begins at D3.

## Significant findings from D1 + D2 baselines

**D1 (sustained forward, 50 ms inter-key, 1 case, 1 631 keys):** master state
handles realistic forward Vietnamese typing at **0.41 % error rate**.

**D2 (sustained edit, 50 ms inter-key, 2 cases, ~21 keys total):** master state
handles realistic edit scenarios (inline tone correction, cross-word backspace
replay) at **0.00 % error rate**. The chaos `5.3-cross-word-bs-vieejt-nam-bs4-s`
key sequence — which corrupts at 1 ms inter-key — produces correct output at
50 ms inter-key, byte-exactly.

**Combined implications:**

1. The original "≥ 30 % improvement on sustained" gate is unworkable on BOTH
   sustained dimensions (30 % of 0.41 % is below noise; 30 % of 0.00 % is
   undefined). Plan revised: both sustained dimensions are "no-regression"
   anchors only.
2. The interesting Sprint 1 movement signal lives in **chaos FAIL flips**
   (≥ 1 of 6) and **L1 timing tightening** (single-owner removes mutex
   contention; expected p99 drop from current 60 ms to under 50 ms post-D11).
3. Chaos bugs are confirmed speed-bound: same key sequences pass cleanly at
   realistic typing pace. The Sprint 1 refactor's value is **letting the
   engine survive sub-millisecond burst input**, not fixing structural
   edit-path bugs (those don't exist at this layer).

### Heisenbug variance noted on chaos co-run (2026-05-04 ~1010)

Re-running `chaos.toml` against the same `43fb4c1` runtime as the locked baseline
produced a 5 PASS / 6 FAIL total (count unchanged) but the **composition shifted**:

| Case | Locked baseline (43fb4c1.md) | New run | Note |
|---|---|---|---|
| 5.2 `uongs` | FAIL `ốngg` | **PASS** | flipped |
| 6.1 `binhf thuongwf` | PASS `bình thường` | **FAIL** `bình tườngg` | flipped |
| 2.3 `hello vieejt` | FAIL `helệo viet` | FAIL `heloệ viet` | same FAIL, ệ position drifts |
| 5.3 `vieejt nam BS×4 s` | FAIL `etết` | FAIL `itết` | same FAIL, first char differs |
| 2.1, 2.3, 3.3 | FAIL | FAIL (identical strings) | stable |
| 1.1, 1.3, 5.1 | PASS | PASS | stable |
| **1.2** | PASS | PASS in conv-run, **FAIL `in`** in D3 capture | flip-prone (D3 evidence) |
| **2.2, 5.3** | FAIL | FAIL with corruption shape varying between runs | composition unstable |

This is the heisenbug behavior already documented in `perf-baseline-43fb4c1.md`
observation #2 (6.1 was borderline at D7 capture). Confirmed: under sub-ms
input, engine state is non-deterministic — the same input gives different
corrupt outputs run-to-run, and a few cases (5.2, 6.1) flip between PASS/FAIL.

**Implication for Sprint 1 D12 gate**: "≥ 1 FAIL flip" can be satisfied by
heisenbug noise rather than by an actual fix. Stable FAILs (2.1, 2.2, 3.3,
2.3, 5.3) are the meaningful regression-detection targets; 5.2 and 6.1 are
unreliable signals on their own. Concrete tightening for D12:

- **Run chaos N=3 times** post-refactor and require a flip in ≥ 2 of 3 runs.
- **Track stable FAILs explicitly**: a flip on 2.1, 2.2, or 3.3 (which never
  PASSed in any captured run) is high-confidence; a flip on 5.2 or 6.1 alone
  is low-confidence and must be corroborated by an L1 timing improvement.
- **Locked baseline is NOT updated** with this re-run data — `43fb4c1.md` is
  frozen by design. The variance observation lives here in HANDOFF.

### Cross-engine + cross-version check (2026-05-04 ~1030)

The same chaos corpus run on the same Windows host at the same 1–5 ms inter-key
pace against three engines:

| Engine | Result | Note |
|---|---|---|
| NexusKey current (`43fb4c1`) | 5 PASS / 6 FAIL (heisenbug variance noted above) | Sprint 1 starting state |
| **NexusKey v2.1** (older release) | **PASS-clean (≈ EVKey)** | Per Phat's test, 2026-05-04 |
| **EVKey** | **11 PASS / 0 FAIL** | Different project, stable |

This is **regression evidence**: an older version of *the same project* handled
the chaos cases cleanly. The bugs were introduced by feature additions over
time, not by a fundamental design limitation. The competing engine (EVKey)
confirms the architecture *can* be stable.

Implications for Sprint 1:

1. The chaos failures are **NexusKey-specific regressions**, not a pace-induced
   limitation. Earlier doc wording that called them "speed-bound" (in chaos
   baseline obs #2 and the first draft of sustained-edit obs #1) was
   imprecise — v21 demonstrates the same engine logic *was* stable at this
   pace before recent feature work introduced regressions.
2. Sprint 1 single-owner refactor's value is **restoring architectural
   stability** — not as a "code health" abstraction, but as a concrete
   condition that v21 had and current does not: adding a feature should not
   regress prior chaos behavior. The four pillars (Nhanh / Nhẹ / Mượt / Mở
   rộng-không-làm-nặng) plus "dễ debug" govern this directly: an architecture
   where features compose without regressing each other.
3. Plan scope is **unchanged**. The single-owner refactor is the right
   structural change — it disentangles state ownership so feature additions
   do not silently couple via shared mutable state. Phase D outcome B
   (D12.5 single-FAIL engine fix) accommodates per-bug regression repairs
   if the architectural change alone does not fully restore v21 behavior.
4. EVKey passing 11/11 + v21 passing chaos is **proof the architecture is
   recoverable**, not just proof "fixes exist for individual bugs". Post-
   Sprint 1, the right comparison is: does adding the next feature on the
   refactored base regress chaos? If no, the architectural goal is met.

**Locked baseline `43fb4c1.md` is NOT amended** with this data — frozen by
design. The observation lives here in HANDOFF.

**Methodology note (lesson recorded in feedback_test_dont_theorize):** the
v21 + EVKey runs were direct test evidence supplied by Phat. An earlier
draft of this section reasoned about what the chaos FAILs *must* be from
the locked baseline alone, without the comparator data. That hypothesizing
was wrong-shaped — the right question was "can we run the same corpus
against a known-good engine?", which Phat answered by running v21 and EVKey.
Future Sprint 1 work that needs to attribute a regression cause should
likewise prefer "run the corpus against state X" over "reason about state X
from data we already have".

## Teammate handoff (2026-05-05) — picking up Sprint 1 D13

If you're inheriting this branch from Phat, this is the **fastest read for the current state**. Skip directly past the legacy "Read in this order" section below — that was for D3 pickup and is now stale on most points.

**Sprint 1 status: code-complete on Notepad / RichEditD2DPT. PR not yet opened. One known cross-app issue on Chrome (5.3 only).**

### What's done

- Phase A0/A/B/C/D all committed on this branch (`refactor/phase-1-single-owner`).
- Chaos verdict on Win11 New Notepad: **5 / 11 → 11 / 11 PASS** at commit `582dab2`. Baseline captured: `docs/baselines/perf-baseline-d12-richedit-fix-chaos.{md,csv,xml}`.
- Cross-platform GTest suite: 1403 / 1403 PASS on Linux at commit `58be0f7`.
- D7 audit script (`tools/audit/check_hook_thread_no_mutex.sh`) exits 0 — Phase B compliance maintained.
- HookEngine refactor: lock-free hook hot path (atomic flags + RCU `shared_ptr<TypingConfig>`), `recursive_mutex` → `std::mutex`, `MainThreadWorker` owns config drain + 200 ms CJK / focus poll (was a `SetTimer`).
- Output-channel fix: for `useEditMsgPath_` apps, **all** output (alpha, commit-trigger char, BS, commit-undo BS) routes through `EM_REPLACESEL` — solves the chaos failures that were a `RichEditD2DPT` sent-vs-posted reorder race, NOT the engine state-machine bug D4 hypothesised.

### What's left for Sprint 1 close

1. **Decide what to do about the Chrome 5.3 finding** (see below). My recommendation: document as known limitation in the merge PR, defer fix to Sprint 2 IOutputInjector. Notepad / RichEditD2DPT chaos win stands on its own.
2. **Open the PR.** Title: `Sprint 1: single-owner hook engine refactor + RichEditD2DPT chaos fix`. Body should link the 13 commits and the canonical baseline doc.
3. **(Optional) Manual smoke test on a few real-world apps** before merge — the chaos corpus is synthetic; the architectural changes are broad enough that one real-world walkthrough on Notepad++, Word / Outlook, Slack / Discord (Electron) is cheap insurance. Won't catch the Chrome 5.3 case but will catch obvious regressions in the editMsg path.

### Known Chrome 5.3 issue (2026-05-05)

After the Notepad fix landed, smoke run on Chrome (address bar / textarea — non-`useEditMsgPath_` host) shows **10 / 11 PASS** with **5.3 still failing**, but the corruption shape **changed**:

- Notepad-pre-fix `5.3` actual: `etết`
- Notepad-post-fix `5.3` actual: `viết` (PASS)
- Chrome-post-fix `5.3` actual: `việts` (FAIL, new shape)

Engine layer is clean (engine reproducer test passes). The fix C all-EM_REPLACESEL rule is gated on `useEditMsgPath_`, which is false for Chrome — so Chrome takes the original SendInput-batch / split-Electron output path. The new failure shape suggests the BS+chars portion of the synthetic burst is being dropped (or pre-empted by the reinjected VK_S) on Chrome's renderer, but exact mechanism is not localised yet.

**Full analysis + investigation paths:** `docs/baselines/perf-baseline-d12-chrome-cross-app.md`. Reproduces with foreground=Chrome and `chaos.toml`.

This is **separate** from the bug D12 just closed on Notepad — different host, different output path. Don't try to fix it by tightening the EM_REPLACESEL rule (that would regress everything else).

### Do NOT before merge

- Do **not** revert any of the D5–D11 atomic / RCU / mutex changes — they are the foundation Fix C builds on, and the D7 audit script will fail.
- Do **not** re-introduce a posted-output path (passthrough alpha, raw `SendBackspaces`, raw `InjectKey(VK_BACK)`) for an editMsg app — that resurrects the chaos failures D12 closed.
- Do **not** change `useEditMsgPath_` detection to fire for Chrome — Chrome doesn't accept `EM_REPLACESEL` and the path will fail silently. Need a different abstraction (Sprint 2 IOutputInjector).

### Files to know

| File | Purpose |
|---|---|
| `HANDOFF.md` (this) | Current branch state |
| `docs/plans/sprint-1-single-owner-refactor.md` | The 14-day plan, all phases marked ✅ except D13 |
| `docs/baselines/perf-baseline-43fb4c1.{md,csv,xml}` | Locked baseline (5/11 PASS pre-Sprint-1) |
| `docs/baselines/perf-baseline-d12-richedit-fix-chaos.{md,csv,xml}` | Canonical D12 baseline (11/11 on Notepad) |
| `docs/baselines/perf-baseline-d12-chrome-cross-app.md` | Cross-app smoke + Chrome 5.3 finding |
| `src/app/system/HookEngine.{h,cpp}` | All Sprint 1 hook changes |
| `src/app/system/MainThreadWorker.{h,cpp}` | New in D8–D10 |
| `tools/audit/check_hook_thread_no_mutex.sh` | D7 CI guard — must keep exit 0 |
| `tools/NextKeyTestRunner/corpus/chaos.toml` | Full corpus, 11 cases |
| `tools/NextKeyTestRunner/corpus/chaos-3.3-only.toml` | Single-case repro for diagnostic capture |

### Reproduce locally (Windows)

```powershell
cd '\\wsl.localhost\Ubuntu-24.04\home\phatmt\code\NexusKey\build'
cmake --build . --target NextKeyApp NextKeyTestRunner --config Debug

# Start NexusKey Debug from build\Debug\NexusKey.exe (tray icon)
# Open Notepad, focus it.

.\tools\Debug\NextKeyTestRunner.exe `
    --corpus ..\tools\NextKeyTestRunner\corpus\chaos.toml `
    --junit  ..\docs\baselines\NEW.xml `
    --perf-csv ..\docs\baselines\NEW.csv `
    --hook-log Debug\NexusKey_hook.log
```

Runner prompts at end of run — exit NexusKey from tray, press Enter, runner post-mortem-parses `NexusKey_hook.log` for L1 timing.

### Linux GTest

```
cmake --build build-linux --target NextKeyTests
./build-linux/tests/NextKeyTests
```

1403 tests should pass.

---

## Read in this order (for a teammate picking up D3)

1. **`docs/PHILOSOPHY.md`** (~10 min)
   Four pillars (Nhanh / Nhẹ / Mượt / Mở rộng-no-runtime-cost), test-first,
   three pre-code questions. Highest-level filter for all design decisions.

2. **`docs/CODING_RULES/11-hook-system-rules.md`** (~5 min)
   Operationalizes Pillar #1 — 1 ms hook budget, forbidden ops, contention
   law, atomic + RCU patterns, two-phase classification. Mandatory before
   touching anything reachable from `LowLevelKeyboardProc`.

3. **`docs/plans/sprint-1-single-owner-refactor.md`** (~10 min)
   The 14-day plan. D0–D1 are ✅. **Pick up at D2.** Each day has Q1/Q2/Q3
   pre-code answers, test-first artifact, DoD, and commit message draft.

4. **`docs/baselines/perf-baseline-43fb4c1.md`** (chaos baseline, 5 PASS / 6 FAIL)
   and **`docs/baselines/perf-baseline-3459642-sustained-forward.md`**
   (sustained forward, 0.41 % error). The "before" pictures Sprint 1 must
   not regress.

5. **`_bmad-output/brainstorming/brainstorming-session-2026-05-03-1201.md`**
   (986 lines) — full design history. Phase 6.3 SCAMPER + Phase 7.4 sprint
   table. Read only if Sprint 1 hits an unexpected blocker; otherwise the
   plan + rules are sufficient.

6. **`_bmad-output/brainstorming/brainstorming-session-2026-05-04-0734.md`**
   pre-mortem session that produced the philosophy + plan. Optional, but
   shows the reasoning behind the assumption verdicts.

7. **`git log 43fb4c1..HEAD --oneline`** plus full bodies — every commit has
   pre-code questions answered + test diff. Read commit `a372a27` (D0
   infrastructure) and `3459642` (Windows fix) for the testing patterns.

8. **`tools/NextKeyTestRunner/README.md`** + the runner's `--help` output —
   for `--corpus`, `--convert`, `--hook-log` flag references.

## Sprint 1 — D2 next (sustained edit baseline)

**Goal:** Encode 2–3 cases for typo + cross-word edit scenarios into
`tools/NextKeyTestRunner/corpus/sustained.toml`, run them on the SAME
master `43fb4c1` runtime that the rest of D0–D1 measured, lock the
baseline.

**Approach (per plan §A0 D2):**

Each scenario built from the `text_with_edits`-style pattern:
- Pure text segments → run through `Telex.h::StrToTelex` (use the new `text` field; auto-converts).
- Backspace interjections → explicit `\b` characters or `keys` field.
- For each case: write expected as the *intended Vietnamese final text*,
  drive the actual sequence, observe what the engine produces, set
  `expected = <observed>` if it matches user expectation. **Do not predict
  the output from telex math — verify on the actual engine.**

Verify any hand-written telex via the new CLI:
```powershell
.\build\tools\Debug\NextKeyTestRunner.exe --convert "việt có dấu"
# -> vieejt cos daasu
```

Reference scenario types (see plan A0 §D2):
- `inline-typo-correction` — type a wrong letter mid-word, BS, retype.
- `cross-word-edit-fix` — type a word, commit, BS past committed text,
  retype to fix. (User's example: `việt có dấu` → BS×N → `viết có dấu`.)
- `mixed-edit-session` — 50 forward + 3 typos + 1 cross-word edit (optional).

**DoD for D2:** baseline files
`docs/baselines/perf-baseline-<sha>-sustained-edit.{csv,xml,md}` committed.
Each new case in `sustained.toml` has a comment citing kTable lines
(`Telex.h:25–44`) for hand-written telex segments.

**Anti-pattern reminder (from `feedback_never_hand_encode_telex` memory):**
Do NOT generate Vietnamese telex from memory. Always cite the kTable entry.
The `--convert` CLI is the source of truth.

## Sprint 1 — D3+ (refactor proper)

After D2 baseline is locked, the diagnostic spike begins (D3 lock pre-spike
snapshot, D4 minimal mutex drop). See plan for full day-by-day. The 3 Rule
#11 violations to remove:

| File:line | Function |
|---|---|
| `src/app/system/HookEngine.cpp:647` | `LowLevelKeyboardProc` |
| `src/app/system/HookEngine.cpp:677` | `WinEventProc` |
| `src/app/system/HookEngine.cpp:705` | `LowLevelMouseProc` |

**Gate (revised after D1):**
- Chaos: ≥ 5 PASS, ≥ 1 FAIL flip vs `perf-baseline-43fb4c1`.
- Hook callback p99: ≤ 16 ms (chaos burst).
- Sustained forward: no regression vs D1 baseline (≤ 0.41 % error).
- Sustained edit: no regression vs D2 baseline (≥ 30 % improvement target).

**Verification per commit:**
1. Build NexusKey debug + restart.
2. `NextKeyTestRunner --corpus chaos.toml --hook-log ... --junit ... --perf-csv ...`
3. `NextKeyTestRunner --corpus sustained.toml --hook-log ... --junit ... --perf-csv ...`
4. Diff against baselines.
5. On merge, commit new baselines as
   `docs/baselines/perf-baseline-<sha>-{chaos,sustained-forward,sustained-edit}.{csv,xml,md}`.

## Known limitations (do NOT re-discover)

| Limitation | Where documented | Impact |
|---|---|---|
| Sub-ms inter-key impossible from user-mode driver — floors at ~3 ms | baseline.md observation #1 | Phase 1 perf goals use 3 ms target, not 1 ms |
| L1 timing only via post-mortem log parse (NexusKey buffers + locks log while running) | commit `2efd180` body | Tool prompts user to stop NexusKey at end of run |
| **Heisenbug**: `_IONBF` log slowed hook enough to mask race-condition bugs entirely | commit `d58bb4e` revert + `2efd180` body | **DO NOT re-enable `_IONBF`** without verifying bugs still reproduce against baseline |
| Uppercase Vietnamese passthrough in `--raw` mode returns false from VkKeyScanW | commit `fd4a14f` body | Use lowercase Vietnamese in TOML `keys` field |
| Uppercase Đ (U+0110) in `text` field also fails — `StrToTelex` passes uppercase through and `VkKeyScanW(Đ) == -1` on US layout | D1 baseline `3459642`, commit body | Edit Vietnamese source paragraphs to avoid uppercase Đ; e.g. `Điều này` → `Việc này`. ASCII uppercase (H, K, M, V, ...) is fine. |
| `std::min({initializer-list})` breaks under MSVC after `Windows.h` (min macro) | D0 fix commit `3459642`, memory `feedback_windows_min_max_macros` | Use `(std::min)(a, (std::min)(b, c))` paren-trick form in headers that may be included after `Windows.h`. |

## Sprint 1-5 roadmap (from brainstorm Phase 7.4)

| # | Focus | Est | Source |
|---|---|---|---|
| **1** | T2: Single-Owner + `WaitOnAddress` + 1 worker thread | 2 weeks | Phase 6.3 SCAMPER |
| 2 | T3: `IOutputInjector` factory + matrix harness | 2 weeks | Phase 6.2 First Principles |
| 3 | T4: `noexcept` enforcement + SEH = WER fallthrough | 1 week | Phase 5 Crash Resilience |
| 4 | T5: Passive self-healing (heartbeat) | 1 week | Phase 2 architecture |
| 5 | Integration + full regression | 1 week | T1-5 wired together |

**V2 backlog** (deferred, with full context preserved in brainstorm Phase 6.3):
- Hook fast-path foreground detection (profile switch 50 ms → 1 ms)
- ETW tracing (replaces post-mortem log parse with real-time)
- External crash watcher process (alternative to WER fallthrough)

## Anti-abandon rules (from brainstorm Phase 6.1 Blue Hat)

Phase 0a was completed with zero abandoned days. Same discipline carries to Sprint 1+:
- Daily commit (10 LOC counts) — streak prevents abandonment.
- User-reported bugs unrelated to current sprint → issue tracker, **do not** fix inline.
- Crash / data-loss bugs are the exception — pause sprint, fix, return.
- Don't merge a half-baked sprint branch into master. Ship full sprints.

## Build commands (quick reference)

### Linux test build (recommended for CI / engine logic)
```bash
cmake -B build-linux -G "Unix Makefiles" -DCMAKE_BUILD_TYPE=Debug
cmake --build build-linux --target NextKeyTestRunnerTests
./build-linux/tests/NextKeyTestRunnerTests
```
Expected: **281 / 281** tests pass in < 5 ms (was 250 before D0 added EditDistance + CliConvert + new TomlLoader cases).

### Windows full build (from WSL)
```bash
powershell.exe -Command 'cd "\\wsl.localhost\Ubuntu-24.04\home\phatmt\code\NexusKey\build"; cmake --build . --target NextKeyTestRunner --config Debug'
powershell.exe -Command 'cd "\\wsl.localhost\Ubuntu-24.04\home\phatmt\code\NexusKey\build"; cmake --build . --target NextKeyApp --config Debug'
```

### Reproduce baseline
See `docs/baselines/perf-baseline-43fb4c1.md` "Reproduction" section.

## Open questions / next decisions

When picking up Sprint 1, you'll need to decide:
1. Land `MainThreadWorker` in same PR as Single-Owner refactor, or split?
2. Keep current `SharedStateManager` seqlock, or migrate to `WaitOnAddress` event?
3. Drop `recursive_mutex` cold turkey or behind a feature flag?

The brainstorm doc Phase 6.3 SCAMPER has these tradeoffs analyzed. Read the
"Tổng hợp 4 đề xuất" table before committing to an approach.
