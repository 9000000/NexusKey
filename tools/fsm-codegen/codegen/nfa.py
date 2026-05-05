"""Build NFA from phonotactic rules + keymap.

D-1 Task 3b implementation. Tests in tests/test_nfa.py.

Design (locked in test_nfa.py docstring):
  - NFA receives AbstractInput name strings ("TONE_SAC", "INSERT_LITERAL_a"),
    NOT raw keys. Keymap layer translates raw → abstract BEFORE NFA.
  - NfaState is a frozen dataclass — hashable, value-equal.
  - Action emission OUT OF SCOPE for NFA. Escape states are tagged with
    is_escape=True; their literal output text is computed by Task 6 overlay.
  - NFA is permissive: allow more transitions than strictly valid Vietnamese.
    Codegen verifier (Task 7) is the strictness gate. Keeps NFA focused on
    structure, not orthographic policing.
"""
from __future__ import annotations

from dataclasses import dataclass, field
from typing import Iterable

from .parser import KeymapRule, PhonotacticsRule


# ─────────────────────────────────────────────────────────────────────────
# State + transition types — frozen, hashable.
# ─────────────────────────────────────────────────────────────────────────


@dataclass(frozen=True)
class NfaState:
    """Snapshot of in-progress syllable + status flags.

    Equality + hash by all fields — two states with same syllable/tone/etc.
    are the same NFA state (mergeable).
    """

    cons: str = ""                      # initial consonant chars typed
    vowels: tuple[str, ...] = ()        # vowel sequence (each may be modified)
    mods: tuple[str, ...] = ()          # parallel — modifier name per vowel
    tone: str = "none"                  # current tone
    coda: str = ""                      # final consonant
    is_start: bool = False
    is_escape: bool = False             # this state was reached via escape


@dataclass(frozen=True)
class NfaTransition:
    src: NfaState
    input_symbol: str                   # AbstractInput name
    dst: NfaState


# ─────────────────────────────────────────────────────────────────────────
# Helpers — modifier maps, vowel/coda lookup.
# ─────────────────────────────────────────────────────────────────────────


# Hard-coded Vietnamese modifier maps. Source-of-truth ultimately should
# live in vietnamese-phonotactics.toml; for D-1 these are inline.
_CIRCUMFLEX = {"a": "â", "o": "ô", "e": "ê"}
_BREVE      = {"a": "ă"}
_HORN       = {"o": "ơ", "u": "ư"}


# Reverse-map from modified vowel back to base — for escape semantics.
_UNMODIFIED = {v: k for k, v in {**_CIRCUMFLEX, **_BREVE, **_HORN}.items()}


def _is_vowel_char(ch: str, phono: PhonotacticsRule) -> bool:
    return ch in phono.vowels_single


def _is_coda_char(ch: str, phono: PhonotacticsRule) -> bool:
    """First char of any final consonant qualifies as coda starter."""
    return ch in {c[0] for c in (phono.finals_c1 + phono.finals_c2 + phono.finals_c3)}


# ─────────────────────────────────────────────────────────────────────────
# Nfa container.
# ─────────────────────────────────────────────────────────────────────────


