# Sprint 3 FSM Engine Refactor — Implementation Plan

> **For agentic workers:** Use checkbox (`- [ ]`) tracking. Tasks group by D-day; each D-day = ONE atomic commit.
>
> **Branch policy:** D-1 work on `sprint-3/codegen-tool` (codegen tool only, can land first). D0-D6 work on `sprint-3/fsm-engine` (engine refactor). Codegen tool merged first; engine PR second.
>
> **Foundation:** [`2026-05-05-fsm-engine-refactor-design.md`](2026-05-05-fsm-engine-refactor-design.md) — Read §0 (5-question gate), §1 (locked decisions), §6 (D-day plan) before starting.
>
> **Baseline:** chaos 55/55 PASS on Main. GTest 1409/1409 PASS Linux. Sprint 2 output injector preserved.

---

## D-1 — Codegen Tool Development (~1 week pre-sprint)

Goal: Python tool that parses phonotactic rules + keymap TOML, builds NFA, converts to DFA, minimizes via Hopcroft, exhaustive-verifies against 20,504 syllables, emits C++ constexpr tables.

Branch: `sprint-3/codegen-tool` from Main. Standalone — no NexusKey runtime dep. Lands as separate PR before D0.

### Task 1: Project skeleton

**Files:**
- Create: `tools/fsm-codegen/pyproject.toml`
- Create: `tools/fsm-codegen/main.py`
- Create: `tools/fsm-codegen/codegen/__init__.py`
- Create: `tools/fsm-codegen/codegen/nfa.py`
- Create: `tools/fsm-codegen/codegen/dfa.py`
- Create: `tools/fsm-codegen/codegen/parser.py`
- Create: `tools/fsm-codegen/codegen/emitter.py`
- Create: `tools/fsm-codegen/codegen/verifier.py`
- Create: `tools/fsm-codegen/tests/__init__.py`
- Create: `tools/fsm-codegen/tests/test_nfa.py`
- Create: `tools/fsm-codegen/tests/test_dfa.py`
- Create: `tools/fsm-codegen/tests/test_parser.py`
- Create: `tools/fsm-codegen/tests/test_emitter.py`
- Create: `tools/fsm-codegen/tests/test_verifier.py`
- Create: `tools/fsm-codegen/README.md`

- [ ] **Step 1:** Init `pyproject.toml` with deps: `tomli` (Python <3.11) or stdlib `tomllib` (>=3.11), `pytest`, `pyfsa` (or `automata-lib` — pick one after spike).
- [ ] **Step 2:** Skeleton modules with empty class stubs (`Nfa`, `Dfa`, `RuleParser`, `CppEmitter`, `Verifier`).
- [ ] **Step 3:** `main.py` CLI: `python -m fsm_codegen --rules rules/ --output src/core/engine/generated/`.
- [ ] **Step 4:** Run pytest stub — all tests pass (no asserts yet). Verify Python pkg structure.
- [ ] **Step 5:** README explains pipeline + how to run.

### Task 2: Parse phonotactic rules

**Files:**
- Create: `rules/vietnamese-phonotactics.toml` (hand-authored from `docs/RuleTiengViet_Summary.md`)
- Create: `rules/telex-keymap.toml`
- Create: `rules/vni-keymap.toml`
- Modify: `tools/fsm-codegen/codegen/parser.py`

- [ ] **Step 1:** Author `rules/vietnamese-phonotactics.toml`. Schema:

```toml
[consonants]
initial_d1 = ["b", "d", "đ", "g", "l", "m", "n", "p", "r", "s", "t", "v", "ch", "nh", "ph", "tr"]
initial_d2 = ["c", "h", "th", "kh"]   # c includes k/qu variants by rule
initial_d3 = ["x", "gi", "ng"]         # ng includes ngh
initial_special = ["k", "qu", "gh", "ngh"]   # context-dependent

[vowels]
single  = ["a", "ă", "â", "e", "ê", "i", "o", "ô", "ơ", "u", "ư"]   # 11
diphthong = ["ai", "ao", "au", "ay", "âu", "ây", "eo", "êu", "ia", "iê",
             "iu", "oa", "oă", "oe", "oi", "ôi", "ơi", "ua", "uâ", "uê",
             "ui", "uô", "uơ", "uy", "ưa", "ưi", "ươ", "ưu", "oo", "ôô"]    # 30
triphthong = ["iêu", "oai", "oay", "uây", "uôi", "uyê", "uyu",
              "ươi", "ươu", "uya", "oao", "oeo"]                            # 12

[final_consonants]
c1 = ["ng", "c"]
c2 = ["nh", "ch"]
c3 = ["m", "n", "p", "t"]

[vowel_groupings]
n1 = ["â", "e", "o", "ô", "u", "ư", "ơ", "ă", "oă", "oe", "uâ", "uô", "uơ", "ươ"]   # only c1+c3, no c2
n2 = ["ê", "i", "uê", "uy", "ua"]                                                     # only c2+c3, no c1
n3 = ["a", "oa", "iê", "uyê"]                                                         # all c1, c2, c3

closed_vowels = ["ai", "ao", "au", "ay", ...]   # 28 — never have final consonant
suspended_vowels = ["ă", "â", "iê", "oă", "uâ", "uô", "oo", "ôô", "ươ", "uyê"]      # 10 — must have final

[tone_rules]
tones = ["sắc", "huyền", "hỏi", "ngã", "nặng"]
hard_constraint = { codas = ["c", "ch", "p", "t"], allowed_tones = ["sắc", "nặng"] }
```

