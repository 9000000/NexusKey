"""Unit tests for codegen.dfa — NFA → DFA subset construction (Task 4) +
Hopcroft minimization (Task 5).

Test-first per project_test_first memory. This file currently covers
only Task 4 subset construction; Hopcroft tests added in Task 5.

Design choices these tests lock:
  1. Dfa.from_nfa(nfa) returns a Dfa with same accept-set behavior
     (any input that NFA accepts, DFA accepts; same for rejects).
  2. DFA is deterministic — each state has exactly 0 or 1 outgoing
     transitions per input symbol (asserted by test_dfa_is_deterministic).
  3. Dfa.accepts(inputs) is the test oracle, mirroring Nfa.accepts.
"""
from __future__ import annotations

from pathlib import Path

import pytest

from codegen.dfa import Dfa
from codegen.nfa import Nfa
from codegen.parser import KeymapRule, PhonotacticsRule, RuleParser


REPO_ROOT = Path(__file__).resolve().parents[3]
RULES_DIR = REPO_ROOT / "rules"


# Reuse the minimal-grammar helpers from test_nfa.py via direct duplication.
# (Avoid cross-test imports — duplication is fine for fixtures.)


def _minimal_phonotactics() -> PhonotacticsRule:
    return PhonotacticsRule(
        initial_consonants_d1=[],
        initial_consonants_d2=[],
        initial_consonants_d3=[],
        special_initials={},
        vowels_single=["a"],
        vowels_diphthong=[],
        vowels_triphthong=[],
        finals_c1=[],
        finals_c2=[],
        finals_c3=[],
        n1_vowels=[],
        n2_vowels=[],
        n3_vowels=["a"],
        closed_vowels=[],
        suspended_vowels=[],
        tones_all=["none", "sắc", "huyền"],
        tone_hard_constraint_codas=[],
        tone_hard_constraint_allowed=[],
        tone_placement_2vowel_no_coda=1,
        tone_placement_2vowel_with_coda=2,
        tone_placement_3vowel=2,
        tone_placement_uye_exception=3,
        tone_placement_classic_modern_diphthongs=[],
        tone_placement_default="classic",
        modifiers_all=["none", "circumflex"],
        modifier_targets={"circumflex": ["a"]},
        horn_forward_propagation=[],
        uo_default_result="ươ",
        uo_edge_result="uơ",
        uo_edge_initials=[],
    )


def _minimal_telex_keymap() -> KeymapRule:
    return KeymapRule(
        method_name="telex",
        spec_source="test fixture",
        mappings={"s": "TONE_SAC", "f": "TONE_HUYEN"},
        double_sequences={"aa": "MOD_CIRCUMFLEX"},
    )


def _build_minimal_dfa() -> Dfa:
    nfa = Nfa.from_rules(_minimal_phonotactics(), _minimal_telex_keymap())
    return Dfa.from_nfa(nfa)


# Module-scoped fixture — real-rules DFA built ONCE per test session.
# Without this, every test rebuilds the (slow) real-rules NFA+DFA pair.
@pytest.fixture(scope="module")
def real_dfa() -> Dfa:
    phono = RuleParser.load_phonotactics(RULES_DIR / "vietnamese-phonotactics.toml")
    telex = RuleParser.load_keymap(RULES_DIR / "telex-keymap.toml")
    nfa = Nfa.from_rules(phono, telex)
    return Dfa.from_nfa(nfa)


# ─────────────────────────────────────────────────────────────────────────
# Build sanity
# ─────────────────────────────────────────────────────────────────────────


def test_dfa_builds_from_minimal_nfa() -> None:
    dfa = _build_minimal_dfa()
    assert dfa is not None
    assert dfa.start is not None


def test_dfa_state_count_finite() -> None:
    dfa = _build_minimal_dfa()
    assert 1 <= len(dfa.states) <= 10_000


def test_dfa_state_ids_unique() -> None:
    dfa = _build_minimal_dfa()
    ids = [s.state_id for s in dfa.states]
    assert len(ids) == len(set(ids)), "duplicate state_id in DFA"


def test_dfa_transitions_reference_valid_states() -> None:
    dfa = _build_minimal_dfa()
    valid_ids = {s.state_id for s in dfa.states}
    for src_id, edges in dfa.transitions.items():
        assert src_id in valid_ids
        for dst_id in edges.values():
            assert dst_id in valid_ids


# ─────────────────────────────────────────────────────────────────────────
# Determinism — each (state, input) has at most 1 destination.
# ─────────────────────────────────────────────────────────────────────────


def test_dfa_is_deterministic() -> None:
    """For every state + input symbol, dfa.transitions yields a single
    destination — not a set, not multiple."""
    dfa = _build_minimal_dfa()
    for src_id, edges in dfa.transitions.items():
        for sym, dst in edges.items():
            assert isinstance(dst, int), (
                f"DFA transition ({src_id}, {sym!r}) → {dst!r} is not a "
                f"single int dst (DFA must be deterministic)"
            )


