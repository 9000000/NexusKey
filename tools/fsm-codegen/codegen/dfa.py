"""NFA → DFA via subset construction; Hopcroft minimization.

Implementation deferred to D-1 Task 4 (subset construction) + Task 5 (Hopcroft).
"""
from __future__ import annotations

from dataclasses import dataclass, field

from .nfa import Nfa


@dataclass
class DfaState:
    state_id: int
    is_accept: bool = False


@dataclass
class Dfa:
    states: list[DfaState] = field(default_factory=list)
    start: DfaState | None = None
    # transitions[state_id][input_symbol] -> dst state_id
    transitions: dict[int, dict[str, int]] = field(default_factory=dict)
    # action_id per accept state — populated during overlay (Task 6)
    actions: dict[int, str] = field(default_factory=dict)

    @classmethod
    def from_nfa(cls, nfa: Nfa) -> "Dfa":
        """Subset construction: NFA → DFA.

        Implementation: D-1 Task 4.
        """
        raise NotImplementedError("Task 4")

    def minimize(self) -> "Dfa":
        """Hopcroft minimization: collapse equivalent states.

        Implementation: D-1 Task 5.
        """
        raise NotImplementedError("Task 5")