- [ ] **Step 2:** Author `rules/telex-keymap.toml`:

```toml
[mappings]
# Tone keys (single-press)
"s" = "TONE_SAC"
"f" = "TONE_HUYEN"
"r" = "TONE_HOI"
"x" = "TONE_NGA"
"j" = "TONE_NANG"

# Modifier keys (single-press; 2-char sequences handled by FSM state)
"w" = "MOD_HORN"

# 2-char trigger sequences
[double_sequences]
"aa" = "MOD_CIRCUMFLEX_a"   # → â
"oo" = "MOD_CIRCUMFLEX_o"   # → ô
"ee" = "MOD_CIRCUMFLEX_e"   # → ê
"aw" = "MOD_BREVE_a"         # → ă
"ow" = "MOD_HORN_o"          # → ơ (alt for w-after-o)
"uw" = "MOD_HORN_u"          # → ư (alt for w-after-u)
"dd" = "MOD_D_BAR"           # → đ
```

- [ ] **Step 3:** Author `rules/vni-keymap.toml` (same structure, digit keys).

- [ ] **Step 4:** Implement `RuleParser.load_phonotactics(path)` — parse TOML, validate schema, return structured `PhonotacticsRule` dataclass.

- [ ] **Step 5:** Implement `RuleParser.load_keymap(path)` → `KeymapRule`.

- [ ] **Step 6:** Tests `test_parser.py`: malformed TOML → raise; missing required field → raise; valid TOML → correct dataclass.

- [ ] **Step 7:** Run `pytest tools/fsm-codegen/tests/test_parser.py` — PASS.

### Task 3: Build NFA

**Files:**
- Modify: `tools/fsm-codegen/codegen/nfa.py`

- [ ] **Step 1:** Define dataclass `NfaState`, `NfaTransition`. State = abstract syllable-position label.

- [ ] **Step 2:** Implement `Nfa.from_phonotactics(rules)` — walk syllable structure:
   - Start state `S0`.
   - For each initial consonant: edge `S0 -> S1[cons]` on consonant input.
   - For each vowel from `S1[cons]`: edge to `S2[cons,vowel]`.
   - Apply `n1/n2/n3` constraint when adding final consonant edges.
   - Apply tone overlay: each accept state has 6 variants (1 per tone, including no-tone).
   - Apply suspended/closed-vowel constraints.
   - Accept states = complete syllables.

- [ ] **Step 3:** Apply keymap on top: for each Telex/VNI key, add transitions encoding modifier/tone application.

- [ ] **Step 4:** Apply escape rules: state `MOD_APPLIED + same modifier key` → state `ESCAPE_LITERAL` + emit literal pair.

- [ ] **Step 5:** Tests `test_nfa.py`: sample mini-grammar (just `a` + `s` → `á`, `aa` → `â`, `aaa` → `aa`). Walk NFA → expected accept state.

- [ ] **Step 6:** Pytest PASS.

### Task 4: Subset construction (NFA → DFA)

**Files:**
- Modify: `tools/fsm-codegen/codegen/dfa.py`

- [ ] **Step 1:** Implement `Dfa.from_nfa(nfa)` — subset construction algorithm (Aho/Sethi/Ullman §3.7):
   - Start state = ε-closure of NFA start.
   - For each input symbol, compute move + ε-closure → new DFA state.
   - Continue until fixpoint.

- [ ] **Step 2:** Sanity check: DFA from sample NFA should be deterministic (each state has at most 1 transition per input).

- [ ] **Step 3:** Tests `test_dfa.py`: sample NFA with epsilon transitions → expected DFA.

- [ ] **Step 4:** Pytest PASS.

