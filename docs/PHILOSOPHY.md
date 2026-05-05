# NexusKey — Core Development Philosophy

> Stated by the project owner 2026-05-04.
> This document supersedes individual sprint goals. Every architectural decision, every PR, every commit must satisfy what is written here. If a tactical decision conflicts with this document, the document wins.

This file is the highest-level filter for all design and implementation work in NexusKey. It has three layers:

1. **What we build** — the four product pillars
2. **How we develop** — test-first
3. **What we ask before each piece** — the three pre-code questions

---

## 1. The Four Pillars (Product DNA)

NexusKey core is designed for: **Nhanh — Nhẹ — Mượt — Mở rộng cao mà không ảnh hưởng hiệu suất.**

(Fast — Light — Smooth — Highly extensible without runtime cost.)

| Pillar | Concrete meaning | Anti-pattern that violates it |
|---|---|---|
| **Nhanh (Fast)** | Hot path completes in < 1 ms (Rule #11.1). Hook callback uses atomic + RCU only — never mutex, never I/O, never allocation. | `std::lock_guard` on hook hot path. Any syscall in hook callback (`CreateFile`, `CreateToolhelp32Snapshot`, `Sleep`). Heap allocation per keystroke. |
| **Nhẹ (Light)** | Memory + binary footprint stays small. SharedState struct is compact (cross-process, mapped into every TSF host). DLL stays small (loaded into every app). One worker thread covers many responsibilities; one thread per responsibility is rejected. | Adding a heavy library when an existing utility (toml++, EnglishProtection.h, VietnameseTables.h) suffices. Three threads for three jobs when one thread can serialize them. |
| **Mượt (Smooth)** | Worst case (p99) matters more than average. No "sometimes slow" moments. Config reload, focus change, tray click do not stutter. Every slow operation moves OFF the hook thread. | `ReloadFromToml()` triggered on hook thread. Long focus-classification path holding a lock readable by the hook. Any user-visible operation whose latency is unpredictable. |
| **Mở rộng cao, không ảnh hưởng hiệu suất** (Extensible without runtime cost) | New features (injectors, encodings, profiles, macros) plug in without changing hot-path code. Extensibility cost paid at compile time (templates, static dispatch) or load time (factory at startup), never at runtime per keystroke. | Virtual call on hook hot path. `std::function` per keystroke. Plugin loaded at runtime that the hook must consult. "Generic" abstraction with no concrete second use case. |

### The Layering Rule (key tension resolved)

The fourth pillar's tension — extensibility usually costs runtime — is resolved by **strict hot/cold layering**:

```
HOT PATH (hook callback, every keystroke):
  ↓ atomic loads + RCU read + plain function calls only
  ↓ NO virtual dispatch, NO factory lookup, NO indirection
  ↓ extensibility = compile-time (templates) or data-driven (lookup table)

COLD PATH (main thread, MainThreadWorker, settings, init):
  ↓ classes, virtual, factories, allocations all OK
  ↓ extensibility = runtime registration / configuration
  ↓ 1 ms cost here is invisible to the user
```

**Example — IOutputInjector (Sprint 2):**
- Selection logic ("which injector for this app?") = **cold path**, runs once on focus change. Virtual / factory / allocation OK.
- Selected injector is published to an atomic pointer.
- Hot path reads atomic pointer → 1 virtual call → ~5 ns. Pillar 1 still holds.
- Adding a new injector = new class + register in factory. Zero hot-path code changes.

---

## 2. Test-First (How We Develop)

> "Test trước — test ra được thì mới implement."

| Code change type | Test-first concretely means |
|---|---|
| **New feature** | Write a NextKeyTestRunner corpus case (or unit test) capturing expected output BEFORE the feature exists. Run → fails. Implement → passes. |
| **Bug fix** | Add a corpus case (or unit test) reproducing the bug FIRST. Run → fails (reproduces). Fix → passes. The case stays as regression guard. |
| **Refactor** | Existing tests are the contract. Verify they pass before refactor (lock baseline). Refactor → tests still pass. If a refactor needs to change tests, that is a behavior change, not refactor. |
| **New infrastructure** (new class, thread, IPC) | Test file (`FooTests.cpp` with GTest) asserting lifecycle, behavior, edge cases is committed alongside or before the implementation. |
| **Architecture change** | Snapshot baseline (chaos corpus + perf-baseline-`<sha>`). Define new contract via failing test. Make it pass. Compare new baseline against locked old. |

**Why:** Concurrency bugs (race, ordering, lifetime) are silent. Code can "look correct," pass review, and ship broken. Only a test that asserts behavior catches them. Phase 0a built `tools/NextKeyTestRunner` precisely so this discipline is feasible end-to-end. Skipping it wastes the infrastructure.

**Anti-patterns refused on sight:**

- "I'll add tests later." It never happens, and concurrency bugs slip through.
- "This change is too small for a test." Small changes are exactly where regressions hide.
- "The existing tests cover this" without re-running tests — verify, don't claim.
- Adding a feature flag because tests fail. Fix the implementation; do not gate around the test.

**PR gate:** Every PR must include test diffs for new infrastructure. A PR with implementation but no test is rejected at review.

---

## 3. The Three Pre-Code Questions (How We Decide Each Piece)

Before any code is written — **before even the test** — the implementer must answer three questions in writing (in the design note, plan, or PR draft):

### Q1 — Is this in the right place? (right module / layer / file)

- Hot path vs cold path? If on hook hot path, does it satisfy Rule #11 (no mutex, no I/O, no allocation)? If borderline, route through MainThreadWorker.
- Module boundary: `src/core/` (cross-cutting engine + IPC), `src/app/` (process-local services), `src/tsf/` (DLL)?
- File: existing file or new file? Existing namespace or new?
- Layer: hot/cold, presentation/logic, IPC/local — pick the right layer.

### Q2 — What's the impact? (side effects / blast radius)

- Cross-process: change `SharedState`? → Rule #5 (struct versioning) bump required.
- Cross-thread: new shared variable? → atomic + memory ordering specified, OR documented owner thread.
- Performance: syscall, allocation, virtual call introduced on hot path? Measure or refuse.
- API surface: signature change? Who else calls? Update all callers in the same PR.
- Build: new dependency, CMake change, new package? Document why in commit body.

### Q3 — Is there a better way? (alternative considered?)

- Algorithmic: O(n) when O(1) lookup is possible (hash table, indexed array)?
- Concurrency: mutex when atomic suffices? Atomic when relaxed-ordering suffices? (Don't over-synchronize either.)
- Memory: heap when stack/static fits? `std::string` when `wchar_t[N]` works?
- Compiler: virtual when template instantiation does the job at compile time? `std::function` when a plain function pointer works?
- Reuse: reinventing something already in `EnglishProtection.h`, `VietnameseTables.h`, or another existing utility?

**Why:** Wrong-place code passes its tests but harms the project. Side effects compound silently — a new SharedState field that doesn't bump version "works" today, breaks the next user's build. "Better way" exists at design time but evaporates once code is written (sunk cost).

### Sequence (mandatory order)

```
1. DESIGN moment        ← answer Q1/Q2/Q3 in writing (in plan / PR draft / design note)
   ↓
2. WRITE THE TEST       ← test-first: write the test against the chosen design
   ↓
3. IMPLEMENT            ← make the test pass
   ↓
4. COMMIT               ← commit body references the Q1/Q2/Q3 answers + test diff
```

**Anti-patterns:**

- Answering Q1/Q2/Q3 after writing code — that is a review, not a design check. The whole point is to ask BEFORE.
- "Skipping Q3 because the answer is obvious" — write the alternative anyway. The act of writing forces clarity.
- Q3 answered with "this is fine" — Q3 demands a *named alternative considered and rejected*, not a value judgment.

---

## How These Layers Stack

```
        ┌─────────────────────────────────────────────────┐
        │   FOUR PILLARS (what the code must achieve)     │
        │     Nhanh • Nhẹ • Mượt • Mở rộng-no-cost        │
        └─────────────────────────────────────────────────┘
                              ▲
                              │ guides
        ┌─────────────────────────────────────────────────┐
        │   THREE PRE-CODE QUESTIONS (per piece of work)  │
        │     Q1 right place? Q2 impact? Q3 better way?    │
        └─────────────────────────────────────────────────┘
                              ▲
                              │ then
        ┌─────────────────────────────────────────────────┐
        │   TEST-FIRST (the artifact-creation order)       │
        │     test → fail → implement → pass               │
        └─────────────────────────────────────────────────┘
```

A change that satisfies all three layers is ready to merge. A change that fails any layer is sent back to the design moment.

---

## Companion Documents

- **`docs/CODE_GOVERNANCE.md`** — the 5-question pre-code gate (operational form of §3) and the architectural prescriptions (FSM core, IOutputInjector, SPSC, plugin layer) that 3 collaborators align on.
- **`docs/CODING_RULES/index.md`** — concrete rules (naming, memory, error handling, hook system, etc.) that operationalize this philosophy.
- **`docs/CODING_RULES/11-hook-system-rules.md`** — the hard rules that derive from Pillar #1 (Nhanh).
- **`docs/CODING_RULES/5-struct-versioning.md`** — the rule that makes Pillar #4 (extensibility without breaking) actually work cross-process.
- **`PROJECT_MAP.md`** — directory tree and module boundaries that Q1 (right place) refers to.
- **`HANDOFF.md`** — current sprint state and gates.
