"""Build NFA from phonotactic rules + keymap.

Implementation deferred to D-1 Task 3.
"""
from __future__ import annotations

from dataclasses import dataclass, field

from .parser import KeymapRule, PhonotacticsRule


@dataclass(frozen=True)
class NfaState:
    """Abstract syllable-position label.

    Examples:
        S0 — start of syllable
        S1[t] — typed initial consonant 't'
        S2[t,a] — typed 'ta'
        S3[t,a,sắc] — typed 'tas' → 'tá'
    """

    label: str


@dataclass
class NfaTransition:
    src: NfaState
    input_symbol: str  # AbstractInput name, e.g. "TONE_SAC", "INSERT_LITERAL_a"
    dst: NfaState


@dataclass
class Nfa:
    states: set[NfaState] = field(default_factory=set)
    start: NfaState | None = None
    accepts: set[NfaState] = field(default_factory=set)
    transitions: list[NfaTransition] = field(default_factory=list)

    @classmethod
    def from_rules(cls, rules: PhonotacticsRule, keymap: KeymapRule) -> "Nfa":
        """Walk phonotactic structure + apply keymap → NFA.

        Implementation: D-1 Task 3.
        """
        raise NotImplementedError("Task 3")