### Task 5: Hopcroft minimization

**Files:**
- Modify: `tools/fsm-codegen/codegen/dfa.py`

- [ ] **Step 1:** Implement `Dfa.minimize()` via Hopcroft algorithm:
   - Initial partition: {accept_states, non_accept_states}.
   - For each input symbol, refine partition until no further split.
   - Return Dfa with merged equivalent states.

- [ ] **Decision:** Use library `automata-lib` if available (`pip install automata-lib`), else implement from scratch.

- [ ] **Step 2:** Tests `test_dfa.py::test_minimize`: pre-known case (3-state DFA collapses to 2) → assert result.

- [ ] **Step 3:** Pytest PASS.

### Task 6: Apply tone/modifier overlay + escape semantics

**Files:**
- Modify: `tools/fsm-codegen/codegen/nfa.py` or new `codegen/overlay.py`

- [ ] **Step 1:** After base DFA built, cross-product with tone state {NoTone, Sắc, Huyền, Hỏi, Ngã, Nặng}.
- [ ] **Step 2:** For each tone key (TONE_SAC, ...), add transition: `(accept_state, tone_key) → (same_accept_state, tone_set)` + emit `REPLACE_LAST(N, toned_text)`.
- [ ] **Step 3:** For escape: `(toned_state, same_tone_key) → (literal_escape_state)` + emit `REPLACE_LAST(1, "ay")` (literal pair, e.g., `aff → af`).
- [ ] **Step 4:** Validate: tone-on-coda hard constraint (c/ch/p/t → only sắc/nặng) — codegen reject invalid combos.
- [ ] **Step 5:** Tests: escape pattern `lè` + `f` → `lèf` (literal escape). `cafcs` → `các` (tone replacement, not escape because different tones).

### Task 7: Exhaustive verify

**Files:**
- Modify: `tools/fsm-codegen/codegen/verifier.py`

- [ ] **Step 1:** Enumerate all 20,504 valid Vietnamese syllables programmatically (cross-product per phonotactic rules).
- [ ] **Step 2:** For each syllable + each keymap (Telex/VNI), generate the input keystroke sequence that should produce it.
- [ ] **Step 3:** Run sequence through DFA → assert final state corresponds to expected output.
- [ ] **Step 4:** If any failure → print mismatch, exit codegen with error code.
- [ ] **Step 5:** Time budget: full verify ≤ 30 seconds (else codegen too slow for build pipeline).

### Task 8: Emit C++ tables

**Files:**
- Modify: `tools/fsm-codegen/codegen/emitter.py`
- Output (created at run time): `src/core/engine/generated/fsm_table_telex.h` etc.

- [ ] **Step 1:** Implement `CppEmitter.emit_dense(dfa, path)`:

```cpp
// AUTO-GENERATED by tools/fsm-codegen/main.py — DO NOT EDIT
// Source: rules/vietnamese-phonotactics.toml + rules/telex-keymap.toml
// Generated: 2026-05-XX HH:MM:SS
// Source-of-truth license: BSD-3-Clause (Gõ Nhanh Contributors, 2025)

#pragma once
#include <array>
#include <cstdint>

namespace NextKey::Engine::Generated {

struct FsmCell { uint16_t nextState; uint16_t actionId; };

inline constexpr std::size_t kStateCount  = 1234;   // codegen output
inline constexpr std::size_t kInputCount  = 32;
inline constexpr std::size_t kActionCount = 567;

inline constexpr std::array<std::array<FsmCell, kInputCount>, kStateCount> kFsmTableTelex = {{
    /* state 0 */ {{ {0,0}, {1,5}, {0,0}, ... }},
    /* state 1 */ {{ ... }},
    ...
}};

inline constexpr std::array<Action, kActionCount> kActionTable = { ... };

}  // namespace NextKey::Engine::Generated
```

- [ ] **Step 2:** Implement `CppEmitter.emit_sparse(dfa, path)` for fallback (sorted transitions for binary search).

- [ ] **Step 3:** Switch dense vs sparse based on size threshold (80 KB).

- [ ] **Step 4:** Emit keymap tables: `keymap_telex.h`, `keymap_vni.h`, `keymap_combined.h`.

- [ ] **Step 5:** Tests `test_emitter.py`: emit small DFA → verify output compiles via `clang -fsyntax-only`.

### Task 9: CMake integration (deferred until D0)

D-1 tool stands alone. CMake hook added in D0 when engine consumes.

### Task 10: D-1 commits

