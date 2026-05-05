"""Unit tests for codegen.nfa — NFA construction from phonotactic rules.

Test-first per project_test_first memory. Tests written BEFORE
implementation lands in codegen/nfa.py (D-1 Task 3).

Design choices these tests lock in:
  1. `Nfa.from_rules(phono, keymap)` is the public builder.
  2. `nfa.accepts(abstract_inputs: list[str]) -> bool` — primary oracle
     for testing transition correctness. Avoids leaking internal state IDs.
  3. `nfa.states()` / `nfa.accept_states()` — state-set introspection
     for size/sanity bounds.
  4. NFA operates on AbstractInput names (strings), NOT raw keys.
     Keymap layer translates raw → abstract; NFA does not see 's', '1',
     etc. — only "TONE_SAC", "INSERT_LITERAL_a", etc.
  5. Action emission is OUT OF SCOPE for NFA — Task 6 overlay layer
     handles tone/modifier text actions. NFA only does state transitions.
  6. Escape state semantics: typing same-tone twice (`a` + TONE_SAC +
     TONE_SAC) reaches an "escape" accept state. Tests verify reachability
     but not action text.
"""
from __future__ import annotations

from pathlib import Path

import pytest

from codegen.nfa import Nfa
from codegen.parser import KeymapRule, PhonotacticsRule, RuleParser


REPO_ROOT = Path(__file__).resolve().parents[3]
RULES_DIR = REPO_ROOT / "rules"


# ─────────────────────────────────────────────────────────────────────────
# Minimal test rule helpers — keep state count small, isolate behavior.
# ─────────────────────────────────────────────────────────────────────────


def _minimal_phonotactics() -> PhonotacticsRule:
    """Tiny phonotactic rule: just vowel 'a', tones, circumflex modifier.

    Covers enough surface to test:
      - 1-vowel acceptance
      - tone application
      - modifier (circumflex via aa)
      - escape (aaa → aa, ass → as)
    """
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
        n3_vowels=["a"],   # 'a' takes any coda — but no codas in this rule
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
    """Just s/f tones + aa circumflex sequence."""
    return KeymapRule(
        method_name="telex",
        spec_source="test fixture",
        mappings={
            "s": "TONE_SAC",
            "f": "TONE_HUYEN",
        },
        double_sequences={
            "aa": "MOD_CIRCUMFLEX",
        },
    )


# ─────────────────────────────────────────────────────────────────────────
# Build sanity — NFA constructible, has start + at least 1 accept state.
# ─────────────────────────────────────────────────────────────────────────


def test_nfa_builds_from_minimal_rules() -> None:
    nfa = Nfa.from_rules(_minimal_phonotactics(), _minimal_telex_keymap())
    assert nfa is not None
    assert nfa.start_state() is not None


def test_nfa_has_accept_states() -> None:
    nfa = Nfa.from_rules(_minimal_phonotactics(), _minimal_telex_keymap())
    assert len(nfa.accept_states()) >= 1


def test_nfa_state_count_finite_and_bounded() -> None:
    """Sanity bound — minimal grammar should be small."""
    nfa = Nfa.from_rules(_minimal_phonotactics(), _minimal_telex_keymap())
    n = len(nfa.states())
    assert 1 <= n <= 200, f"unexpected NFA size for minimal grammar: {n}"


# ─────────────────────────────────────────────────────────────────────────
# Acceptance — minimal grammar (state-shape transparent).
#
# All inputs are AbstractInput name strings. Keymap-level raw→abstract
# translation happens BEFORE NFA in the real pipeline.
# ─────────────────────────────────────────────────────────────────────────


def test_nfa_accepts_single_vowel() -> None:
    """Typing just 'a' produces a valid syllable."""
    nfa = Nfa.from_rules(_minimal_phonotactics(), _minimal_telex_keymap())
    assert nfa.accepts(["INSERT_LITERAL_a"])


def test_nfa_accepts_vowel_with_tone() -> None:
    """'a' + TONE_SAC = 'á' (valid)."""
    nfa = Nfa.from_rules(_minimal_phonotactics(), _minimal_telex_keymap())
    assert nfa.accepts(["INSERT_LITERAL_a", "TONE_SAC"])


