"""Parse phonotactic rules + keymap from TOML files.

D-1 Task 2b implementation. Tests in tests/test_parser.py.
"""
from __future__ import annotations

import tomllib
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any


class RuleSchemaError(ValueError):
    """Raised when a rule TOML file fails schema validation.

    Wraps both malformed-TOML errors and missing/invalid-field errors.
    Tests rely on the message containing relevant section/field names.
    """


@dataclass
class PhonotacticsRule:
    """Vietnamese syllable structural rules.

    Source-of-truth: rules/vietnamese-phonotactics.toml.
    Layout follows the TOML schema 1:1 — see file comments + design doc §5.
    """

    # ─── consonants ──────────────────────────────────────────────────────
    initial_consonants_d1: list[str] = field(default_factory=list)
    initial_consonants_d2: list[str] = field(default_factory=list)
    initial_consonants_d3: list[str] = field(default_factory=list)

    # special_initials.{c|g|ng}_variants — sub-table per variant family.
    # Each value is a dict with keys: default, default_before, k_before|gh_before|ngh_before|qu_before
    special_initials: dict[str, dict[str, Any]] = field(default_factory=dict)

    # ─── vowels ──────────────────────────────────────────────────────────
    vowels_single: list[str] = field(default_factory=list)
    vowels_diphthong: list[str] = field(default_factory=list)
    vowels_triphthong: list[str] = field(default_factory=list)

    # ─── final consonants (codas) ────────────────────────────────────────
    finals_c1: list[str] = field(default_factory=list)
    finals_c2: list[str] = field(default_factory=list)
    finals_c3: list[str] = field(default_factory=list)

    # ─── vowel × coda groupings (N1/N2/N3) ───────────────────────────────
    n1_vowels: list[str] = field(default_factory=list)
    n2_vowels: list[str] = field(default_factory=list)
    n3_vowels: list[str] = field(default_factory=list)

    # ─── vowel constraints (closed/suspended) ────────────────────────────
    closed_vowels: list[str] = field(default_factory=list)
    suspended_vowels: list[str] = field(default_factory=list)

    # ─── tones ───────────────────────────────────────────────────────────
    tones_all: list[str] = field(default_factory=list)
    tone_hard_constraint_codas: list[str] = field(default_factory=list)
    tone_hard_constraint_allowed: list[str] = field(default_factory=list)

    # ─── tone placement ──────────────────────────────────────────────────
    tone_placement_2vowel_no_coda: int = 1
    tone_placement_2vowel_with_coda: int = 2
    tone_placement_3vowel: int = 2
    tone_placement_uye_exception: int = 3
    tone_placement_classic_modern_diphthongs: list[str] = field(default_factory=list)
    tone_placement_default: str = "classic"

    # ─── modifiers ───────────────────────────────────────────────────────
    modifiers_all: list[str] = field(default_factory=list)
    modifier_targets: dict[str, list[str]] = field(default_factory=dict)

    # ─── auto modifier completion ────────────────────────────────────────
    horn_forward_propagation: list[dict[str, str]] = field(default_factory=list)
    uo_default_result: str = "ươ"
    uo_edge_result: str = "uơ"
    uo_edge_initials: list[str] = field(default_factory=list)


@dataclass
class KeymapRule:
    """Method-specific raw-key → AbstractInput mapping.

    Source: rules/{telex,telex-simple,vni}-keymap.toml.
    """

    method_name: str = ""
    spec_source: str = ""
    mappings: dict[str, str] = field(default_factory=dict)
    double_sequences: dict[str, str] = field(default_factory=dict)


# ────────────────────────────────────────────────────────────────────────
# Helpers
# ────────────────────────────────────────────────────────────────────────


def _load_toml(path: Path) -> dict[str, Any]:
    """Read TOML file, wrap parser errors in RuleSchemaError."""
    if not path.exists():
        raise FileNotFoundError(f"Rule file not found: {path}")
    try:
        with open(path, "rb") as fp:
            return tomllib.load(fp)
    except tomllib.TOMLDecodeError as exc:
        raise RuleSchemaError(
            f"malformed TOML in {path.name}: {exc}"
        ) from exc


def _require(data: dict[str, Any], path: str, kind: type) -> Any:
    """Walk dotted path; raise RuleSchemaError if missing or wrong type."""
    cur: Any = data
    for part in path.split("."):
        if not isinstance(cur, dict) or part not in cur:
            raise RuleSchemaError(
                f"missing required section/field: {path}"
            )
        cur = cur[part]
    if not isinstance(cur, kind):
        raise RuleSchemaError(
            f"field {path!r}: expected {kind.__name__}, got {type(cur).__name__}"
        )
    return cur


