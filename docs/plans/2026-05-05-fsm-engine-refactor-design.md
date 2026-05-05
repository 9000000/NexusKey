# FSM Engine Core Refactor — Design

> **Branch:** `design/fsm-engine-refactor` (this branch — design doc only)
> **Implementation branch:** `sprint-3/fsm-engine` (to be created at D0)
> **Foundation:** [`docs/PHILOSOPHY.md`](../PHILOSOPHY.md) · [`docs/CODE_GOVERNANCE.md`](../CODE_GOVERNANCE.md) §1 (FSM core) + §4 (plugin layer) · [`docs/CODING_RULES/11-hook-system-rules.md`](../CODING_RULES/11-hook-system-rules.md) · [`docs/RuleTiengViet_Summary.md`](../RuleTiengViet_Summary.md)
> **Predecessor:** Sprint 1 (single-owner hook) + Sprint 2 T3 (IOutputInjector) — both shipped on Main.
> **Baseline before FSM-1:** chaos 55/55 PASS on Main, GTest 1409/1409 PASS Linux. FSM-1 must preserve.
> **Status:** Design (brainstormed 2026-05-05). Ready for plan-doc → implementation.

---

## 0. CODE_GOVERNANCE — 5-Question Pre-Code Gate

### Q1 — Layer Check

FSM lives in `src/core/engine/` (replaces `TypingEngine.cpp` + `SpellChecker.cpp`). Three sub-layers:

```
[Hook callback] → [Input Mapping]    → [FSM Core]      → [Plugin Bus] → [IOutputInjector]
                       ↑                    ↑                ↑
                  src/core/engine/     src/core/engine/  src/core/engine/
                  InputMapping.{h,cpp} FsmDispatcher    FsmPluginBus
                                       FsmTable (data)   IFsmPlugin
```

Hook callback layer (`src/app/system/HookEngine.cpp`) is unchanged in topology — it now calls `FsmDispatcher::Process(rawKey)` instead of `TypingEngine::ProcessChar`. Output dispatch via `IOutputInjector` (Sprint 2 T3) preserved.

CODE_GOVERNANCE §1 (FSM core) + §4 (plugin extension) realized; §2 (output) already shipped Sprint 2; §3 (SPSC) remains roadmap (Sprint 4).

### Q2 — Performance Impact

Hot-path cost per keystroke after FSM-1:

| Step | Cost |
|---|---|
| `std::atomic_load(&activeKeymap_)` (RCU shared_ptr) | ~10 ns |
| `keymap[rawKey]` → abstract input | ~1 ns (array lookup) |
| `std::atomic_load(&fsmTable_)` | ~10 ns |
| `table[state][input]` → (nextState, action) | ~5 ns (table cell, cache-line) |
| Plugin chain (1 plugin: SyllableValidator strict mode) | ~50 ns (1 hash lookup) |
| Action emit via `injector_->Replace(...)` | ~11 ns (Sprint 2 baseline) |
| **Total per key** | **≈ 87 ns** |

Current `TypingEngine::ProcessChar` measured ~36-106 µs (per `docs/plans/hot-path-optimization-plan.md`). FSM expected **~3 orders of magnitude faster**. Real benefit: deterministic O(1) lookup vs scan-back loop variability.