def test_nfa_accepts_modified_vowel() -> None:
    """'a' + 'a' (modifier circumflex) = 'â' (valid)."""
    nfa = Nfa.from_rules(_minimal_phonotactics(), _minimal_telex_keymap())
    assert nfa.accepts(["INSERT_LITERAL_a", "INSERT_LITERAL_a"])


def test_nfa_accepts_modified_vowel_with_tone() -> None:
    """'a' + 'a' + TONE_SAC = 'ấ' (valid)."""
    nfa = Nfa.from_rules(_minimal_phonotactics(), _minimal_telex_keymap())
    assert nfa.accepts(
        ["INSERT_LITERAL_a", "INSERT_LITERAL_a", "TONE_SAC"]
    )


def test_nfa_accepts_tone_replacement() -> None:
    """'a' + TONE_SAC + TONE_HUYEN = 'à' — different tones replace, not escape."""
    nfa = Nfa.from_rules(_minimal_phonotactics(), _minimal_telex_keymap())
    assert nfa.accepts(
        ["INSERT_LITERAL_a", "TONE_SAC", "TONE_HUYEN"]
    )


def test_nfa_escape_repeated_tone() -> None:
    """'a' + TONE_SAC + TONE_SAC reaches an accept state (escape literal 'as').

    Same-tone-twice is an escape — output should be the literal pair, not
    a re-toned vowel. NFA must reach an accept state (not dead state).
    """
    nfa = Nfa.from_rules(_minimal_phonotactics(), _minimal_telex_keymap())
    assert nfa.accepts(
        ["INSERT_LITERAL_a", "TONE_SAC", "TONE_SAC"]
    )


def test_nfa_escape_repeated_modifier() -> None:
    """'a' + 'a' + 'a' = 'aaa' → escape circumflex (literal 'aa')."""
    nfa = Nfa.from_rules(_minimal_phonotactics(), _minimal_telex_keymap())
    assert nfa.accepts(
        ["INSERT_LITERAL_a", "INSERT_LITERAL_a", "INSERT_LITERAL_a"]
    )


# ─────────────────────────────────────────────────────────────────────────
# Real-rules sanity — full Vietnamese phonotactics + Telex keymap.
# ─────────────────────────────────────────────────────────────────────────


def test_nfa_builds_from_real_rules() -> None:
    """Full Vietnamese phonotactics + Telex full keymap: just builds OK."""
    phono = RuleParser.load_phonotactics(RULES_DIR / "vietnamese-phonotactics.toml")
    telex = RuleParser.load_keymap(RULES_DIR / "telex-keymap.toml")
    nfa = Nfa.from_rules(phono, telex)
    assert nfa is not None
    n = len(nfa.states())
    # Loose sanity bound — full grammar much bigger than minimal.
    # Codegen target is 500-2000 minimized DFA states; NFA pre-minimization
    # may be ~10-100× larger.
    assert 100 <= n <= 1_000_000, f"unreasonable NFA size from real rules: {n}"


def test_nfa_real_rules_accepts_basic_consonant_vowel() -> None:
    """'t' + 'a' = 'ta' valid."""
    phono = RuleParser.load_phonotactics(RULES_DIR / "vietnamese-phonotactics.toml")
    telex = RuleParser.load_keymap(RULES_DIR / "telex-keymap.toml")
    nfa = Nfa.from_rules(phono, telex)
    assert nfa.accepts(["INSERT_LITERAL_t", "INSERT_LITERAL_a"])


def test_nfa_real_rules_accepts_full_syllable_with_tone() -> None:
    """'t' + 'a' + TONE_SAC = 'tá'."""
    phono = RuleParser.load_phonotactics(RULES_DIR / "vietnamese-phonotactics.toml")
    telex = RuleParser.load_keymap(RULES_DIR / "telex-keymap.toml")
    nfa = Nfa.from_rules(phono, telex)
    assert nfa.accepts(
        ["INSERT_LITERAL_t", "INSERT_LITERAL_a", "TONE_SAC"]
    )


def test_nfa_real_rules_accepts_consonant_vowel_coda() -> None:
    """'t' + 'a' + 'n' = 'tan' (closed syllable with C3 coda)."""
    phono = RuleParser.load_phonotactics(RULES_DIR / "vietnamese-phonotactics.toml")
    telex = RuleParser.load_keymap(RULES_DIR / "telex-keymap.toml")
    nfa = Nfa.from_rules(phono, telex)
    assert nfa.accepts(
        ["INSERT_LITERAL_t", "INSERT_LITERAL_a", "INSERT_LITERAL_n"]
    )


