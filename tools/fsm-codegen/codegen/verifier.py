"""Exhaustive verifier — simulate input through DFA, verify rendered text.

D-1 Task 7. Validates the codegen pipeline's correctness:
  1. Translates raw Telex/VNI characters to AbstractInput names via keymap.
  2. Walks the DFA from start, applying overlay Action per transition.
  3. Returns the final accumulated text.

Per docs/RuleTiengViet_Summary.md: 20,504 valid orthographic syllables.
Full enumeration deferred to a follow-up; this implementation supplies
the core simulator + a curated-pair verifier (~30 known good cases).
"""
from __future__ import annotations

from dataclasses import dataclass

from .dfa import Dfa
from .overlay import OverlayResult
from .parser import KeymapRule


@dataclass
class VerifyResult:
    total_syllables: int = 0
    passed: int = 0
    failed: list[tuple[str, str]] = None  # type: ignore[assignment]

    def __post_init__(self) -> None:
        if self.failed is None:
            self.failed = []

    @property
    def ok(self) -> bool:
        return len(self.failed) == 0


class Verifier:
    """Pure-static helpers — no instance state."""

    # ── Raw → abstract input translation ──────────────────────────────────

    @staticmethod
    def translate(raw: str, keymap: KeymapRule) -> list[str]:
        """Convert raw Telex/VNI char sequence to AbstractInput names.

        Per-char lookup in keymap.mappings (e.g., 's' → TONE_SAC); fall
        back to INSERT_LITERAL_<lowercase> for unmapped chars (vowels +
        consonants are handled by FSM transitions, not by keymap).

        Note: 2-char double_sequences (aa, oo, etc.) are NOT translated to
        a special symbol — both chars produce INSERT_LITERAL_<x>, and the
        FSM table's state transitions encode the modifier behavior (Layer C
        in nfa.py).
        """
        out: list[str] = []
        for ch in raw:
            if ch in keymap.mappings:
                out.append(keymap.mappings[ch])
            else:
                out.append(f"INSERT_LITERAL_{ch.lower()}")
        return out

    # ── Simulation ────────────────────────────────────────────────────────

    @staticmethod
    def simulate(
        telex_input: str,
        dfa: Dfa,
        overlay: OverlayResult,
        keymap: KeymapRule,
    ) -> str:
        """Walk DFA on translated input; accumulate text via Actions.

        Returns the final visible text. If FSM hits a dead state (no
        transition for current input), returns the text accumulated so far.
        """
        if dfa.start is None:
            return ""

        abstract = Verifier.translate(telex_input, keymap)

        text = ""
        cur_id = dfa.start.state_id
        for sym in abstract:
            edges = dfa.transitions.get(cur_id, {})
            if sym not in edges:
                # Dead — best effort. In production hook, the unhandled
                # input would be passed through to OS as literal.
                return text + Verifier._raw_for_unhandled(sym)
            dst_id = edges[sym]

            action_id = overlay.transition_actions.get(cur_id, {}).get(sym)
            if action_id is not None:
                action = overlay.action_table[action_id]
                if action.bs_count > 0:
                    text = text[: -action.bs_count]
                text += action.insert_text

            cur_id = dst_id

        return text

    @staticmethod
    def _raw_for_unhandled(sym: str) -> str:
        """Recover literal text from an INSERT_LITERAL_<x> symbol that
        had no transition. Returns x for INSERT_LITERAL_x, else empty
        (other abstract inputs like TONE_SAC have no literal fallback in
        a verifier context — they'd become the raw 's' in production)."""
        if sym.startswith("INSERT_LITERAL_"):
            return sym[len("INSERT_LITERAL_"):]
        return ""

    # ── Pair verification ─────────────────────────────────────────────────

    @staticmethod
    def verify_pairs(
        pairs: list[tuple[str, str]],
        dfa: Dfa,
        overlay: OverlayResult,
        keymap: KeymapRule,
    ) -> VerifyResult:
        """Verify each (telex_input, expected_output) pair.

        Returns VerifyResult with pass/fail counts and per-case failure
        details (limited to a sane upper bound to avoid log blow-up).
        """
        result = VerifyResult(total_syllables=len(pairs))
        for telex_input, expected in pairs:
            actual = Verifier.simulate(telex_input, dfa, overlay, keymap)
            if actual == expected:
                result.passed += 1
            else:
                result.failed.append(
                    (expected, f"input={telex_input!r} got={actual!r}")
                )
        return result

    # ── Bulk syllable verification (Task 7 stretch — currently a thin
    #    wrapper; full enumerate-and-compose deferred to Task 7b) ────────

    @staticmethod
    def verify_syllables(
        syllables: list[str],
        dfa: Dfa,
        overlay: OverlayResult,
        keymap: KeymapRule,
    ) -> VerifyResult:
        """Verify a list of expected syllables — derives Telex input via
        a naive composer.

        TODO Task 7b: implement full syllable→Telex composer covering
        diphthong/triphthong/tone-placement edge cases. For now, returns
        empty pass for empty input and a placeholder result otherwise.
        """
        if not syllables:
            return VerifyResult(total_syllables=0)
        # Stub: declare all unknown until composer lands.
        return VerifyResult(
            total_syllables=len(syllables),
            passed=0,
            failed=[(s, "verify_syllables composer pending Task 7b") for s in syllables],
        )