- [ ] **Step 1:** Atomic commits per task (Task 1 commit, Task 2 commit, ..., Task 8 commit).
- [ ] **Step 2:** Linux pytest CI green.
- [ ] **Step 3:** Run codegen end-to-end: `python -m fsm_codegen --rules rules/ --output /tmp/test-gen/`. Inspect output `.h` for sanity.
- [ ] **Step 4:** Open PR `tooling: FSM codegen tool (Sprint 3 D-1)`. Merge to Main before D0.

---

## D0 — Engine scaffolding

Goal: codegen tool consumed by CMake. Generated `.h` committed to repo. `FsmTable` accessor + RCU `shared_ptr` member in HookEngine. NOT wired to hook callback yet. Linux GTest 1409/1409 still passes.

Branch: `sprint-3/fsm-engine` from Main.

### Task 11: CMake codegen integration

**Files:**
- Modify: `CMakeLists.txt` (root)
- Modify: `src/CMakeLists.txt`

- [ ] **Step 1:** Add `find_package(Python3 REQUIRED COMPONENTS Interpreter)`.
- [ ] **Step 2:** Add `add_custom_command` to run codegen at configure time, output to `${CMAKE_BINARY_DIR}/generated/`.
- [ ] **Step 3:** Add `add_custom_target(codegen DEPENDS generated/fsm_table_telex.h ...)`.
- [ ] **Step 4:** Header search path includes `${CMAKE_BINARY_DIR}/generated/`.
- [ ] **Step 5:** Pre-generated output also COMMITTED to `src/core/engine/generated/` (headless build fallback). CMake picks committed if codegen unavailable.

### Task 12: Generated headers committed

**Files:**
- Create (via tool): `src/core/engine/generated/fsm_table_telex.h`
- Create: `src/core/engine/generated/fsm_table_telex_simple.h`
- Create: `src/core/engine/generated/fsm_table_vni.h`
- Create: `src/core/engine/generated/fsm_table_combined.h`
- Create: `src/core/engine/generated/keymap_telex.h`
- Create: `src/core/engine/generated/keymap_vni.h`
- Create: `src/core/engine/generated/keymap_combined.h`
- Create: `src/core/engine/generated/abstract_input.h` (enum)

- [ ] **Step 1:** Run `python -m fsm_codegen --rules rules/ --output src/core/engine/generated/`.
- [ ] **Step 2:** Commit generated `.h`. Sizes: dense ≤ 100 KB total expected.
- [ ] **Step 3:** CI step: re-run codegen, `git diff` → must be empty (no drift).

### Task 13: FsmDispatcher class skeleton

**Files:**
- Create: `src/core/engine/FsmDispatcher.h`
- Create: `src/core/engine/FsmDispatcher.cpp`

- [ ] **Step 1:** Header skeleton:

```cpp
#pragma once
#include "generated/abstract_input.h"
#include <atomic>
#include <memory>

namespace NextKey::Engine {

struct FsmTable;        // forward
struct Keymap;          // forward
class HistoryRingBuffer;
class CommitUndoSM;
class FsmPluginBus;
class IOutputInjector;  // from Sprint 2

class FsmDispatcher {
public:
    FsmDispatcher(std::shared_ptr<FsmPluginBus> bus,
                  std::shared_ptr<IOutputInjector> initialInjector) noexcept;

    enum class HookVerdict { EATEN, PASS_THROUGH };
    HookVerdict ProcessKey(wchar_t rawKey) noexcept;
    HookVerdict ProcessBackspace() noexcept;

    void SetActiveKeymap(std::shared_ptr<const Keymap> km) noexcept;
    void SetActiveFsmTable(std::shared_ptr<const FsmTable> tbl) noexcept;
    void SetActiveInjector(std::shared_ptr<IOutputInjector> inj) noexcept;

private:
    std::atomic<std::shared_ptr<const Keymap>>          activeKeymap_;
    std::atomic<std::shared_ptr<const FsmTable>>        activeFsmTable_;
    std::atomic<std::shared_ptr<IOutputInjector>>       activeInjector_;
    std::shared_ptr<FsmPluginBus>                       pluginBus_;
    HistoryRingBuffer*                                  history_;
    CommitUndoSM*                                       commitUndo_;
    uint16_t                                            currentState_;
};

}  // namespace NextKey::Engine
```

- [ ] **Step 2:** Source skeleton: ctor stores members, `ProcessKey` returns PASS_THROUGH (stub). All accessors atomic_store.

- [ ] **Step 3:** CMakeLists adds source.

- [ ] **Step 4:** Linux compile clean.

### Task 14: HistoryRingBuffer

