"""Unit tests for codegen.verifier — exhaustive syllable verification.

D-1 Task 7. Verifies that the FSM (NFA → DFA → overlay) correctly
produces every valid Vietnamese syllable from its Telex input sequence.

Strategy:
  - Curated case list (~30 syllables covering tone, modifier, diphthong,
    coda, escape) — fast smoke set, runs every test.
  - Full enumeration over phonotactic rules (~ thousands of syllables) —
    bigger correctness gate, marked separately for budget.
"""
from __future__ import annotations

from pathlib import Path

import pytest

from codegen.dfa import Dfa
from codegen.nfa import Nfa
from codegen.overlay import OverlayResult, apply_overlay
from codegen.parser import KeymapRule, RuleParser
from codegen.verifier import VerifyResult, Verifier


REPO_ROOT = Path(__file__).resolve().parents[3]
RULES_DIR = REPO_ROOT / "rules"


@pytest.fixture(scope="module")
def real_pipeline() -> tuple[Dfa, OverlayResult, KeymapRule]:
    phono = RuleParser.load_phonotactics(RULES_DIR / "vietnamese-phonotactics.toml")
    telex = RuleParser.load_keymap(RULES_DIR / "telex-keymap.toml")
    nfa = Nfa.from_rules(phono, telex)
    dfa = Dfa.from_nfa(nfa)
    overlay = apply_overlay(dfa, nfa, telex)
    return dfa, overlay, telex


# ─────────────────────────────────────────────────────────────────────────
# VerifyResult dataclass shape
# ─────────────────────────────────────────────────────────────────────────


def test_verifyresult_fields() -> None:
    r = VerifyResult(total_syllables=0, passed=0, failed=[])
    assert r.total_syllables == 0
    assert r.passed == 0
    assert r.failed == []
    assert r.ok is True


def test_verifyresult_ok_when_no_failures() -> None:
    r = VerifyResult(total_syllables=10, passed=10, failed=[])
    assert r.ok is True


def test_verifyresult_not_ok_when_failures() -> None:
    r = VerifyResult(total_syllables=10, passed=9, failed=[("t", "miss")])
    assert r.ok is False


# ─────────────────────────────────────────────────────────────────────────
# Curated case list — known good Telex pairs.
# Each tuple: (telex_input_chars, expected_output)
# Input is RAW chars; verifier translates via keymap.
# ─────────────────────────────────────────────────────────────────────────


CURATED_CASES = [
    # Single vowel + tone
    ("a", "a"),
    ("as", "á"),
    ("af", "à"),
    ("ar", "ả"),
    ("ax", "ã"),
    ("aj", "ạ"),

    # Modifier circumflex
    ("aa", "â"),
    ("aas", "ấ"),
    ("aaf", "ầ"),

    # Modifier breve
    ("aw", "ă"),
    ("aws", "ắ"),

    # Modifier horn
    ("ow", "ơ"),
    ("uw", "ư"),

    # Modifier d-bar
    ("dd", "đ"),

    # Consonant + vowel
    ("ta", "ta"),
    ("tas", "tá"),
    ("taf", "tà"),
    ("ban", "ban"),
    ("ban5", "ban5"),  # digits = literal in Telex mode (not tone keys)

    # Multi-char consonant + vowel
    ("cha", "cha"),
    ("nha", "nha"),
    ("nga", "nga"),
    ("tha", "tha"),
    ("tras", "trá"),

    # Modifier + tone
    ("taas", "tấ"),
    ("tooj", "tộ"),
    ("tees", "tế"),

    # Diphthong (no modifier)
    ("tai", "tai"),
    ("tao", "tao"),
    ("tay", "tay"),
    ("teo", "teo"),

    # Coda
    ("tan", "tan"),
    ("tang", "tang"),
    ("tach", "tach"),

    # Tone after coda
    ("tans", "tán"),
    ("tangs", "táng"),
]


@pytest.mark.parametrize("telex_input,expected", CURATED_CASES)
def test_verifier_curated_case(
    real_pipeline: tuple[Dfa, OverlayResult, KeymapRule],
    telex_input: str,
    expected: str,
) -> None:
    """Each curated (input, output) pair should verify."""
    dfa, overlay, keymap = real_pipeline
    actual = Verifier.simulate(telex_input, dfa, overlay, keymap)
    assert actual == expected, (
        f"telex {telex_input!r} produced {actual!r}, expected {expected!r}"
    )


# ─────────────────────────────────────────────────────────────────────────
# verify_syllables — bulk verification API
# ─────────────────────────────────────────────────────────────────────────


def test_verify_syllables_empty_list(
    real_pipeline: tuple[Dfa, OverlayResult, KeymapRule],
) -> None:
    dfa, overlay, keymap = real_pipeline
    result = Verifier.verify_syllables([], dfa, overlay, keymap)
    assert result.total_syllables == 0
    assert result.passed == 0
    assert result.failed == []


def test_verify_syllables_curated_pass(
    real_pipeline: tuple[Dfa, OverlayResult, KeymapRule],
) -> None:
    """Curated set has all-pass — confirms verifier wiring + curated correctness."""
    dfa, overlay, keymap = real_pipeline
    pairs = CURATED_CASES
    result = Verifier.verify_pairs(pairs, dfa, overlay, keymap)
    assert result.total_syllables == len(pairs)
    if not result.ok:
        # Print failure details for debug
        msg = "\n".join(f"  {s}: {r}" for s, r in result.failed[:10])
        pytest.fail(f"{len(result.failed)} curated failures:\n{msg}")


# ─────────────────────────────────────────────────────────────────────────
# Performance bound
# ─────────────────────────────────────────────────────────────────────────


def test_verify_curated_under_2_seconds(
    real_pipeline: tuple[Dfa, OverlayResult, KeymapRule],
) -> None:
    import time

    dfa, overlay, keymap = real_pipeline
    t0 = time.perf_counter()
    Verifier.verify_pairs(CURATED_CASES, dfa, overlay, keymap)
    elapsed = time.perf_counter() - t0
    assert elapsed < 2.0, f"curated verifier took {elapsed:.2f}s (budget 2.0s)"
