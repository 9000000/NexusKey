"""Unit tests for codegen.overlay — Action emission per DFA transition.

D-1 Task 6. Tests written test-first; pass after overlay impl.
"""
from __future__ import annotations

from pathlib import Path

import pytest

from codegen.dfa import Dfa
from codegen.nfa import Nfa, NfaState
from codegen.overlay import (
    Action,
    OverlayResult,
    apply_overlay,
    compute_action,
    render_state,
)
from codegen.parser import KeymapRule, PhonotacticsRule, RuleParser


REPO_ROOT = Path(__file__).resolve().parents[3]
RULES_DIR = REPO_ROOT / "rules"


# ─────────────────────────────────────────────────────────────────────────
# render_state — pure function, no DFA needed.
# ─────────────────────────────────────────────────────────────────────────


def test_render_start_state() -> None:
    s = NfaState(is_start=True)
    assert render_state(s) == ""


def test_render_consonant_only() -> None:
    s = NfaState(cons="t")
    assert render_state(s) == "t"


def test_render_consonant_vowel() -> None:
    s = NfaState(cons="t", vowels=("a",), mods=("none",))
    assert render_state(s) == "ta"


def test_render_vowel_with_tone_sac() -> None:
    s = NfaState(cons="t", vowels=("a",), mods=("none",), tone="sắc")
    assert render_state(s) == "tá"


def test_render_vowel_with_tone_huyen() -> None:
    s = NfaState(cons="t", vowels=("a",), mods=("none",), tone="huyền")
    assert render_state(s) == "tà"


def test_render_modifier_circumflex() -> None:
    s = NfaState(cons="t", vowels=("â",), mods=("circumflex",))
    assert render_state(s) == "tâ"


def test_render_modifier_with_tone() -> None:
    s = NfaState(cons="t", vowels=("â",), mods=("circumflex",), tone="sắc")
    assert render_state(s) == "tấ"


def test_render_horn_vowel() -> None:
    s = NfaState(cons="n", vowels=("ư",), mods=("horn",))
    assert render_state(s) == "nư"


def test_render_consonant_vowel_coda() -> None:
    s = NfaState(cons="t", vowels=("a",), mods=("none",), coda="n")
    assert render_state(s) == "tan"


def test_render_2vowel_no_coda_tone_on_first() -> None:
    """2 vowels + no coda → tone on first vowel."""
    s = NfaState(cons="t", vowels=("o", "a"), mods=("none", "none"), tone="huyền")
    assert render_state(s) == "tòa"


def test_render_2vowel_with_coda_tone_on_second() -> None:
    """2 vowels + coda → tone on second vowel.

    Anh's example: nưởc = n + ư + ở + c (tone hỏi on ơ, the second vowel,
    because syllable has coda c).
    """
    s = NfaState(
        cons="n", vowels=("ư", "ơ"), mods=("horn", "horn"),
        tone="hỏi", coda="c",
    )
    assert render_state(s) == "nưởc"


def test_render_uo_edge_huo() -> None:
    """h + u + o + horn → 'huơ' (edge case after h, only o horned)."""
    s = NfaState(cons="h", vowels=("u", "ơ"), mods=("none", "horn"))
    assert render_state(s) == "huơ"


def test_render_uo_default_uoung() -> None:
    """default uo + horn → ươ (both horned)."""
    s = NfaState(cons="t", vowels=("ư", "ơ"), mods=("horn", "horn"), coda="ng")
    assert render_state(s) == "tương"


def test_render_escape_state_literal() -> None:
    """Escape state renders as literal vowels (no tone applied)."""
    s = NfaState(cons="", vowels=("a",), mods=("none",), is_escape=True)
    # Escape from 'â' typed via 'aa' + extra 'a' → literal 'a' (just first
    # of the pair); compute_action wraps the trigger char to produce 'aa'.
    assert render_state(s) == "a"


# ─────────────────────────────────────────────────────────────────────────
# compute_action — pure text diff.
# ─────────────────────────────────────────────────────────────────────────


def test_compute_action_empty_to_char() -> None:
    """"" → "a": insert 'a', no backspaces."""
    a = compute_action("", "a")
    assert a == Action(bs_count=0, insert_text="a")