**Files:**
- Create: `src/core/engine/HistoryRingBuffer.h`
- Create: `src/core/engine/HistoryRingBuffer.cpp`
- Create: `tests/HistoryRingBufferTest.cpp`

- [ ] **Step 1:** Implement fixed-size ring buffer (`std::array<Record, 256>` + read/write head).
- [ ] **Step 2:** API: `Push`, `PopLast`, `RewindToPrevSyllable`, `Clear`.
- [ ] **Step 3:** Tests: 256-record fill + wrap, push-pop matched, rewind to prev syllable correctly identifies boundary.
- [ ] **Step 4:** Linux GTest PASS.

### Task 15: CommitUndoSM

**Files:**
- Create: `src/core/engine/CommitUndoSM.h`
- Create: `src/core/engine/CommitUndoSM.cpp`
- Create: `tests/CommitUndoSMTest.cpp`

- [ ] **Step 1:** Port commit-undo state machine logic from `HookEngine.cpp` (lines 556-594 per `docs/archive/ghost-chars-chrome-investigation.md`). Same 3-state Idle/Primed/Ready semantics.
- [ ] **Step 2:** Tests: state transitions on space/punct/Enter + Backspace + non-BS input.
- [ ] **Step 3:** Linux GTest PASS.

### Task 16: D0 commit

- [ ] **Step 1:** All scaffolding committed: codegen output + FsmDispatcher skeleton + HistoryRingBuffer + CommitUndoSM. Engine wiring NOT touched.
- [ ] **Step 2:** Linux GTest 1409+ PASS (1409 existing + new HistoryRingBuffer + CommitUndoSM tests).
- [ ] **Step 3:** Windows MSVC compile clean.
- [ ] **Step 4:** Commit message: `Sprint 3 D0: FSM scaffolding (codegen output, FsmDispatcher, HistoryRingBuffer, CommitUndoSM)`.

---

## D1 — FsmDispatcher implementation + diff harness

Goal: FsmDispatcher fully implements `ProcessKey` + `ProcessBackspace` using FSM table lookup. Diff harness runs combined corpus (31,757 cases) through both engines, asserts equality. Initial mismatch <5%.

### Task 17: Implement FsmDispatcher::ProcessKey

**Files:**
- Modify: `src/core/engine/FsmDispatcher.cpp`

- [ ] **Step 1:** Hot path:

```cpp
HookVerdict FsmDispatcher::ProcessKey(wchar_t rawKey) noexcept {
    // 1. Plugin BeforeInput
    auto bus = pluginBus_;
    if (bus->BeforeInput(rawKey) == Verdict::CONSUME) return HookVerdict::EATEN;

    // 2. Map raw key
    auto km = std::atomic_load(&activeKeymap_);
    AbstractInput input = km->map[rawKey];

    // 3. FSM lookup
    auto tbl = std::atomic_load(&activeFsmTable_);
    auto cell = tbl->cells[currentState_][static_cast<size_t>(input)];
    Action action = tbl->actions[cell.actionId];

    // 4. Plugin BeforeTransition (VETO?)
    if (bus->BeforeTransition(currentState_, input) == Verdict::VETO) {
        // emit literal raw key
        return HookVerdict::PASS_THROUGH;
    }

    // 5. Apply transition
    uint16_t prevState = currentState_;
    currentState_ = cell.nextState;

    // 6. Emit action
    auto inj = std::atomic_load(&activeInjector_);
    EmitAction(action, *inj);

    // 7. Record history
    history_->Push({rawKey, input, prevState, currentState_, action});

    // 8. Plugin OnPostAction (ROLLBACK?)
    auto resultBuf = ComputeResultBuf();
    if (bus->OnPostAction(action, resultBuf) == Verdict::ROLLBACK) {
        Rollback();
        return HookVerdict::PASS_THROUGH;
    }

    // 9. Plugin AfterTransition (observers)
    bus->AfterTransition(prevState, input, currentState_, action);

    // 10. CommitUndoSM update
    commitUndo_->OnNonBsInput();

    return HookVerdict::EATEN;
}
```

- [ ] **Step 2:** Implement `EmitAction` (dispatch to `injector_->Replace` etc).
- [ ] **Step 3:** Implement `Rollback` (pop history, undo emit).

### Task 18: Implement FsmDispatcher::ProcessBackspace

**Files:**
- Modify: `src/core/engine/FsmDispatcher.cpp`

- [ ] **Step 1:** BS within syllable: `history_->PopLast()` → restore prev state + undo emit.
- [ ] **Step 2:** BS cross-syllable: `history_->RewindToPrevSyllable()` → revive previous syllable state.
- [ ] **Step 3:** BS at empty history: PASS_THROUGH.

