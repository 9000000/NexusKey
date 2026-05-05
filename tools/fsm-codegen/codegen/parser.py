"""Parse phonotactic rules + keymap from TOML files.

Implementation deferred to D-1 Task 2.
"""
from __future__ import annotations

from dataclasses import dataclass, field
from pathlib import Path


@dataclass
class PhonotacticsRule:
    """Vietnamese syllable structural rules — see docs/RuleTiengViet_Summary.md.

    Populated by RuleParser.load_phonotactics(). Empty here.
    """

    initial_consonants: list[str] = field(default_factory=list)
    vowels_single: list[str] = field(default_factory=list)
    vowels_diphthong: list[str] = field(default_factory=list)
    vowels_triphthong: list[str] = field(default_factory=list)
    final_consonants_c1: list[str] = field(default_factory=list)
    final_consonants_c2: list[str] = field(default_factory=list)
    final_consonants_c3: list[str] = field(default_factory=list)
    n1_vowels: list[str] = field(default_factory=list)
    n2_vowels: list[str] = field(default_factory=list)
    n3_vowels: list[str] = field(default_factory=list)
    closed_vowels: list[str] = field(default_factory=list)
    suspended_vowels: list[str] = field(default_factory=list)


@dataclass
class KeymapRule:
    """Method-specific raw-key → AbstractInput mapping.

    `mappings` covers single-press keys (e.g. Telex 'f' → TONE_HUYEN).
    `double_sequences` covers 2-char modifier triggers (e.g. 'aa' → MOD_CIRCUMFLEX_a).
    """

    method_name: str = ""
    mappings: dict[str, str] = field(default_factory=dict)
    double_sequences: dict[str, str] = field(default_factory=dict)


class RuleParser:
    """Loads TOML rule files into typed dataclasses.

    Implementation: D-1 Task 2.
    """

    @staticmethod
    def load_phonotactics(path: Path) -> PhonotacticsRule:
        raise NotImplementedError("Task 2")

    @staticmethod
    def load_keymap(path: Path) -> KeymapRule:
        raise NotImplementedError("Task 2")