@dataclass
class Nfa:
    _states: set[NfaState] = field(default_factory=set)
    _start: NfaState | None = None
    _transitions: list[NfaTransition] = field(default_factory=list)
    # Outgoing index: (src, input_symbol) → set[NfaState]
    _outgoing: dict[tuple[NfaState, str], set[NfaState]] = field(default_factory=dict)

    # ── public API ────────────────────────────────────────────────────────

    def states(self) -> set[NfaState]:
        return self._states

    def start_state(self) -> NfaState:
        assert self._start is not None, "NFA not built — call from_rules()"
        return self._start

    def accept_states(self) -> set[NfaState]:
        """States representing a (potentially) complete syllable.

        Vietnamese syllable requires AT LEAST one vowel — bare consonant-only
        states (cons="t", vowels=()) are NOT accept. Tone/modifier alone are
        also non-accept (no vowels). Strict orthographic validity (N1/N2/N3,
        suspended/closed) is enforced by Task 7 verifier, not NFA structure.
        """
        return {
            s for s in self._states
            if not s.is_start and len(s.vowels) >= 1
        }

    def accepts(self, inputs: Iterable[str]) -> bool:
        current = {self._start} if self._start else set()
        for inp in inputs:
            nxt: set[NfaState] = set()
            for s in current:
                nxt |= self._outgoing.get((s, inp), set())
            current = nxt
            if not current:
                return False
        return any(s in self.accept_states() for s in current)

    # ── internal: transition adders ────────────────────────────────────────

    def _add_state(self, s: NfaState) -> None:
        self._states.add(s)

    def _add_transition(self, src: NfaState, sym: str, dst: NfaState) -> None:
        self._add_state(src)
        self._add_state(dst)
        self._transitions.append(NfaTransition(src, sym, dst))
        self._outgoing.setdefault((src, sym), set()).add(dst)

    # ── builder ────────────────────────────────────────────────────────────

    @classmethod
    def from_rules(cls, phono: PhonotacticsRule, keymap: KeymapRule) -> "Nfa":
        nfa = cls()
        start = NfaState(is_start=True)
        nfa._start = start
        nfa._add_state(start)

        # Pre-compute commonly-used sets.
        initials = (
            phono.initial_consonants_d1
            + phono.initial_consonants_d2
            + phono.initial_consonants_d3
        )
        all_vowels_single = phono.vowels_single
        all_codas = phono.finals_c1 + phono.finals_c2 + phono.finals_c3

        # ── Layer A: initial consonants ──────────────────────────────────
        # From start, type each initial consonant char-by-char.
        cons_states: dict[str, NfaState] = {}   # final state of each initial path
        for cons in initials:
            cur = start
            buf = ""
            for ch in cons:
                buf += ch
                nxt = NfaState(cons=buf)
                nfa._add_transition(cur, f"INSERT_LITERAL_{ch}", nxt)
                cur = nxt
            cons_states[cons] = cur

        # All "after consonant" entry points for vowel transitions:
        cons_entry: list[tuple[str, NfaState]] = [("", start)] + [
            (cons, st) for cons, st in cons_states.items()
        ]

        # ── Layer B: single-vowel insertion ──────────────────────────────
        # vowel_state_map[(cons, vowel_char)] = state representing syllable
        # cons + that bare vowel.
        vowel_state_map: dict[tuple[str, str], NfaState] = {}
        for cons, src_state in cons_entry:
            for v in all_vowels_single:
                dst = NfaState(cons=cons, vowels=(v,), mods=("none",))
                nfa._add_transition(src_state, f"INSERT_LITERAL_{v}", dst)
                vowel_state_map[(cons, v)] = dst

        # ── Layer C: modifier via double_sequences (Telex 'aa', 'oo' etc.) ─
        # State (cons, vowel='a') + INSERT_LITERAL_a → state (cons, vowel='â')
        # — when keymap has 'aa' → MOD_CIRCUMFLEX.
        for seq_str, abstract in keymap.double_sequences.items():
            if len(seq_str) != 2:
                continue
            seq_lower = seq_str.lower()
            first, second = seq_lower[0], seq_lower[1]
            for cons, _ in cons_entry:
                src = vowel_state_map.get((cons, first))
                if src is None:
                    continue
                # Determine modified vowel char + mod name.
                mod_name, mod_map = None, None
                if abstract == "MOD_CIRCUMFLEX":
                    mod_name, mod_map = "circumflex", _CIRCUMFLEX
                elif abstract == "MOD_BREVE":
                    mod_name, mod_map = "breve", _BREVE
                elif abstract == "MOD_HORN":
                    mod_name, mod_map = "horn", _HORN
                elif abstract == "MOD_D_BAR":
                    # 'dd' → đ — special: modifies CONSONANT, not vowel.
                    # Skip in Layer C; handled inline below if needed.
                    continue
                else:
                    continue

                if first not in mod_map:
                    continue
                modded = mod_map[first]
                dst = NfaState(cons=cons, vowels=(modded,), mods=(mod_name,))
                nfa._add_transition(src, f"INSERT_LITERAL_{second}", dst)

        # ── Layer C': single-press modifier keys (Telex 'w' standalone) ──
        # keymap.mappings has direct abstract input (e.g., 'w' → MOD_HORN).
        # NFA transition: any state with bare u/o + MOD_HORN → modified.
        for raw, abstract in keymap.mappings.items():
            if abstract == "MOD_HORN":
                for cons, _ in cons_entry:
                    for base, modded in _HORN.items():
                        src = vowel_state_map.get((cons, base))
                        if src is None:
                            continue
                        dst = NfaState(cons=cons, vowels=(modded,), mods=("horn",))
                        nfa._add_transition(src, "MOD_HORN", dst)
            elif abstract == "MOD_CIRCUMFLEX":
                for cons, _ in cons_entry:
                    for base, modded in _CIRCUMFLEX.items():
                        src = vowel_state_map.get((cons, base))
                        if src is None:
                            continue
                        dst = NfaState(cons=cons, vowels=(modded,), mods=("circumflex",))
                        nfa._add_transition(src, "MOD_CIRCUMFLEX", dst)
            elif abstract == "MOD_BREVE":
                for cons, _ in cons_entry:
                    src = vowel_state_map.get((cons, "a"))
                    if src is None:
                        continue
                    dst = NfaState(cons=cons, vowels=("ă",), mods=("breve",))
                    nfa._add_transition(src, "MOD_BREVE", dst)

        # ── Layer D: tone application ────────────────────────────────────
        # From any single-vowel state (incl. modified), apply tone via
        # TONE_SAC / TONE_HUYEN / TONE_HOI / TONE_NGA / TONE_NANG.
        tone_inputs = ("TONE_SAC", "TONE_HUYEN", "TONE_HOI", "TONE_NGA", "TONE_NANG")
        tone_name = {
            "TONE_SAC": "sắc", "TONE_HUYEN": "huyền", "TONE_HOI": "hỏi",
            "TONE_NGA": "ngã", "TONE_NANG": "nặng",
        }
        # Walk over all single-vowel states (no tone yet, no coda).
        single_vowel_states = [
            s for s in list(nfa._states)
            if len(s.vowels) == 1 and s.tone == "none" and s.coda == ""
            and not s.is_start
        ]
        for src in single_vowel_states:
            for tin in tone_inputs:
                # Same-tone-twice → escape; handled in Layer F.
                # Different tones replace; encoded directly here.
                dst = NfaState(
                    cons=src.cons, vowels=src.vowels, mods=src.mods,
                    tone=tone_name[tin], coda=src.coda,
                )
                nfa._add_transition(src, tin, dst)

        # ── Layer E: tone replacement on already-toned vowel ─────────────
        # From state with tone X, on TONE_Y (Y != X) → state with tone Y.
        # On TONE_X (same) → escape, handled Layer F.
        toned_states = [
            s for s in list(nfa._states)
            if len(s.vowels) == 1 and s.tone != "none" and s.coda == ""
        ]
        name_to_input = {v: k for k, v in tone_name.items()}
        for src in toned_states:
            cur_input = name_to_input[src.tone]
            for tin in tone_inputs:
                if tin == cur_input:
                    continue   # same tone → escape, Layer F
                new_tone = tone_name[tin]
                dst = NfaState(
                    cons=src.cons, vowels=src.vowels, mods=src.mods,
                    tone=new_tone, coda=src.coda,
                )
                nfa._add_transition(src, tin, dst)

        # ── Layer F: escape ──────────────────────────────────────────────
        # F1: same-tone-twice → escape.
        for src in toned_states:
            cur_input = name_to_input[src.tone]
            esc = NfaState(
                cons=src.cons, vowels=src.vowels, mods=src.mods,
                tone="none", coda=src.coda, is_escape=True,
            )
            nfa._add_transition(src, cur_input, esc)

        # F2: same-modifier-twice → escape.
        # State (cons, vowel=modified) + INSERT_LITERAL_<base_char> → escape.
        modified_vowel_states = [
            s for s in list(nfa._states)
            if len(s.vowels) == 1 and s.mods != ("none",) and s.tone == "none"
            and s.coda == ""
        ]
        for src in modified_vowel_states:
            modified_v = src.vowels[0]
            base = _UNMODIFIED.get(modified_v)
            if base is None:
                continue
            esc = NfaState(
                cons=src.cons, vowels=(base,), mods=("none",),
                tone="none", is_escape=True,
            )
            # Trigger key — for circumflex 'â', third 'a' triggers escape.
            # For horn 'ư', third 'u' or 'w' triggers escape.
            # We encode INSERT_LITERAL_<base> as the trigger.
            nfa._add_transition(src, f"INSERT_LITERAL_{base}", esc)

        # ── Layer G: coda (final consonant) ──────────────────────────────
        # From any single-vowel state (toned or not, modified or not), on
        # INSERT_LITERAL_<coda_first_char> → state with coda set.
        all_vowel_states_for_coda = [
            s for s in list(nfa._states)
            if len(s.vowels) == 1 and s.coda == "" and not s.is_start
            and not s.is_escape
        ]
        for src in all_vowel_states_for_coda:
            for coda in all_codas:
                dst = NfaState(
                    cons=src.cons, vowels=src.vowels, mods=src.mods,
                    tone=src.tone, coda=coda,
                )
                # Use first char of coda as trigger (multi-char codas like
                # 'ng', 'ch', 'nh' also start with their first char).
                nfa._add_transition(src, f"INSERT_LITERAL_{coda[0]}", dst)

        # ── Layer H: 2-vowel sequences (diphthong) ───────────────────────
        # Allow vowel after vowel, including 'uo' for horn-modifier path.
        # CONFLICT AVOIDANCE: skip pairs where keymap has a double-sequence
        # (e.g., 'aa', 'oo', 'ee' → modifier per Layer C). Otherwise NFA
        # encodes BOTH modifier-target and 2-vowel literal for same input,
        # producing non-deterministic action emission after subset
        # construction.
        double_seq_pairs = {seq.lower() for seq in keymap.double_sequences.keys()}
        single_vowel_for_extend = [
            s for s in list(nfa._states)
            if len(s.vowels) == 1 and s.tone == "none" and s.coda == ""
            and not s.is_escape and not s.is_start
        ]
        two_vowel_state_map: dict[tuple[str, str, str], NfaState] = {}
        for src in single_vowel_for_extend:
            base_first = _UNMODIFIED.get(src.vowels[0], src.vowels[0])
            for v2 in all_vowels_single:
                # Skip if (base_first + v2) is a double-sequence — Layer C
                # already wired the modifier transition.
                if (base_first + v2) in double_seq_pairs:
                    continue
                dst = NfaState(
                    cons=src.cons,
                    vowels=src.vowels + (v2,),
                    mods=src.mods + ("none",),
                )
                nfa._add_transition(src, f"INSERT_LITERAL_{v2}", dst)
                two_vowel_state_map[(src.cons, src.vowels[0], v2)] = dst

        # ── Layer I: uo + MOD_HORN (default vs edge case) ────────────────
        # From state (cons, vowels=("u","o"), mods=("none","none")) +
        # MOD_HORN → state (cons, vowels=("ư","ơ") or ("u","ơ"), mods=...)
        for (cons, v1, v2), src in two_vowel_state_map.items():
            if (v1, v2) != ("u", "o"):
                continue
            is_edge = cons in phono.uo_edge_initials
            if is_edge:
                # uơ — only o gets horn
                dst = NfaState(
                    cons=cons,
                    vowels=("u", "ơ"),
                    mods=("none", "horn"),
                )
            else:
                # ươ — both u and o horned
                dst = NfaState(
                    cons=cons,
                    vowels=("ư", "ơ"),
                    mods=("horn", "horn"),
                )
            nfa._add_transition(src, "MOD_HORN", dst)
            # Also via raw 'w' if that maps to MOD_HORN
            for raw, abstract in keymap.mappings.items():
                if abstract == "MOD_HORN":
                    nfa._add_transition(src, "MOD_HORN", dst)
                    break

        return nfa