def test_compute_action_replace_last_char() -> None:
    """ "a" → "á": replace last 1 char with 'á'."""
    a = compute_action("a", "á")
    assert a == Action(bs_count=1, insert_text="á")


def test_compute_action_modifier_swap() -> None:
    """"ta" → "tâ": replace last 1 char (a → â)."""
    a = compute_action("ta", "tâ")
    assert a == Action(bs_count=1, insert_text="â")


def test_compute_action_no_change() -> None:
    """Identical src and dst → noop action."""
    a = compute_action("abc", "abc")
    assert a.is_noop()


def test_compute_action_extend_text() -> None:
    """"t" → "ta": no backspace, insert 'a'."""
    a = compute_action("t", "ta")
    assert a == Action(bs_count=0, insert_text="a")


def test_compute_action_uo_to_uong() -> None:
    """"tu" → "tương": replace last 1 char with 'ương'."""
    a = compute_action("tu", "tương")
    assert a == Action(bs_count=1, insert_text="ương")


def test_compute_action_escape_aa_from_a_circumflex() -> None:
    """ "tâ" → "taa": replace last 1 char with 'aa'."""
    a = compute_action("tâ", "taa")
    assert a == Action(bs_count=1, insert_text="aa")


def test_compute_action_escape_as_from_a_with_tone() -> None:
    """ "á" + escape → "as": replace last 1 with 'as'."""
    a = compute_action("á", "as")
    assert a == Action(bs_count=1, insert_text="as")


# ─────────────────────────────────────────────────────────────────────────
# apply_overlay — full pipeline test on minimal grammar.
# ─────────────────────────────────────────────────────────────────────────


def _minimal_phono() -> PhonotacticsRule:
    return PhonotacticsRule(
        initial_consonants_d1=[], initial_consonants_d2=[], initial_consonants_d3=[],
        special_initials={},
        vowels_single=["a"], vowels_diphthong=[], vowels_triphthong=[],
        finals_c1=[], finals_c2=[], finals_c3=[],
        n1_vowels=[], n2_vowels=[], n3_vowels=["a"],
        closed_vowels=[], suspended_vowels=[],
        tones_all=["none", "sắc", "huyền"],
        tone_hard_constraint_codas=[], tone_hard_constraint_allowed=[],
        tone_placement_2vowel_no_coda=1, tone_placement_2vowel_with_coda=2,
        tone_placement_3vowel=2, tone_placement_uye_exception=3,
        tone_placement_classic_modern_diphthongs=[], tone_placement_default="classic",
        modifiers_all=["none", "circumflex"],
        modifier_targets={"circumflex": ["a"]},
        horn_forward_propagation=[],
        uo_default_result="ươ", uo_edge_result="uơ", uo_edge_initials=[],
    )


def _minimal_keymap() -> KeymapRule:
    return KeymapRule(
        method_name="telex",
        spec_source="test fixture",
        mappings={"s": "TONE_SAC", "f": "TONE_HUYEN"},
        double_sequences={"aa": "MOD_CIRCUMFLEX"},
    )


def test_overlay_returns_overlay_result() -> None:
    nfa = Nfa.from_rules(_minimal_phono(), _minimal_keymap())
    dfa = Dfa.from_nfa(nfa)
    out = apply_overlay(dfa, nfa, _minimal_keymap())
    assert isinstance(out, OverlayResult)


def test_overlay_action_table_non_empty() -> None:
    nfa = Nfa.from_rules(_minimal_phono(), _minimal_keymap())
    dfa = Dfa.from_nfa(nfa)
    out = apply_overlay(dfa, nfa, _minimal_keymap())
    assert len(out.action_table) >= 1


def test_overlay_state_render_covers_all_states() -> None:
    nfa = Nfa.from_rules(_minimal_phono(), _minimal_keymap())
    dfa = Dfa.from_nfa(nfa)
    out = apply_overlay(dfa, nfa, _minimal_keymap())
    assert len(out.state_render) == len(dfa.states)


def test_overlay_action_for_typing_a_from_start() -> None:
    """Start + INSERT_LITERAL_a → state "a" should emit Action(0, "a")."""
    nfa = Nfa.from_rules(_minimal_phono(), _minimal_keymap())
    dfa = Dfa.from_nfa(nfa)
    out = apply_overlay(dfa, nfa, _minimal_keymap())
    start_id = dfa.start.state_id
    edges = out.transition_actions.get(start_id, {})
    aid = edges.get("INSERT_LITERAL_a")
    assert aid is not None
    assert out.action_table[aid] == Action(bs_count=0, insert_text="a")


