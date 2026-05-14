# fsm-codegen — VKey Sprint 3 FSM Table Generator

Build-time Python tool that consumes Vietnamese phonotactic rules + Telex/VNI keymaps and emits C++ `constexpr` FSM tables for the new `FsmDispatcher` engine.

**Status**: D-1 Task 1 skeleton (2026-05-05). Pipeline impl Tasks 2-8.

**Foundation docs**:
- [`../../docs/plans/2026-05-05-fsm-engine-refactor-design.md`](../../docs/plans/2026-05-05-fsm-engine-refactor-design.md) — design
- [`../../docs/plans/sprint-3-fsm-engine-plan.md`](../../docs/plans/sprint-3-fsm-engine-plan.md) — task breakdown
- [`../../docs/RuleTiengViet_Summary.md`](../../docs/RuleTiengViet_Summary.md) — Vietnamese phonotactics

## Pipeline

```
rules/*.toml
   ↓ parser
PhonotacticsRule + KeymapRule
   ↓ nfa.Nfa.from_rules()
NFA (states = partial-syllable patterns)
   ↓ dfa.Dfa.from_nfa()           [subset construction]
DFA (deterministic)
   ↓ dfa.Dfa.minimize()            [Hopcroft]
DFA (minimized)
   ↓ overlay (tone/modifier/escape)
Final DFA + actions
   ↓ verifier.verify_exhaustive()  [20,504 syllables]
✅ all reach accept states
   ↓ emitter
src/core/engine/generated/*.h    [C++ constexpr tables]
```

## Usage

```bash
# Install dev dependencies
cd tools/fsm-codegen
pip install -e '.[dev]'

# Run codegen end-to-end
python -m codegen.cli --rules ../../rules/ --output ../../src/core/engine/generated/

# Or via setuptools entry point (after `pip install`)
fsm-codegen --rules ../../rules/ --output ../../src/core/engine/generated/

# Run tests
pytest
```

## Module map

| Module | Responsibility | Implementation task |
|---|---|---|
| `codegen.cli` | CLI entry point + arg parsing | Task 1 (this commit) |
| `codegen.parser` | TOML → `PhonotacticsRule` / `KeymapRule` | Task 2 |
| `codegen.nfa` | Phonotactic rules + keymap → NFA | Task 3 |
| `codegen.dfa` | NFA → DFA (subset) + Hopcroft minimize | Task 4 + 5 |
| `codegen.overlay` | Tone/modifier/escape transitions | Task 6 |
| `codegen.verifier` | Exhaustive 20,504-syllable check | Task 7 |
| `codegen.emitter` | DFA → C++ `constexpr` headers | Task 8 |

## Output files (after Tasks 2-8)

```
src/core/engine/generated/
├── abstract_input.h         # enum AbstractInput { TONE_SAC, MOD_HORN, ... }
├── fsm_table_telex.h        # constexpr FsmCell[state][input] for Telex
├── fsm_table_telex_simple.h # subset (no shorthand)
├── fsm_table_vni.h
├── fsm_table_combined.h     # Telex ∪ VNI (disjoint key sets)
├── keymap_telex.h           # constexpr AbstractInput[256] raw → abstract
├── keymap_telex_simple.h
├── keymap_vni.h
└── keymap_combined.h
```

## License

Tool itself: GPL-3.0-or-later (matches VKey).

Vendored test/dictionary data is BSD-3-Clause (gonhanh.org). See [`../../LICENSE-3RD-PARTY.md`](../../LICENSE-3RD-PARTY.md).

## Why a build-time codegen and not a runtime engine?

- **Verification**: 20,504 syllables exhaustively checked at codegen time, not runtime. Coverage is a build artifact.
- **Memory**: tables are `constexpr` constants in `.rodata`, no heap, ≤ 100 KB total.
- **Hot-path performance**: 1 atomic_load + 1 array index = ~15 ns. No interpretation overhead.
- **Single source of truth**: rules live in `.toml`; tool re-derives all 4 method tables from same source. Edit a rule → re-codegen → all 4 tables updated consistently.