### Task 19: Diff harness

**Files:**
- Create: `tests/FsmDispatcherDiffTest.cpp`
- Create: `tests/corpus/gonhanh-telex-pairs.txt` (downloaded from gonhanh.org/HEAD with attribution header)
- Create: `LICENSE-3RD-PARTY.md`

- [ ] **Step 1:** Download `vietnamese_telex_pairs.txt` from gonhanh.org HEAD (after merge of upstream this commit), commit with attribution header (BSD-3 notice + copyright).
- [ ] **Step 2:** Create `LICENSE-3RD-PARTY.md` listing gonhanh.org BSD-3 attribution + upstream commit hash.
- [ ] **Step 3:** Test fixture loads `gonhanh-telex-pairs.txt` (30,337 lines), parses `telex<TAB>expected`.
- [ ] **Step 4:** For each pair: feed `telex` chars to BOTH engines (`TypingEngine` cũ + `FsmDispatcher` mới), compare output. ASSERT_EQ.
- [ ] **Step 5:** Add: feed `chaos.toml` 11 cases (load via existing TomlLoader). ASSERT_EQ.
- [ ] **Step 6:** Add: feed `TelexEngineTest.cpp` 1409 cases (refactor as parametrized). ASSERT_EQ.
- [ ] **Step 7:** Total: 31,757 ASSERT_EQ. D1 may have <5% mismatch — D2 task to fix.

### Task 20: D1 commit

- [ ] **Step 1:** Commit `Sprint 3 D1: FsmDispatcher impl + diff harness (31,757 cases, initial mismatch X%)`.
- [ ] **Step 2:** CI runs diff harness on Linux, reports % mismatch.

---

## D2 — Fix to diff = 0

Goal: 31,757/31,757 PASS. Audit mismatches, update rule TOML, regen codegen.

### Task 21: Audit mismatches

- [ ] **Step 1:** Diff harness emits report file: `tests/diff-report.txt` listing each mismatch as `<input> | <old_output> | <new_output>`.
- [ ] **Step 2:** Group by category (forward typing / chaos / engine internal). Count per category.
- [ ] **Step 3:** Investigate top 10 mismatches manually. Identify root cause: rule missing? Codegen bug? FSM transition incorrect?

### Task 22: Iterate rule fixes

- [ ] **Step 1:** For each rule fix: update `rules/*.toml`, regen codegen, re-run diff. Decrement mismatch count.
- [ ] **Step 2:** Hard cap: 2 days. If mismatch > 0 after 2 days, escalate to anh — review per-case decision (keep cũ vs accept FSM fix).
- [ ] **Step 3:** **Bug `cafcs → các` resolution**: this case may flip from FAIL to PASS in FSM (because rule encoding makes tone-replacement explicit). Diff harness shows: TypingEngine=`càcs`, FsmDispatcher=`các`. Decision: accept FSM (correct per Vietnamese phonotactics). Document in commit.

### Task 23: D2 commit

- [ ] **Step 1:** All 31,757 cases PASS via FsmDispatcher.
- [ ] **Step 2:** Commit `Sprint 3 D2: 31,757/31,757 PASS, FsmDispatcher ≡ TypingEngine`.

---

## D3 — Plugin event bus + SyllableValidator

Goal: Plugin bus implementation. SyllableValidator extracted from `SpellChecker.cpp`. Free typing toggle still works.

### Task 24: FsmPluginBus

**Files:**
- Create: `src/core/engine/FsmPluginBus.h`
- Create: `src/core/engine/FsmPluginBus.cpp`
- Create: `src/core/engine/IFsmPlugin.h`
- Create: `tests/FsmPluginBusTest.cpp`

- [ ] **Step 1:** Implement `IFsmPlugin` interface (per design §4.3).
- [ ] **Step 2:** Implement `FsmPluginBus` with `Register/Unregister/Dispatch*` methods. Plugin order = registration order.
- [ ] **Step 3:** Tests:
   - Register 2 plugins, both get called.
   - 1st plugin returns CONSUME → 2nd not called.
   - 1st plugin returns VETO → transition not applied.
   - 1st plugin returns ROLLBACK → applied then reverted.
   - Unregister → removed from chain.
- [ ] **Step 4:** GTest PASS.

### Task 25: SyllableValidatorPlugin