def _opt(data: dict[str, Any], path: str, default: Any = None) -> Any:
    """Optional field accessor; returns default when absent."""
    cur: Any = data
    for part in path.split("."):
        if not isinstance(cur, dict) or part not in cur:
            return default
        cur = cur[part]
    return cur


# ────────────────────────────────────────────────────────────────────────
# Public API
# ────────────────────────────────────────────────────────────────────────


class RuleParser:
    """Loads TOML rule files into typed dataclasses."""

    @staticmethod
    def load_phonotactics(path: Path) -> PhonotacticsRule:
        data = _load_toml(path)

        rule = PhonotacticsRule(
            initial_consonants_d1=_require(data, "consonants.initial_d1", list),
            initial_consonants_d2=_require(data, "consonants.initial_d2", list),
            initial_consonants_d3=_require(data, "consonants.initial_d3", list),
            special_initials=_opt(data, "special_initials", default={}),
            vowels_single=_require(data, "vowels.single", list),
            vowels_diphthong=_require(data, "vowels.diphthong", list),
            vowels_triphthong=_require(data, "vowels.triphthong", list),
            finals_c1=_require(data, "final_consonants.c1", list),
            finals_c2=_require(data, "final_consonants.c2", list),
            finals_c3=_require(data, "final_consonants.c3", list),
            n1_vowels=_require(data, "vowel_groupings.n1", list),
            n2_vowels=_require(data, "vowel_groupings.n2", list),
            n3_vowels=_require(data, "vowel_groupings.n3", list),
            closed_vowels=_require(data, "vowel_constraints.closed", list),
            suspended_vowels=_require(data, "vowel_constraints.suspended", list),
            tones_all=_require(data, "tones.all", list),
            tone_hard_constraint_codas=_require(
                data, "tone_constraints.hard_constraint.codas", list
            ),
            tone_hard_constraint_allowed=_require(
                data, "tone_constraints.hard_constraint.allowed", list
            ),
            tone_placement_2vowel_no_coda=_require(
                data, "tone_placement.default_2vowel_no_coda", int
            ),
            tone_placement_2vowel_with_coda=_require(
                data, "tone_placement.default_2vowel_with_coda", int
            ),
            tone_placement_3vowel=_require(
                data, "tone_placement.default_3vowel", int
            ),
            tone_placement_uye_exception=_require(
                data, "tone_placement.exception_uye", int
            ),
            tone_placement_classic_modern_diphthongs=_require(
                data, "tone_placement_classic_vs_modern.diphthongs_with_variant", list
            ),
            tone_placement_default=_require(
                data, "tone_placement_classic_vs_modern.default_to", str
            ),
            modifiers_all=_require(data, "modifiers.all", list),
            modifier_targets=_require(data, "modifiers.modifier_targets", dict),
            horn_forward_propagation=_require(
                data, "auto_modifier_completion.horn_forward_propagation", list
            ),
            uo_default_result=_require(
                data, "auto_modifier_completion.horn_initial_uo_pair.default_result", str
            ),
            uo_edge_result=_require(
                data, "auto_modifier_completion.horn_initial_uo_pair.edge_result", str
            ),
            uo_edge_initials=_require(
                data, "auto_modifier_completion.horn_initial_uo_pair.edge_initials", list
            ),
        )

        # Cross-validation: list-non-empty, default-classic-or-modern, etc.
        if not rule.initial_consonants_d1:
            raise RuleSchemaError("consonants.initial_d1 must be non-empty")
        if rule.tone_placement_default not in ("classic", "modern"):
            raise RuleSchemaError(
                f"tone_placement_classic_vs_modern.default_to must be "
                f"'classic' or 'modern', got {rule.tone_placement_default!r}"
            )

        return rule

    @staticmethod
    def load_keymap(path: Path) -> KeymapRule:
        data = _load_toml(path)

        method_name = _require(data, "meta.method_name", str)
        spec_source = _opt(data, "meta.spec_source", default="")
        mappings = _require(data, "mappings", dict)
        if not mappings:
            raise RuleSchemaError(
                f"keymap {path.name}: [mappings] must not be empty"
            )

        # Coerce mapping values to str (TOML int keys become Python int when
        # written as `1 = ...`, but we wrote them quoted as `"1" = ...`).
        for k, v in mappings.items():
            if not isinstance(k, str) or not isinstance(v, str):
                raise RuleSchemaError(
                    f"keymap {path.name}: mapping key/value must be string, "
                    f"got {type(k).__name__}/{type(v).__name__}"
                )

        double_sequences = _opt(data, "double_sequences", default={})
        if not isinstance(double_sequences, dict):
            raise RuleSchemaError(
                f"keymap {path.name}: double_sequences must be a table"
            )

        return KeymapRule(
            method_name=method_name,
            spec_source=spec_source,
            mappings=dict(mappings),
            double_sequences=dict(double_sequences),
        )