def test_nfa_real_rules_accepts_diphthong_with_horn() -> None:
    """'t' + 'u' + 'o' + MOD_HORN = 'tươ' (default uo+horn → ươ, not edge)."""
    phono = RuleParser.load_phonotactics(RULES_DIR / "vietnamese-phonotactics.toml")
    telex = RuleParser.load_keymap(RULES_DIR / "telex-keymap.toml")
    nfa = Nfa.from_rules(phono, telex)
    assert nfa.accepts(
        ["INSERT_LITERAL_t", "INSERT_LITERAL_u",
         "INSERT_LITERAL_o", "MOD_HORN"]
    )


def test_nfa_real_rules_accepts_uo_edge_after_h() -> None:
    """'h' + 'u' + 'o' + MOD_HORN = 'huơ' (edge case after h, NOT 'hươ')."""
    phono = RuleParser.load_phonotactics(RULES_DIR / "vietnamese-phonotactics.toml")
    telex = RuleParser.load_keymap(RULES_DIR / "telex-keymap.toml")
    nfa = Nfa.from_rules(phono, telex)
    # NFA reachability — accept any path. Distinction between 'hươ' and 'huơ'
    # output is a Task 6 (overlay action) concern, not NFA structure.
    assert nfa.accepts(
        ["INSERT_LITERAL_h", "INSERT_LITERAL_u",
         "INSERT_LITERAL_o", "MOD_HORN"]
    )


# ─────────────────────────────────────────────────────────────────────────
# Rejection — invalid input sequences should NOT reach accept.
# ─────────────────────────────────────────────────────────────────────────


def test_nfa_rejects_tone_alone() -> None:
    """TONE_SAC at start (no vowel) — should not reach accept state."""
    nfa = Nfa.from_rules(_minimal_phonotactics(), _minimal_telex_keymap())
    assert not nfa.accepts(["TONE_SAC"])


def test_nfa_rejects_unknown_input() -> None:
    """Unknown abstract input symbol — must not reach accept (and not crash)."""
    nfa = Nfa.from_rules(_minimal_phonotactics(), _minimal_telex_keymap())
    assert not nfa.accepts(["NOT_A_REAL_INPUT_SYMBOL"])


def test_nfa_rejects_consonant_only() -> None:
    """'t' alone (no vowel) — invalid Vietnamese syllable."""
    phono = RuleParser.load_phonotactics(RULES_DIR / "vietnamese-phonotactics.toml")
    telex = RuleParser.load_keymap(RULES_DIR / "telex-keymap.toml")
    nfa = Nfa.from_rules(phono, telex)
    assert not nfa.accepts(["INSERT_LITERAL_t"])


# ─────────────────────────────────────────────────────────────────────────
# Performance bound — NFA build time budget (sanity check).
# ─────────────────────────────────────────────────────────────────────────


def test_nfa_build_time_under_5_seconds() -> None:
    """Build full Vietnamese NFA in < 5 seconds (Task 7 verifier budget = 30s
    total; NFA build is one phase).
    """
    import time

    phono = RuleParser.load_phonotactics(RULES_DIR / "vietnamese-phonotactics.toml")
    telex = RuleParser.load_keymap(RULES_DIR / "telex-keymap.toml")
    t0 = time.perf_counter()
    Nfa.from_rules(phono, telex)
    elapsed = time.perf_counter() - t0
    assert elapsed < 5.0, f"NFA build took {elapsed:.2f}s, budget 5.0s"


# ─────────────────────────────────────────────────────────────────────────
# API surface check — declares contract subsequent Tasks (4: DFA) consume.
# ─────────────────────────────────────────────────────────────────────────


def test_nfa_states_returns_set() -> None:
    nfa = Nfa.from_rules(_minimal_phonotactics(), _minimal_telex_keymap())
    states = nfa.states()
    assert isinstance(states, (set, frozenset))


def test_nfa_accept_states_subset_of_states() -> None:
    nfa = Nfa.from_rules(_minimal_phonotactics(), _minimal_telex_keymap())
    assert nfa.accept_states().issubset(nfa.states())


def test_nfa_start_state_in_states() -> None:
    nfa = Nfa.from_rules(_minimal_phonotactics(), _minimal_telex_keymap())
    assert nfa.start_state() in nfa.states()