**Files:**
- Create: `src/core/engine/plugins/SyllableValidatorPlugin.h`
- Create: `src/core/engine/plugins/SyllableValidatorPlugin.cpp`
- Create: `rules/dictionary.toml` (auto-converted from gonhanh's `vi.dic`)
- Create: `tests/SyllableValidatorPluginTest.cpp`

- [ ] **Step 1:** Convert gonhanh's `vi.dic` (6711 từ, plain text) → `rules/dictionary.toml` (TOML wordlist).
- [ ] **Step 2:** SyllableValidatorPlugin loads dict at startup, builds `std::unordered_set<std::wstring>`.
- [ ] **Step 3:** Implement `OnPostAction`:
   - If `!enabled_` → PASS (free mode).
   - If buf in user exception list → PASS.
   - If buf in dict → PASS.
   - Else → ROLLBACK.
- [ ] **Step 4:** Wire `freeTyping_` config flag (existing, in user.toml) → `validatorPlugin_.SetEnabled(!freeTyping)`.
- [ ] **Step 5:** Tests: dict word PASS, non-dict word ROLLBACK, user exception PASS, free mode all PASS.
- [ ] **Step 6:** Diff harness rerun: 31,757 still PASS.

### Task 26: D3 commit

- [ ] **Step 1:** Commit `Sprint 3 D3: plugin event bus + SyllableValidator (free/strict toggle)`.

---

## D4 — HookEngine swap + chaos

Goal: HookEngine routes through FsmDispatcher. TypingEngine code unreachable but compiled. Chaos 55/55 PASS on Win.

### Task 27: HookEngine route swap

**Files:**
- Modify: `src/app/system/HookEngine.cpp`
- Modify: `src/app/system/HookEngine.h`

- [ ] **Step 1:** Replace `typingEngine_->ProcessChar` calls with `fsmDispatcher_->ProcessKey`. Bridge return value (HookVerdict::EATEN → eat key, PASS_THROUGH → call next hook).
- [ ] **Step 2:** Replace `typingEngine_->ProcessBackspace` with `fsmDispatcher_->ProcessBackspace`.
- [ ] **Step 3:** Wire `fsmDispatcher_` in HookEngine ctor — pass injector pointer (existing) + plugin bus + initial keymap/table.
- [ ] **Step 4:** Wire focus change: `OnFocusChange` → `fsmDispatcher_->SetActiveInjector(newInjector)`.
- [ ] **Step 5:** Wire config change (Sprint 1 D9 MainThreadWorker handler): `freeTyping` config change → `validatorPlugin_->SetEnabled(...)`.
- [ ] **Step 6:** Wire input method change: `inputMethod` config → `fsmDispatcher_->SetActiveKeymap(...)`.

### Task 28: Chaos sweep

- [ ] **Step 1:** Build NexusKey Debug Win.
- [ ] **Step 2:** Run `tools/run-chaos.ps1 -RunnerExe build\tools\Debug\NextKeyTestRunner.exe` (5 host × 11 case sweep).
- [ ] **Step 3:** Capture output: `docs/baselines/perf-baseline-fsm-d4-chaos.{md,csv,xml}`.
- [ ] **Step 4:** Verdict: 55/55 PASS required. p99 not regress > 10% vs Sprint 2 baseline.
- [ ] **Step 5:** If FAIL: stop, atomic revert, audit. Likely a case 31,757 corpus didn't cover.

### Task 29: D4 commit

- [ ] **Step 1:** Commit `Sprint 3 D4: HookEngine routes via FsmDispatcher (chaos 55/55 PASS)`.

---

## D5 — Delete TypingEngine + SpellChecker

Goal: ~1,800 LOC dead code removed. Audit script update. Ship clean.

### Task 30: Delete code

**Files:**
- Delete: `src/core/engine/TypingEngine.cpp` (1,603 LOC)
- Delete: `src/core/engine/TypingEngine.h` (197 LOC)
- Delete: `src/core/engine/SpellChecker.cpp`
- Delete: `src/core/engine/SpellChecker.h`
- Delete: `src/core/engine/EnglishProtection.{h,cpp}` (if logic migrated to plugin or unused)
- Modify: `src/CMakeLists.txt` — drop sources
- Modify: `tools/audit/check_hook_thread_no_mutex.sh` — drop checks for old field names

- [ ] **Step 1:** Verify no `#include` references to deleted files (grep).
- [ ] **Step 2:** Build clean Linux + Windows.
- [ ] **Step 3:** Linux GTest PASS (TelexEngineTest.cpp consumed via diff harness; can delete or keep as archive — decide with anh).
- [ ] **Step 4:** Windows compile clean.

### Task 31: D5 commit

- [ ] **Step 1:** Commit `Sprint 3 D5: delete TypingEngine + SpellChecker (~1800 LOC dead code removed)`.

---

## D6 — Final cleanup + PR

### Task 32: Baseline doc

**Files:**
- Create: `docs/baselines/perf-baseline-fsm-final.md`

- [ ] **Step 1:** Document final chaos 55/55 PASS. Compare p99 vs ChannelTraits (Sprint 2 ending) baseline.
- [ ] **Step 2:** Document GTest count delta (1409 - existing + new tests).
- [ ] **Step 3:** Document memory: `sizeof(FsmTable)` + `sizeof(Keymap)` + dictionary heap.
- [ ] **Step 4:** Reference all 6 D-day commits.

### Task 33: PR open

- [ ] **Step 1:** Push `sprint-3/fsm-engine` branch.
- [ ] **Step 2:** Open PR `feat: FSM engine refactor (Sprint 3 §1 + §4 plugin layer)`.
- [ ] **Step 3:** PR body links: design doc, plan doc, baseline, all 6 commits.
- [ ] **Step 4:** Anh review.

### Task 34: D6 commit

- [ ] **Step 1:** Commit `Sprint 3 D6: baseline + PR prep`.

---

## D7 — Buffer (0-2 days)

If D2 audit > 2 days OR D4 chaos failure required investigation.

---

## Cross-cutting

### Atomic field convention

All RCU pointers (`activeKeymap_`, `activeFsmTable_`, `activeInjector_`) follow Sprint 1 D6 RCU pattern: `std::atomic<std::shared_ptr<const T>>`. Read with `std::atomic_load` (memory_order_acquire), write with `std::atomic_store` (release). Pre-tested in Sprint 1+2; no new concurrency invariant.

### Audit script

`tools/audit/check_hook_thread_no_mutex.sh` (Sprint 1 D7) — D5 task updates regex to drop deleted field names + add Check 6: "no use of `TypingEngine::` in HookEngine.cpp" (regression trap).

### CI matrix

- Linux Debug + GTest (existing).
- Linux + codegen verify (re-run, diff vs committed).
- Windows MSVC Debug compile (existing).
- Windows manual chaos (D4 + D6 only).

### Memory budget assertions

`static_assert(sizeof(FsmTable) <= 100 * 1024)` at compile.
`static_assert(sizeof(Keymap) <= 1024)` at compile.

### Naming

| Symbol | Source-of-truth |
|---|---|
| `NextKey::Engine::FsmDispatcher` | `src/core/engine/FsmDispatcher.h` |
| `NextKey::Engine::FsmTable` | `generated/fsm_table_*.h` |
| `NextKey::Engine::Keymap` | `generated/keymap_*.h` |
| `NextKey::Engine::AbstractInput` | `generated/abstract_input.h` |
| `NextKey::Engine::IFsmPlugin` | `src/core/engine/IFsmPlugin.h` |
| `NextKey::Engine::FsmPluginBus` | `src/core/engine/FsmPluginBus.h` |
| `NextKey::Engine::HistoryRingBuffer` | `src/core/engine/HistoryRingBuffer.h` |
| `NextKey::Engine::CommitUndoSM` | `src/core/engine/CommitUndoSM.h` |
| `NextKey::Engine::Plugins::SyllableValidatorPlugin` | `src/core/engine/plugins/SyllableValidatorPlugin.h` |

---

## Risk table

| Risk | Probability | Mitigation |
|---|---|---|
| Codegen tool dev > 1 week | Medium | Use `automata-lib` library if Hopcroft impl complex. Time-box D-1 to 1 week, escalate. |
| D2 mismatch > 2 days | Medium | Per-case review with anh. Some cases may accept FSM-correct (vd `cafcs`) over TypingEngine. |
| D4 chaos < 55/55 | Low (corpus 31,757 + 11 chaos covers most) | Atomic revert D4. Add failing case to corpus. Retry next day. |
| User custom keymap (#111) early-need | Low | UI defer to FSM-2. Keymap loading API designed extensible. |
| Performance regression > 10% p99 | Low (FSM 87ns vs scan-back µs) | Benchmark gate at D6. If regress, investigate cache misses in dense table. |
| Generated `.h` drift | Low | CI gate re-runs codegen, asserts no diff. |
| License attribution missed | Low (audited) | `LICENSE-3RD-PARTY.md` + file headers (Task 19). |

---

## Sign-off

Plan derived from [`2026-05-05-fsm-engine-refactor-design.md`](2026-05-05-fsm-engine-refactor-design.md). All 8 brainstorm decisions locked. Ready to start D-1.

**Next concrete step:** create branch `sprint-3/codegen-tool` from Main, begin Task 1.
