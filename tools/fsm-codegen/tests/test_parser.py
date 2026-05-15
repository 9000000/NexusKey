"""Unit tests for codegen.parser.

Test-first per docs/CODING_RULES + project_test_first memory.
Implementation lands in codegen/parser.py AFTER these tests are written.
"""
from __future__ import annotations

from pathlib import Path

import pytest

from codegen.parser import (
    KeymapRule,
    PhonotacticsRule,
    RuleParser,
    RuleSchemaError,
)


# ─────────────────────────────────────────────────────────────────────────
# Fixtures — point at real rule files in repo's rules/ dir.
# ─────────────────────────────────────────────────────────────────────────

REPO_ROOT = Path(__file__).resolve().parents[3]
RULES_DIR = REPO_ROOT / "rules"

PHONO_PATH         = RULES_DIR / "vietnamese-phonotactics.toml"
TELEX_PATH         = RULES_DIR / "telex-keymap.toml"
TELEX_SIMPLE_PATH  = RULES_DIR / "telex-simple-keymap.toml"
VNI_PATH           = RULES_DIR / "vni-keymap.toml"


# ─────────────────────────────────────────────────────────────────────────
# load_phonotactics — happy path
# ─────────────────────────────────────────────────────────────────────────


def test_phono_loads_real_repo_file() -> None:
    rule = RuleParser.load_phonotactics(PHONO_PATH)
    assert isinstance(rule, PhonotacticsRule)


def test_phono_consonant_counts() -> None:
    rule = RuleParser.load_phonotactics(PHONO_PATH)
    assert len(rule.initial_consonants_d1) == 16
    assert len(rule.initial_consonants_d2) == 4
    assert len(rule.initial_consonants_d3) == 3


def test_phono_vowel_counts() -> None:
    rule = RuleParser.load_phonotactics(PHONO_PATH)
    assert len(rule.vowels_single) == 11
    # 30 → 29 after anh removed ôô
    assert len(rule.vowels_diphthong) == 29
    assert len(rule.vowels_triphthong) == 12


def test_phono_no_oo_double() -> None:
    """ôô must not be in diphthong list (anh confirmed not real Vietnamese)."""
    rule = RuleParser.load_phonotactics(PHONO_PATH)
    assert "ôô" not in rule.vowels_diphthong
    assert "oo" in rule.vowels_diphthong  # kept for loanwords (xoong)
    assert "ôô" not in rule.suspended_vowels
    assert "oo" in rule.suspended_vowels


def test_phono_classic_default() -> None:
    """Anh 2026-05-05: VKey default tone placement is classic (òa)."""
    rule = RuleParser.load_phonotactics(PHONO_PATH)
    assert rule.tone_placement_default == "classic"


def test_phono_uo_edge_initials() -> None:
    """uo + horn after h/th/kh → uơ (only o horned), else → ươ (both)."""
    rule = RuleParser.load_phonotactics(PHONO_PATH)
    assert rule.uo_edge_initials == ["h", "th", "kh"]
    assert rule.uo_edge_result == "uơ"
    assert rule.uo_default_result == "ươ"


def test_phono_tone_hard_constraint() -> None:
    """Coda c/ch/p/t restricts tones to sắc/nặng only."""
    rule = RuleParser.load_phonotactics(PHONO_PATH)
    assert rule.tone_hard_constraint_codas == ["c", "ch", "p", "t"]
    assert sorted(rule.tone_hard_constraint_allowed) == ["nặng", "sắc"]


def test_phono_modifier_targets() -> None:
    rule = RuleParser.load_phonotactics(PHONO_PATH)
    assert rule.modifier_targets["circumflex"] == ["a", "o", "e"]
    assert rule.modifier_targets["breve"] == ["a"]
    assert rule.modifier_targets["horn"] == ["o", "u"]
    assert rule.modifier_targets["d_bar"] == ["d"]


# ─────────────────────────────────────────────────────────────────────────
# load_keymap — happy path
# ─────────────────────────────────────────────────────────────────────────


def test_telex_keymap_loads() -> None:
    km = RuleParser.load_keymap(TELEX_PATH)
    assert isinstance(km, KeymapRule)
    assert km.method_name == "telex"


def test_telex_keymap_full_has_brackets_and_w() -> None:
    """Telex full has w + ] + [ direct shortcuts."""
    km = RuleParser.load_keymap(TELEX_PATH)
    assert km.mappings["w"] == "MOD_HORN"
    assert km.mappings["]"] == "INSERT_VOWEL_U_HORN"
    assert km.mappings["["] == "INSERT_VOWEL_O_HORN"


def test_telex_keymap_z_clear_tone() -> None:
    km = RuleParser.load_keymap(TELEX_PATH)
    assert km.mappings["z"] == "TONE_CLEAR"
    assert km.mappings["Z"] == "TONE_CLEAR"


