"""Exhaustive verifier — enumerate 20,504 valid Vietnamese syllables, simulate on DFA.

Per docs/RuleTiengViet_Summary.md: 20,504 orthographic combinations writable.
Verifier asserts every one is reached by some Telex/VNI input sequence in DFA.

Implementation deferred to D-1 Task 7.
"""
from __future__ import annotations

from dataclasses import dataclass

from .dfa import Dfa
from .parser import KeymapRule, PhonotacticsRule


@dataclass
class VerifyResult:
    total_syllables: int
    passed: int
    failed: list[tuple[str, str]]  # (syllable, reason)

    @property
    def ok(self) -> bool:
        return len(self.failed) == 0


class Verifier:
    @staticmethod
    def verify_exhaustive(
        rules: PhonotacticsRule, keymap: KeymapRule, dfa: Dfa
    ) -> VerifyResult:
        """Enumerate 20,504 valid syllables; simulate input on DFA; assert acceptance.

        Time budget: ≤ 30 seconds (else codegen too slow for build pipeline).

        Implementation: D-1 Task 7.
        """
        raise NotImplementedError("Task 7")
