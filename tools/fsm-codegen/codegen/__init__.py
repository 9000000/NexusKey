"""FSM codegen for NexusKey Sprint 3 Vietnamese typing engine.

Pipeline: parse rules (parser) → NFA (nfa) → DFA (dfa) → minimize → emit C++ tables.
Verifier exhaustively checks 20,504 valid Vietnamese syllables against generated DFA.

See ../docs/plans/2026-05-05-fsm-engine-refactor-design.md §5 for design.
"""

__version__ = "0.1.0"