def test_overlay_action_for_tone_application() -> None:
    """Walking 'a' then TONE_SAC: action on 2nd transition is REPLACE_LAST(1, 'á')."""
    nfa = Nfa.from_rules(_minimal_phono(), _minimal_keymap())
    dfa = Dfa.from_nfa(nfa)
    out = apply_overlay(dfa, nfa, _minimal_keymap())
    # Walk: start --INSERT_LITERAL_a--> S1 --TONE_SAC--> S2
    s1 = dfa.transitions[dfa.start.state_id]["INSERT_LITERAL_a"]
    aid = out.transition_actions[s1]["TONE_SAC"]
    assert out.action_table[aid] == Action(bs_count=1, insert_text="á")


def test_overlay_action_for_modifier_application() -> None:
    """Walking 'a' + 'a' (circumflex via aa): 2nd action is REPLACE_LAST(1, 'â')."""
    nfa = Nfa.from_rules(_minimal_phono(), _minimal_keymap())
    dfa = Dfa.from_nfa(nfa)
    out = apply_overlay(dfa, nfa, _minimal_keymap())
    s1 = dfa.transitions[dfa.start.state_id]["INSERT_LITERAL_a"]
    aid = out.transition_actions[s1]["INSERT_LITERAL_a"]
    assert out.action_table[aid] == Action(bs_count=1, insert_text="â")


def test_overlay_action_for_escape_repeated_tone() -> None:
    """ 'a' + TONE_SAC + TONE_SAC: third transition emits 'as' literal."""
    nfa = Nfa.from_rules(_minimal_phono(), _minimal_keymap())
    dfa = Dfa.from_nfa(nfa)
    out = apply_overlay(dfa, nfa, _minimal_keymap())
    s_a = dfa.transitions[dfa.start.state_id]["INSERT_LITERAL_a"]
    s_aa = dfa.transitions[s_a]["TONE_SAC"]
    aid = out.transition_actions[s_aa]["TONE_SAC"]
    assert out.action_table[aid] == Action(bs_count=1, insert_text="as")


# ─────────────────────────────────────────────────────────────────────────
# Real-rules sanity
# ─────────────────────────────────────────────────────────────────────────


@pytest.fixture(scope="module")
def real_overlay() -> tuple[Dfa, OverlayResult]:
    phono = RuleParser.load_phonotactics(RULES_DIR / "vietnamese-phonotactics.toml")
    telex = RuleParser.load_keymap(RULES_DIR / "telex-keymap.toml")
    nfa = Nfa.from_rules(phono, telex)
    dfa = Dfa.from_nfa(nfa)
    return dfa, apply_overlay(dfa, nfa, telex)


def test_overlay_real_rules_action_table_dedup(
    real_overlay: tuple[Dfa, OverlayResult],
) -> None:
    """Action table should be deduplicated — many transitions share same action."""
    _, out = real_overlay
    n_transitions = sum(len(e) for e in out.transition_actions.values())
    n_actions = len(out.action_table)
    # Heavy dedup expected: distinct actions much fewer than transitions.
    assert n_actions < n_transitions


def test_overlay_real_rules_action_count_bounded(
    real_overlay: tuple[Dfa, OverlayResult],
) -> None:
    """Action table size bounded — sanity check."""
    _, out = real_overlay
    n = len(out.action_table)
    # Loose bound — Vietnamese has ~150 vowel-tone combos; with prefixes
    # of various lengths, expect a few hundred unique actions max.
    assert 1 <= n <= 50_000


def test_overlay_real_rules_walk_basic_word(
    real_overlay: tuple[Dfa, OverlayResult],
) -> None:
    """Walk the full chain for 'tá' (t + a + TONE_SAC) — verify each
    transition has an action."""
    dfa, out = real_overlay

    cur = dfa.start.state_id
    for sym in ["INSERT_LITERAL_t", "INSERT_LITERAL_a", "TONE_SAC"]:
        edges = out.transition_actions.get(cur, {})
        assert sym in edges, f"missing action for {sym} from {cur}"
        cur = dfa.transitions[cur][sym]