# ─────────────────────────────────────────────────────────────────────────
# Acceptance — DFA accepts iff NFA accepts (subset construction is
# language-preserving).
# ─────────────────────────────────────────────────────────────────────────


@pytest.mark.parametrize("inputs", [
    ["INSERT_LITERAL_a"],                                       # a
    ["INSERT_LITERAL_a", "TONE_SAC"],                           # á
    ["INSERT_LITERAL_a", "TONE_HUYEN"],                         # à
    ["INSERT_LITERAL_a", "INSERT_LITERAL_a"],                   # â
    ["INSERT_LITERAL_a", "INSERT_LITERAL_a", "TONE_SAC"],       # ấ
    ["INSERT_LITERAL_a", "TONE_SAC", "TONE_HUYEN"],             # à (replace)
    ["INSERT_LITERAL_a", "TONE_SAC", "TONE_SAC"],               # escape "as"
    ["INSERT_LITERAL_a", "INSERT_LITERAL_a", "INSERT_LITERAL_a"],  # escape "aa"
])
def test_dfa_accepts_what_nfa_accepts(inputs: list[str]) -> None:
    nfa = Nfa.from_rules(_minimal_phonotactics(), _minimal_telex_keymap())
    dfa = Dfa.from_nfa(nfa)
    assert nfa.accepts(inputs), f"NFA precondition: {inputs}"
    assert dfa.accepts(inputs), f"DFA must accept what NFA accepts: {inputs}"


@pytest.mark.parametrize("inputs", [
    ["TONE_SAC"],                          # tone alone
    ["NOT_A_REAL_INPUT_SYMBOL"],           # unknown
])
def test_dfa_rejects_what_nfa_rejects(inputs: list[str]) -> None:
    nfa = Nfa.from_rules(_minimal_phonotactics(), _minimal_telex_keymap())
    dfa = Dfa.from_nfa(nfa)
    assert not nfa.accepts(inputs), f"NFA precondition: {inputs}"
    assert not dfa.accepts(inputs), f"DFA must reject what NFA rejects: {inputs}"


# ─────────────────────────────────────────────────────────────────────────
# Real-rules sanity
# ─────────────────────────────────────────────────────────────────────────


def test_dfa_builds_from_real_rules(real_dfa: Dfa) -> None:
    assert real_dfa is not None
    n = len(real_dfa.states)
    # DFA is generally LARGER than NFA before minimization (subset
    # construction can blow up), but bounded.
    assert 100 <= n <= 5_000_000, f"unreasonable DFA size: {n}"


def test_dfa_real_rules_accepts_basic_cv(real_dfa: Dfa) -> None:
    assert real_dfa.accepts(["INSERT_LITERAL_t", "INSERT_LITERAL_a"])
    assert real_dfa.accepts(["INSERT_LITERAL_t", "INSERT_LITERAL_a", "TONE_SAC"])
    assert real_dfa.accepts(["INSERT_LITERAL_t", "INSERT_LITERAL_a", "INSERT_LITERAL_n"])


def test_dfa_real_rules_uo_horn(real_dfa: Dfa) -> None:
    assert real_dfa.accepts(
        ["INSERT_LITERAL_t", "INSERT_LITERAL_u",
         "INSERT_LITERAL_o", "MOD_HORN"]
    )


def test_dfa_real_rules_uo_edge_after_h(real_dfa: Dfa) -> None:
    assert real_dfa.accepts(
        ["INSERT_LITERAL_h", "INSERT_LITERAL_u",
         "INSERT_LITERAL_o", "MOD_HORN"]
    )


# ─────────────────────────────────────────────────────────────────────────
# Performance bound
# ─────────────────────────────────────────────────────────────────────────


def test_dfa_build_time_under_15_seconds() -> None:
    """Build budget: NFA + DFA subset construction ≤ 15s (verifier full
    pipeline budget = 30s; leaves room for minimize + emit)."""
    import time

    phono = RuleParser.load_phonotactics(RULES_DIR / "vietnamese-phonotactics.toml")
    telex = RuleParser.load_keymap(RULES_DIR / "telex-keymap.toml")
    t0 = time.perf_counter()
    nfa = Nfa.from_rules(phono, telex)
    Dfa.from_nfa(nfa)
    elapsed = time.perf_counter() - t0
    assert elapsed < 15.0, f"NFA+DFA build took {elapsed:.2f}s, budget 15.0s"


# ─────────────────────────────────────────────────────────────────────────
# API surface
# ─────────────────────────────────────────────────────────────────────────


def test_dfa_has_accepts_method() -> None:
    dfa = _build_minimal_dfa()
    assert hasattr(dfa, "accepts")
    assert callable(dfa.accepts)