Hook 1 ms budget (Rule #11.1) preserved with vast headroom.

### Q3 — Native Alternative

| Alternative considered | Rejected because |
|---|---|
| Keep `TypingEngine.cpp` if/else; minor cleanups | 1,603 LOC with 9 spell-check branches; bug `cafcs → các` traceable to scan-back complexity. Future extensions (VNI, Telex+VNI combined, user-custom keymap from issue #111) compound complexity super-linearly. |
| Hand-write FSM 2D table | Vietnamese phonotactics (20,504 valid syllables per `RuleTiengViet_Summary.md`) too large to hand-encode without errors. Maintenance impossible — 1 rule edit = re-trace many cells. |
| FSM hybrid (table for simple cases + scan-back for modifier rules) | Splits source-of-truth across two implementations; same maintainability problem in smaller form. |
| **FSM with codegen tool (CHOSEN)** | Single source-of-truth = rule files; tool generates table; exhaustive verify proves coverage. Codegen tool itself reusable for future languages (Lao/Khmer extension D). |

### Q4 — No-Lock / No-Exception Rule

FSM hot path:
- **Lock-free**: 2 atomic_load (RCU activeKeymap_, fsmTable_, injector_), 1 array lookup, optional plugin chain. No mutex.
- **No exception**: all paths `noexcept`. Action emit may fail (injector returns false) → handled via Verdict::ROLLBACK in plugin chain.
- **Bounded work**: O(1) per key (table cell + bounded plugin chain).

Cold path (focus change, config change, plugin register/unregister): can lock; runs on `MainThreadWorker` from Sprint 1 D8-D10.

### Q5 — Trade-Off Disclosure

| Trade-off | Cost | Benefit |
|---|---|---|
| Codegen tool engineering (~1 week dev) | New tool, Python dependency at build time. Generated `.h` committed so headless build works. | Single source-of-truth, exhaustive verification, future-proofing for new languages/methods. |
| 50-80 KB resident memory for FSM tables | +0.05% NexusKey RSS. | O(1) lookup vs scan-back loop. |
| Plugin event bus indirection | 1 vtable jump per registered plugin per event. | Clean extension; macro/auto-cap/CJK/spell-check all unify under one contract. |
| Migration via parallel-run diff harness | Test corpus must contain reference outputs (provided by `vietnamese_telex_pairs.txt` from gonhanh.org, BSD-3, 30,337 pairs). | High-confidence cut-over without user-visible regression. |

---

## 1. Locked Decisions (Brainstorm 2026-05-05)

This section records 8 decisions locked in conversation with anh. Each affects scope; revisit only if new evidence contradicts.

| # | Decision | Rationale |
|---|---|---|
| **1** | **Migration style: incremental D-day** (B). One commit per D, chaos PASS gate per commit. Bisect-friendly. | Same pattern Sprint 1 + Sprint 2 used; shipped 55/55 chaos. |
| **2** | **Sprint scope: FSM core + Telex/VNI/Combined preset built-in + plugin event bus contract.** Macro/auto-cap/CJK plugin migration deferred to FSM-2 sprint. UI custom keymap (issue #111) deferred. | Engine refactor + UI form are different disciplines. Bundling violates Q5. |
| **3** | **Spell-check is a plugin** (`SyllableValidatorPlugin`). Toggle "Gõ tự do" = plugin off. | Aligns with anh's mental model: "core minimal, everything else plugin". Macro/auto-cap/CJK same pattern (sprint sau). |
| **4** | **Shorthand `cc→ch` is a plugin**, not FSM core. | Shorthand is optional gõ-tắt feature, not part of orthographic transition. Plug via `OnPattern` event. |
| **5** | **Spell-check has 2 modes**: strict (dictionary lookup ~6711 từ) vs free (orthography only, FSM by construction). Plugin enabled/disabled toggles. | User clarified: strict = check dictionary, free = no check beyond orthography. |
| **6** | **FSM 2D table via codegen tool** (option A from brainstorm). NFA → DFA → minimize → emit constexpr. | Pure FSM realizes governance §1 fully. Codegen tool reusable for new languages. |
| **7** | **Dictionary source: `vi.dic` from gonhanh.org** (BSD-3, 6711 từ). | License-compatible with NexusKey GPL-3. Skip PHTV (AGPL conflict) and xkey (Swift parse). |
| **8** | **Diff verification corpus: `vietnamese_telex_pairs.txt` from gonhanh.org** (BSD-3, 30,337 pairs). | 20× current `TelexEngineTest.cpp` (1409 cases). Production-grade Telex test corpus. |

### What's NOT in scope this sprint

- Macro plugin migration (lives in HookEngine cũ, plug bus chừa hook point)
- Auto-cap plugin migration (same)
- CJK detection plugin migration (same)
- Issue #111 UI custom keymap form
- Pass-through plugin (§4 governance fallback)
- New language tables (Lao/Khmer)
- Bug `cafcs → các` separate fix (will resolve naturally via codegen rule + diff harness)

---

## 2. Architecture

### 2.1 Layer diagram (full data flow)

```
┌──────────────────────────────────────────────────────────────────────┐
│ HOOK CALLBACK (LowLevelKeyboardProc) — 1 ms budget, lock-free         │
│                                                                        │
│  rawKey ─► InputMapping ─► FsmDispatcher ─► PluginBus ─► Injector     │
│              │                  │              │            │          │
│              ▼                  ▼              ▼            ▼          │
│          activeKeymap_      activeFsmTable_  registered   activeInj_   │
│         (atomic_load)      (atomic_load)    plugins[]   (atomic_load) │
│                                                                        │
│          + HistoryRingBuffer<Record, 256>  ◄─ write per transition     │
│          + CommitUndoSM                    ◄─ tracks Idle/Primed/Ready │
└──────────────────────────────────────────────────────────────────────┘
                                     │
                                     ▼
┌──────────────────────────────────────────────────────────────────────┐
│ COLD PATH (MainThreadWorker, WinEventProc) — can lock                 │
│                                                                        │
│  - InputMethodRegistry: publish activeKeymap_ on config change        │
│  - FsmTableRegistry: publish activeFsmTable_ on language change       │
│  - FocusTracker: publish activeInjector_ on EVENT_OBJECT_FOCUS         │
│  - PluginRegistry: register/unregister IFsmPlugin instances           │
└──────────────────────────────────────────────────────────────────────┘
```

### 2.2 Components on hook (must, lock-free)

| Component | Responsibility | Memory |
|---|---|---|
| `InputMapping` | rawKey → AbstractInput. RCU `shared_ptr<KeymapTable>`. | ~250-400 byte per keymap; 4 keymaps total |
| `FsmDispatcher` | `(state, abstractInput) → (nextState, Action)`. RCU `shared_ptr<FsmTable>`. | 50-80 KB per table |
| `HistoryRingBuffer<Record, 256>` | Last 256 transitions for BS rewind. SPSC same-thread, no lock. | 256 × ~16 byte = 4 KB |
| `CommitUndoSM` | Idle/Primed/Ready state machine for "BS revoke commit" semantics. | Trivial, ~16 byte |
| `FsmPluginBus::Dispatch` | Iterate registered plugins, call event method, collect Verdict. | Trivial |

### 2.3 Components off hook (cold path, can lock)

| Component | Trigger | Where |
|---|---|---|
| `InputMethodRegistry::Publish` | User changes "Kiểu gõ" in Settings | UI → `MainThreadWorker.Signal()` → atomic_store |
| `FsmTableRegistry::Publish` | Language change (future) | Same path |
| `FocusTracker` | `SetWinEventHook` → EVENT_OBJECT_FOCUS | Already exists Sprint 1 D9; reuse |
| `IFsmPlugin::Register/Unregister` | App startup, config toggle | `MainThreadWorker.Post` with copy-publish-swap |

---

## 3. FSM Core Specification

### 3.1 State

State is an opaque `uint16_t` ID assigned by codegen tool after DFA minimization.

Conceptually each state encodes a partial-syllable pattern:
- consonant initial position state
- vowel sequence state (1/2/3 vowels seen, which ones)
- final consonant state (none / C1 / C2 / C3)
- tone applied state (none / sắc / huyền / hỏi / ngã / nặng)
- modifier applied state (none / circumflex / breve / horn / d-bar)
- escape pending flag (after `aaa → aa` literal)

Codegen produces ~500-2000 minimized states.

### 3.2 Abstract input

Finite enum used for FSM table indexing (~32 values):

```cpp
enum class AbstractInput : uint16_t {
    // Tone keys (mapped from raw via active keymap)
    TONE_SAC, TONE_HUYEN, TONE_HOI, TONE_NGA, TONE_NANG,

    // Modifier keys
    MOD_CIRCUMFLEX,    // Telex: aa/oo/ee, VNI: 6
    MOD_BREVE,         // Telex: aw, VNI: 8
    MOD_HORN,          // Telex: w/uw/ow, VNI: 7
    MOD_D_BAR,         // Telex: dd, VNI: 9

    // Literal vowel/consonant inserts
    INSERT_LITERAL_a, INSERT_LITERAL_b, ..., INSERT_LITERAL_z,

    // Special
    SYLLABLE_BOUNDARY, // space, punct, Enter
    BACKSPACE,
    PASSTHROUGH        // raw key, FSM emits as-is
};
```

### 3.3 Action

Output emitted to `IOutputInjector`:

```cpp
struct Action {
    enum Kind : uint8_t { NOOP, INSERT_CHAR, REPLACE_LAST, PASS_THROUGH, RESET };
    Kind kind;
    uint8_t bsCount;      // for REPLACE_LAST
    wchar_t replacement[6]; // small-string-optimized; max Vietnamese syllable ~7
};
```

Action dispatch:
- `NOOP` → no-op (FSM in escape buffer, no output yet)
- `INSERT_CHAR(c)` → `injector.Replace(0, &c)` (1 char insert)
- `REPLACE_LAST(N, text)` → `injector.Replace(N, text)`
- `PASS_THROUGH` → return CallNextHookEx (do not eat key)
- `RESET` → clear FSM state, history, commit-undo SM

### 3.4 FsmTable layout

```cpp
struct FsmCell {
    uint16_t nextState;
    uint16_t actionId;   // index into ActionTable
};

struct FsmTable {
    static constexpr size_t kStateCount  = N;   // codegen output
    static constexpr size_t kInputCount  = 32;
    FsmCell cells[kStateCount][kInputCount];
    Action  actions[kActionCount];
};
```

Sparse encoding fallback if dense > 100 KB:
```cpp
struct FsmTransition { uint16_t state; uint16_t input; uint16_t nextState; uint16_t actionId; };
struct FsmTableSparse { FsmTransition transitions[N]; /* sorted, binary search */ };
```

Codegen picks dense vs sparse based on size threshold.

### 3.5 InputMapping (keymap)

```cpp
struct Keymap {
    static constexpr size_t kRawKeyMax = 256;  // ASCII range covers all telex/VNI
    AbstractInput map[kRawKeyMax];              // PASSTHROUGH default
};
```

Compile-time generated from `rules/telex-keymap.toml` etc:

```toml
# rules/telex-keymap.toml
[mappings]
"s" = "TONE_SAC"
"f" = "TONE_HUYEN"
"r" = "TONE_HOI"
"x" = "TONE_NGA"
"j" = "TONE_NANG"
"w" = "MOD_HORN"

# 2-char sequences: handled by FSM state, not keymap.
# (e.g., 'aa' triggers MOD_CIRCUMFLEX via state 'after-a' + input 'INSERT_LITERAL_a')
```

VNI keymap:
```toml
"1" = "TONE_SAC"
"2" = "TONE_HUYEN"
"3" = "TONE_HOI"
"4" = "TONE_NGA"
"5" = "TONE_NANG"
"6" = "MOD_CIRCUMFLEX"
"7" = "MOD_HORN"
"8" = "MOD_BREVE"
"9" = "MOD_D_BAR"
```

Combined keymap = union (Telex letters + VNI digits, disjoint, no conflict).

---

## 4. HistoryRingBuffer + Commit-undo SM + Plugin Event Bus

### 4.1 HistoryRingBuffer<Record, 256>

```cpp
struct Record {
    wchar_t       rawKey;          // raw input (e.g., 'f')
    AbstractInput abstractInput;   // mapped (e.g., TONE_HUYEN)
    uint16_t      preState;        // FSM state before transition
    uint16_t      postState;       // FSM state after
    Action        action;          // Action emitted
};

class HistoryRingBuffer {
public:
    void Push(Record r) noexcept;      // O(1)
    bool PopLast(Record& out) noexcept; // O(1) — for BS within syllable
    bool RewindToPrevSyllable(uint16_t& restoredState, std::vector<Record>& popped) noexcept;
};
```

**Backspace handling in FsmDispatcher:**

```
BACKSPACE received:
   if currentState == EMPTY (no syllable in progress):
       if history.HasPrevSyllable():
           // Cross-syllable BS — revive previous syllable
           history.RewindToPrevSyllable() → restoredState, popped[]
           setState(restoredState)
           injector.Replace(popped.totalChars, "")  // erase previous syllable text
           // Now user can type tone/modifier to fix word
           return EATEN
       else:
           // Nothing to rewind; let BS pass through
           return PASSTHROUGH
   else:
       // BS within current syllable
       history.PopLast() → record
       setState(record.preState)
       injector.Replace(record.action.replacement.size(), "")
       return EATEN
```

256 records ≈ 40 từ Việt trung bình. Beyond that, BS passes through (OS handles raw delete).

### 4.2 CommitUndoSM

3 states retained from current HookEngine:

```cpp
enum class CommitState { Idle, Primed, Ready };

class CommitUndoSM {
    CommitState state_ = Idle;
public:
    void OnSyllableBoundary() noexcept;  // space/punct/Enter → Primed
    void OnBackspace() noexcept;          // Primed → Ready (revoke commit)
    void OnNonBsInput() noexcept;         // Primed → Idle (commit sealed)
};
```

Lives in core (always-on, not plugin) because it's tied to FSM transitions (every input checks).

### 4.3 Plugin Event Bus

```cpp
enum class Verdict : uint8_t { PASS, VETO, ROLLBACK, CONSUME };

class IFsmPlugin {
public:
    virtual ~IFsmPlugin() = default;
    virtual const char* Name() const noexcept = 0;

    // Pre-transition hooks
    virtual Verdict BeforeInput(wchar_t rawKey)                                    { return Verdict::PASS; }
    virtual Verdict BeforeTransition(uint16_t state, AbstractInput input)          { return Verdict::PASS; }

    // Post-transition observers
    virtual void    AfterTransition(uint16_t prev, AbstractInput in,
                                    uint16_t next, const Action& a)                {}

    // Result-validity gate (spell-check uses this)
    virtual Verdict OnPostAction(const Action& action,
                                  std::wstring_view resultBuf)                      { return Verdict::PASS; }

    // Word/syllable boundary observers (macro/auto-cap)
    virtual void    OnWordBoundary(std::wstring_view committedWord)                {}
    virtual void    OnCommit(std::wstring_view syllable)                           {}

    // Edge cases
    virtual void    OnInvalidTransition(uint16_t state, AbstractInput input)       {}
    virtual void    OnFocusChange(HWND hwnd)                                       {}

    // Lifecycle
    virtual void    OnRegister()                                                    {}
    virtual void    OnUnregister()                                                  {}
};
```

**Verdict semantics:**
- `PASS`: continue normally
- `VETO`: transition not applied; FSM emits PASSTHROUGH instead
- `ROLLBACK`: transition applied then reverted (undo via injector + history.PopLast)
- `CONSUME`: skip remaining plugins for this event

**Plugin registration order matters** (first-registered runs first). Sprint FSM-1 only registers `SyllableValidatorPlugin`. Future plugins:

| Plugin (sprint sau) | Hook |
|---|---|
| `MacroExpansionPlugin` | `OnWordBoundary` |
| `AutoCapitalizationPlugin` | `OnCommit` + `OnPostAction` |
| `CjkDetectionPlugin` | `OnFocusChange` |
| `ShorthandPlugin` (cc→ch) | `BeforeTransition` |
| `EnglishProtectionPlugin` (#issue protect) | `OnPostAction` |
| `PassThroughFallbackPlugin` (governance §4) | `OnInvalidTransition` |

### 4.4 SyllableValidatorPlugin (in scope FSM-1)

```cpp
class SyllableValidatorPlugin final : public IFsmPlugin {
    bool                    enabled_ = true;       // toggle "Gõ tự do" = false
    std::unordered_set<std::wstring> dict_;        // ~6711 từ from vi.dic
    std::unordered_set<std::wstring> userException_; // user whitelist for #111

public:
    Verdict OnPostAction(const Action& a, std::wstring_view buf) override {
        if (!enabled_) return Verdict::PASS;                  // free typing
        if (userException_.count(std::wstring(buf))) return Verdict::PASS;
        if (dict_.count(std::wstring(buf))) return Verdict::PASS;
        // Orthography is enforced by FSM by construction; we only veto when
        // dictionary requires meaningful word.
        return Verdict::ROLLBACK;
    }
};
```

Free mode = `enabled_ = false`. Strict mode = `enabled_ = true`. UI exposes both as `freeTyping_` config (existing key, semantic preserved).

---

## 5. Codegen Pipeline

### 5.1 Source-of-truth files

```
rules/
  vietnamese-phonotactics.toml   ← Vietnamese syllable structure + N1/N2/N3 + exceptions
                                    Authored by hand from RuleTiengViet_Summary.md
                                    Versioned; source of all FSM tables
  telex-keymap.toml              ← raw key → AbstractInput
  telex-simple-keymap.toml       ← subset (no shorthand)
  vni-keymap.toml                ← VNI digit → AbstractInput
                                    Telex+VNI combined = union, generated automatically

  dictionary.toml                ← ~6711 từ (from gonhanh.org/vi.dic, BSD-3)
                                    + user exception list (separate file, runtime-loaded)
```

### 5.2 Tool

`tools/fsm-codegen/main.py` (~500 LOC):

```
Input: rules/*.toml
Steps:
  1. Parse phonotactic rules → build NFA
     - States = partial-syllable patterns
     - Transitions = consonant/vowel/tone/modifier sequences
     - Accept states = complete valid syllables (orthographic)
  2. Subset construction → DFA
  3. Hopcroft minimization → minimized DFA
  4. Apply tone/modifier overlay (cross-product with tone state, escape rules)
  5. Verify exhaustively:
       enumerate 20,504 valid syllables (per RuleTiengViet)
       simulate input sequence on DFA
       assert FINAL state corresponds to syllable's representation
       any failure → fail codegen
  6. Emit C++ files:
       src/core/engine/generated/fsm_table_telex.h
       src/core/engine/generated/fsm_table_telex_simple.h
       src/core/engine/generated/fsm_table_vni.h
       src/core/engine/generated/fsm_table_combined.h
       src/core/engine/generated/keymap_*.h
```

### 5.3 Build integration

- CMake: `add_custom_command(... COMMAND python3 tools/fsm-codegen/main.py ...)` runs at configure time.
- Generated `.h` files **committed to git** so headless build (no Python) works. Common pattern (e.g., bison/flex output).
- CI gate: re-run codegen on PR, diff committed vs regenerated → must match.
- Local dev: developers re-run codegen after editing rules; `make codegen` shortcut.

### 5.4 Sparse vs dense encoding

```python
# In main.py:
dense_size = len(states) * len(inputs) * sizeof_cell
if dense_size <= 80 * 1024:  # 80 KB threshold
    emit_dense()
else:
    emit_sparse()  # binary-search transitions
```

---

## 6. Migration Plan (D-day)

Single PR at end of D6. Each D-day commits chaos PASS or skip with explanation.

| Day | Task | DoD |
|---|---|---|
| **D-1** | Codegen tool dev (1 week pre-sprint) | `tools/fsm-codegen/` Python pkg + unit tests + sample rule input. Linux-only, no project dep. |
| **D0** | Codegen tool emit → `src/core/engine/generated/*.h` committed. `FsmTable` struct + accessors. Linux GTest unchanged. **Not wired to engine yet.** | Generated `.h` PASS exhaustive 20,504-syllable verify. CMake `make codegen` works. |
| **D1** | `FsmDispatcher` class + `InputMapping` + `HistoryRingBuffer`. **Diff harness** `tests/FsmDispatcherDiffTest.cpp` runs `vietnamese_telex_pairs.txt` (30,337 cases) through both engines. | Diff <5% mismatch initial. Engine cũ untouched. |
| **D2** | Audit + fix mismatches. Update rule TOML, regen codegen. Until **diff = 0**. | 30,337/30,337 PASS. TelexEngineTest 1409/1409 still PASS via FsmDispatcher. |
| **D3** | Plugin event bus + `SyllableValidatorPlugin` (extracted from `SpellChecker`). Free typing toggle still works. | Plugin contract test PASS. Diff harness 30,337 PASS. Strict + free toggle test PASS. |
| **D4** | HookEngine swap: `LowLevelKeyboardProc` routes via `FsmDispatcher`. TypingEngine code dead but compiled. **Chaos 55/55 PASS.** Run `tools/run-chaos.ps1`. | Chaos baseline `perf-baseline-fsm-d4.{md,csv,xml}` committed, 55/55 PASS. |
| **D5** | Delete `TypingEngine.cpp` + `SpellChecker.cpp`. Delete redundant tests (replaced by codegen verify + diff harness). Update `tools/audit/check_hook_thread_no_mutex.sh` regex. | Linux GTest PASS, build clean, dead code removed. ~1,800 LOC deleted. |
| **D6** | Final cleanup, baseline doc, PR review. Open `feat: FSM engine refactor (Sprint 3 §1)` PR. | PR open, baseline `perf-baseline-fsm.md` committed, ready merge. |

**Estimated wall-time**: 1.5-2 weeks (D-1 codegen 1 week + D0-D6 6 days).

### 6.1 Rollback strategy

If D4 chaos < 55/55:
- Atomic revert D4 commit (engine swap). FsmDispatcher remains compiled but unused.
- Re-audit diff harness — likely a case `vietnamese_telex_pairs.txt` doesn't cover.
- Add to corpus, fix rule, retry D4 next day.

No feature flag (per `feedback_no_backwards_compat_hacks` memory). Atomic revert is cleaner.

### 6.2 Bug `cafcs → các` resolution

Currently a TODO bug. Expected resolution path:
1. D2 diff harness includes `cafcs\tcác` test pair (codegen check).
2. If TypingEngine cũ produces `càcs` → diff != 0 → audit reveals SpellChecker over-rejection.
3. Codegen rule encoding makes `(VOWEL_a + TONE_huyền) + TONE_sắc → VOWEL_a + TONE_sắc` explicit transition. FSM produces `các` correctly.
4. D5 deletes SpellChecker; bug gone.

If `cafcs` not in `vietnamese_telex_pairs.txt`, D2 budget includes adding it manually.

---

## 7. Testing Strategy

### Test pyramid

```
┌─────────────────────────────────────────────┐
│  Manual E2E (Win, post-merge)               │  Daily-use defense
│  - anh dùng NexusKey 1-2 tuần               │
├─────────────────────────────────────────────┤
│  Integration (CI matrix Linux + Win)        │  D4 + D6 gate
│  - run-chaos.ps1: 5 host × 11 case = 55/55  │
│  - sustained.toml: 0% drift                  │
├─────────────────────────────────────────────┤
│  Diff harness (CI Linux)                    │  D1-D2 gate
│  - tests/FsmDispatcherDiffTest.cpp          │
│  - 30,337 cases gonhanh telex pairs         │
│  - 1,409 cases TelexEngineTest              │
│  - 11 cases chaos.toml on Linux             │
│  - Required: 31,757/31,757 ASSERT_EQ        │
├─────────────────────────────────────────────┤
│  Unit (CI Linux)                            │  Always PASS
│  - FsmDispatcherTest: hot-swap, escape      │
│  - InputMappingTest: 4 keymap variants       │
│  - HistoryRingBufferTest: rewind cases      │
│  - FsmPluginBusTest: VETO/ROLLBACK/CONSUME  │
│  - SyllableValidatorTest: dict+free+excpt   │
├─────────────────────────────────────────────┤
│  Codegen (D0 build-time)                    │  D0 gate
│  - tools/fsm-codegen/tests: parse/NFA/DFA   │
│  - exhaustive 20,504 syllable verify        │
└─────────────────────────────────────────────┘
```

### Performance gate

- Hot-path microbenchmark in `tests/FsmDispatcherBenchTest.cpp`. Asserts: per-key dispatch ≤ 1 µs (1000× margin vs 1 ms hook budget).
- Memory: `static_assert(sizeof(FsmTable) <= 100 * 1024)` at compile.

### Chaos preservation

- Sprint 2 baseline `perf-baseline-channeltraits-chaos.md`: 55/55 PASS.
- FSM-1 baseline must match or improve. Worst-case p99 not regress > 10%.

---

## 8. Open Questions (Resolved)

| # | Question | Resolution |
|---|---|---|
| 1 | Codegen tool language? | Python (~500 LOC). |
| 2 | Spell-check dictionary? | gonhanh.org `vi.dic` 6711 từ (BSD-3). |
| 3 | Rollback if D4 chaos fail? | Atomic revert D4 commit. No feature flag. |
| 4 | Backward compat user config? | Preserve `freeTyping`, `vietnameseMode` semantics. |
| 5 | Naming class chính? | `FsmDispatcher`, `IFsmPlugin`, `FsmTable`. |
| 6 | Plugin migration order sprint sau? | Defer; not FSM-1 scope. |
| 7 | Issue #111 UI form? | FSM-2 sprint, post-FSM-1 stable 1-2 weeks. |
| 8 | CI codegen verify? | Separate gh-actions job, not block local dev. |

## 9. Open Questions (Remaining — answer at plan-doc time)

These need lock-in before D-1 starts coding. Minor enough to defer to plan doc.

- DSL syntax for `vietnamese-phonotactics.toml`: hand-design vs reuse existing format (e.g., gonhanh's `telex_doubles.rs`)?
- Dense vs sparse threshold: 80 KB? Or measure empirically and pick lower?
- Plugin priority/order: register-time order, or explicit priority field?
- Diff harness fail mode: hard fail (CI red) on first mismatch, or collect all mismatches into report?
- Linux-only vs Windows codegen: tool runs on dev's machine; do we ship Windows builders?

---

## 10. References

### NexusKey docs
- [`docs/PHILOSOPHY.md`](../PHILOSOPHY.md) — Pillar 1-4 + project_three_questions
- [`docs/CODE_GOVERNANCE.md`](../CODE_GOVERNANCE.md) §1 (FSM core), §4 (plugin extension)
- [`docs/CODING_RULES/11-hook-system-rules.md`](../CODING_RULES/11-hook-system-rules.md) — 1 ms hot-path budget
- [`docs/RuleTiengViet_Summary.md`](../RuleTiengViet_Summary.md) — Vietnamese phonotactics (20,504 syllables)
- [`docs/plans/sprint-1-single-owner-refactor.md`](sprint-1-single-owner-refactor.md) — Sprint 1 D-day pattern
- [`docs/plans/sprint-2-output-injector.md`](sprint-2-output-injector.md) — Sprint 2 T3 IOutputInjector
- [`docs/plans/sprint-2-output-injector-plan.md`](sprint-2-output-injector-plan.md) — Sprint 2 implementation plan reference

### External (license-compatible)
- [gonhanh.org](https://github.com/khaphanspace/gonhanh.org) — BSD-3-Clause
  - `core/src/data/dictionaries/vi.dic` (6,711 từ) → `rules/dictionary.toml`
  - `core/tests/data/vietnamese_telex_pairs.txt` (30,337 cases) → `tests/corpus/gonhanh-telex-pairs.txt`
  - Attribute in `LICENSE-3RD-PARTY.md` + file header
- [PHTV](https://github.com/PhamHungTien/PHTV) — AGPL-3.0 — **SKIP** (license conflict with NexusKey GPL-3)
- [xkey](https://github.com/xmannv/xkey) — MIT — **SKIP** (Swift code, parse cost > benefit)

### Algorithms
- Hopcroft DFA minimization — Wikipedia + libraries `automata-lib` / `pyfsa`
- NFA → DFA subset construction — standard textbook (Aho/Sethi/Ullman §3.7)

### Issues
- [#111](https://github.com/phatMT97/NexusKey/issues/111) — User-defined custom keymap (deferred FSM-2)

---

## 11. Glossary

- **FSM** — Finite State Machine. Here: 2D table indexed by (state, abstract_input) returning (next_state, action).
- **DFA** — Deterministic FSM. Codegen output after subset construction + minimization.
- **NFA** — Non-deterministic FSM. Codegen intermediate.
- **AbstractInput** — Enum of post-keymap abstract symbols (TONE_SAC, MOD_HORN, INSERT_LITERAL_a, etc).
- **Keymap** — `array[256] of AbstractInput`, raw key → abstract input. Telex/VNI/Combined are different keymaps over same FSM.
- **Hopcroft minimization** — Algorithm to reduce DFA to minimal equivalent.
- **RCU** — Read-Copy-Update. Lock-free publish via `std::atomic<std::shared_ptr<T>>`. Used Sprint 1 D6 for `config_`, Sprint 2 D3 for `injector_`, here for `fsmTable_` + `keymap_`.
- **Verdict** — Plugin return value: PASS / VETO / ROLLBACK / CONSUME.
- **Diff harness** — Test that runs both old engine + new FSM on same input corpus, asserts identical output.
- **Parallel-run** — Compiled-in coexistence of old + new engine, output compared. NOT runtime — D1-D2 only.

---

## 12. Sign-off

This design was brainstormed conversationally on 2026-05-05, based on user's philosophy "Nhanh / Nhẹ / Mượt / Mở rộng-không-giảm-perf" and CODE_GOVERNANCE §1+§4 prescriptions. Locked decisions in §1 reflect user's preferences captured during brainstorm.

Next step: `gsd:plan-phase` (or hand-write plan doc) to break D-1 / D0 / D1 / ... into per-task checklists, then start D-1 codegen tool development.