def test_telex_keymap_double_sequences() -> None:
    km = RuleParser.load_keymap(TELEX_PATH)
    assert km.double_sequences["aa"] == "MOD_CIRCUMFLEX"
    assert km.double_sequences["dd"] == "MOD_D_BAR"
    assert km.double_sequences["uw"] == "MOD_HORN"


def test_telex_simple_omits_w_and_brackets() -> None:
    """Telex Simple subset: no w/W/]/[ in mappings."""
    km = RuleParser.load_keymap(TELEX_SIMPLE_PATH)
    assert "w" not in km.mappings
    assert "W" not in km.mappings
    assert "]" not in km.mappings
    assert "[" not in km.mappings
    # But tone keys + clear-tone preserved
    assert km.mappings["s"] == "TONE_SAC"
    assert km.mappings["z"] == "TONE_CLEAR"
    # Double sequences preserved (uw still works for ư via 2-char)
    assert km.double_sequences["uw"] == "MOD_HORN"


def test_vni_keymap_zero_clear_tone() -> None:
    km = RuleParser.load_keymap(VNI_PATH)
    assert km.mappings["0"] == "TONE_CLEAR"


def test_vni_keymap_modifier_digits() -> None:
    km = RuleParser.load_keymap(VNI_PATH)
    assert km.mappings["6"] == "MOD_CIRCUMFLEX"
    assert km.mappings["7"] == "MOD_HORN"
    assert km.mappings["8"] == "MOD_BREVE"
    assert km.mappings["9"] == "MOD_D_BAR"


def test_vni_keymap_no_double_sequences() -> None:
    """VNI is digit-only; no 2-char modifier sequences."""
    km = RuleParser.load_keymap(VNI_PATH)
    assert km.double_sequences == {}


# ─────────────────────────────────────────────────────────────────────────
# Error handling — malformed input
# ─────────────────────────────────────────────────────────────────────────


def test_phono_missing_file_raises(tmp_path: Path) -> None:
    fake = tmp_path / "nope.toml"
    with pytest.raises(FileNotFoundError):
        RuleParser.load_phonotactics(fake)


def test_phono_malformed_toml_raises(tmp_path: Path) -> None:
    f = tmp_path / "bad.toml"
    f.write_text("this is = = not valid TOML")
    with pytest.raises(RuleSchemaError):
        RuleParser.load_phonotactics(f)


def test_phono_missing_required_section_raises(tmp_path: Path) -> None:
    f = tmp_path / "missing.toml"
    f.write_text('[meta]\nversion = "1.0"\n')  # no [consonants], [vowels] etc
    with pytest.raises(RuleSchemaError, match=r"(consonants|vowels|missing)"):
        RuleParser.load_phonotactics(f)


def test_keymap_missing_meta_method_raises(tmp_path: Path) -> None:
    f = tmp_path / "no-meta.toml"
    f.write_text('[mappings]\n"a" = "TONE_SAC"\n')
    with pytest.raises(RuleSchemaError, match=r"(meta|method_name)"):
        RuleParser.load_keymap(f)


def test_keymap_empty_mappings_raises(tmp_path: Path) -> None:
    f = tmp_path / "empty.toml"
    f.write_text(
        '[meta]\nmethod_name = "x"\nspec_source = "x"\n[mappings]\n'
    )
    with pytest.raises(RuleSchemaError, match=r"(empty|mappings)"):
        RuleParser.load_keymap(f)


# ─────────────────────────────────────────────────────────────────────────
# Cross-rule consistency
# ─────────────────────────────────────────────────────────────────────────


def test_phono_modifier_all_includes_targets_keys() -> None:
    """modifiers.all should contain all keys present in modifier_targets."""
    rule = RuleParser.load_phonotactics(PHONO_PATH)
    target_keys = set(rule.modifier_targets.keys())
    all_minus_none = set(rule.modifiers_all) - {"none"}
    assert target_keys.issubset(all_minus_none), (
        f"modifier_targets has keys not in modifiers.all: "
        f"{target_keys - all_minus_none}"
    )


def test_phono_n1_n2_n3_disjoint_or_documented() -> None:
    """N1/N2/N3 should NOT overlap (each vowel in exactly one group)."""
    rule = RuleParser.load_phonotactics(PHONO_PATH)
    n1, n2, n3 = set(rule.n1_vowels), set(rule.n2_vowels), set(rule.n3_vowels)
    assert n1.isdisjoint(n2), f"N1 ∩ N2 = {n1 & n2}"
    assert n1.isdisjoint(n3), f"N1 ∩ N3 = {n1 & n3}"
    assert n2.isdisjoint(n3), f"N2 ∩ N3 = {n2 & n3}"