def test_dfa_start_in_states() -> None:
    dfa = _build_minimal_dfa()
    assert dfa.start in dfa.states


# ═════════════════════════════════════════════════════════════════════════
# Task 5 — Hopcroft minimization tests
# ═════════════════════════════════════════════════════════════════════════


def test_minimize_returns_dfa() -> None:
    dfa = _build_minimal_dfa()
    minimized = dfa.minimize()
    assert isinstance(minimized, Dfa)
    assert minimized.start is not None


def test_minimize_does_not_grow_state_count() -> None:
    """Minimization can only reduce or preserve state count, never grow."""
    dfa = _build_minimal_dfa()
    minimized = dfa.minimize()
    assert len(minimized.states) <= len(dfa.states)


def test_minimize_state_count_at_least_one() -> None:
    dfa = _build_minimal_dfa()
    minimized = dfa.minimize()
    assert len(minimized.states) >= 1


@pytest.mark.parametrize("inputs", [
    ["INSERT_LITERAL_a"],
    ["INSERT_LITERAL_a", "TONE_SAC"],
    ["INSERT_LITERAL_a", "TONE_HUYEN"],
    ["INSERT_LITERAL_a", "INSERT_LITERAL_a"],
    ["INSERT_LITERAL_a", "INSERT_LITERAL_a", "TONE_SAC"],
    ["INSERT_LITERAL_a", "TONE_SAC", "TONE_HUYEN"],
    ["INSERT_LITERAL_a", "TONE_SAC", "TONE_SAC"],
    ["INSERT_LITERAL_a", "INSERT_LITERAL_a", "INSERT_LITERAL_a"],
])
def test_minimize_preserves_acceptance(inputs: list[str]) -> None:
    """Language preservation: minimized DFA accepts ⇔ original accepts."""
    dfa = _build_minimal_dfa()
    minimized = dfa.minimize()
    assert dfa.accepts(inputs) == minimized.accepts(inputs), (
        f"language mismatch on {inputs}: "
        f"orig={dfa.accepts(inputs)}, min={minimized.accepts(inputs)}"
    )


@pytest.mark.parametrize("inputs", [
    ["TONE_SAC"],
    ["NOT_A_REAL_INPUT_SYMBOL"],
])
def test_minimize_preserves_rejection(inputs: list[str]) -> None:
    dfa = _build_minimal_dfa()
    minimized = dfa.minimize()
    assert not dfa.accepts(inputs)
    assert not minimized.accepts(inputs)


def test_minimize_idempotent() -> None:
    """minimize().minimize() should have same state count as minimize()."""
    dfa = _build_minimal_dfa()
    once = dfa.minimize()
    twice = once.minimize()
    assert len(once.states) == len(twice.states)


def test_minimize_real_rules_reduces_state_count(real_dfa: Dfa) -> None:
    """Hopcroft on real Vietnamese DFA must significantly reduce states.

    Pre-min: ~19,549 states (em saw). Target ≤ 5,000 (≥ 4× reduction).
    Tighter target post-Hopcroft is 500-2000 per design doc; allow slack.
    """
    minimized = real_dfa.minimize()
    assert len(minimized.states) <= len(real_dfa.states)
    assert len(minimized.states) <= 5000, (
        f"minimize did not compress enough: "
        f"{len(real_dfa.states)} → {len(minimized.states)}"
    )


def test_minimize_real_rules_preserves_acceptance(real_dfa: Dfa) -> None:
    minimized = real_dfa.minimize()
    cases = [
        ["INSERT_LITERAL_t", "INSERT_LITERAL_a"],
        ["INSERT_LITERAL_t", "INSERT_LITERAL_a", "TONE_SAC"],
        ["INSERT_LITERAL_t", "INSERT_LITERAL_a", "INSERT_LITERAL_n"],
        ["INSERT_LITERAL_t", "INSERT_LITERAL_u", "INSERT_LITERAL_o", "MOD_HORN"],
        ["INSERT_LITERAL_h", "INSERT_LITERAL_u", "INSERT_LITERAL_o", "MOD_HORN"],
    ]
    for inputs in cases:
        assert real_dfa.accepts(inputs)
        assert minimized.accepts(inputs), f"minimize regressed accept: {inputs}"

    rejects = [["TONE_SAC"], ["NOT_REAL"], ["INSERT_LITERAL_t"]]
    for inputs in rejects:
        assert not real_dfa.accepts(inputs)
        assert not minimized.accepts(inputs), f"minimize regressed reject: {inputs}"


def test_minimize_real_rules_under_30s(real_dfa: Dfa) -> None:
    """Hopcroft minimization ≤ 30s budget (verifier total budget)."""
    import time

    t0 = time.perf_counter()
    real_dfa.minimize()
    elapsed = time.perf_counter() - t0
    assert elapsed < 30.0, f"minimize took {elapsed:.2f}s, budget 30.0s"
