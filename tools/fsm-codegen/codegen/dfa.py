"""NFA → DFA via subset construction (Task 4); Hopcroft minimization (Task 5).

Reference: Aho/Sethi/Ullman, "Compilers: Principles, Techniques, and Tools",
§3.7 — subset construction; §3.9 — DFA minimization.
"""
from __future__ import annotations

from dataclasses import dataclass, field
from typing import Iterable

from .nfa import Nfa, NfaState


@dataclass
class DfaState:
    state_id: int
    is_accept: bool = False
    # NFA states this DFA state subsumes — for debugging + Hopcroft minimize.
    nfa_states: frozenset[NfaState] = field(default_factory=frozenset)


@dataclass
class Dfa:
    states: list[DfaState] = field(default_factory=list)
    start: DfaState | None = None
    # transitions[src_state_id][input_symbol] → dst_state_id (single int).
    transitions: dict[int, dict[str, int]] = field(default_factory=dict)
    # action_id per accept state — populated during overlay (Task 6).
    actions: dict[int, str] = field(default_factory=dict)

    # ── public API ────────────────────────────────────────────────────────

    def accepts(self, inputs: Iterable[str]) -> bool:
        if self.start is None:
            return False
        cur_id: int = self.start.state_id
        # state_id → DfaState lookup
        state_by_id: dict[int, DfaState] = {s.state_id: s for s in self.states}
        for sym in inputs:
            edges = self.transitions.get(cur_id, {})
            if sym not in edges:
                return False
            cur_id = edges[sym]
        return state_by_id[cur_id].is_accept

    # ── builder: subset construction ──────────────────────────────────────

    @classmethod
    def from_nfa(cls, nfa: Nfa) -> "Dfa":
        """Subset construction: NFA → equivalent DFA.

        Each DFA state corresponds to a SET of NFA states (frozenset).
        The DFA is deterministic by construction: each (subset, input) pair
        yields exactly one resultant subset.
        """
        dfa = cls()

        # Map: frozenset[NfaState] → DfaState (with int state_id).
        subset_to_dfa: dict[frozenset[NfaState], DfaState] = {}
        next_id = 0

        nfa_accepts = nfa.accept_states()

        def _make_dfa_state(subset: frozenset[NfaState]) -> DfaState:
            nonlocal next_id
            if subset in subset_to_dfa:
                return subset_to_dfa[subset]
            is_accept = bool(subset & nfa_accepts)
            ds = DfaState(state_id=next_id, is_accept=is_accept, nfa_states=subset)
            subset_to_dfa[subset] = ds
            dfa.states.append(ds)
            next_id += 1
            return ds

        # Start subset = {NFA start}. (No epsilon transitions in our NFA;
        # ε-closure is identity.)
        start_subset = frozenset({nfa.start_state()})
        start_dfa = _make_dfa_state(start_subset)
        dfa.start = start_dfa

        # Pre-compute outgoing alphabet per NFA state — turns O(|edges|)
        # scan-per-subset into O(1) lookup. Critical for full Vietnamese
        # rules (10K+ NFA states, ~50K edges).
        alphabets_by_src: dict[NfaState, set[str]] = {}
        for (src, sym) in nfa._outgoing.keys():  # noqa: SLF001
            alphabets_by_src.setdefault(src, set()).add(sym)

        worklist: list[frozenset[NfaState]] = [start_subset]
        while worklist:
            cur_subset = worklist.pop()
            cur_dfa = subset_to_dfa[cur_subset]

            # Collect alphabet = ∪ alphabets_by_src(s) for s in subset.
            alphabet: set[str] = set()
            for s in cur_subset:
                alphabet |= alphabets_by_src.get(s, set())

            for sym in alphabet:
                # move(subset, sym) = ∪ outgoing(s, sym) for s in subset
                move: set[NfaState] = set()
                for s in cur_subset:
                    move |= nfa._outgoing.get((s, sym), set())  # noqa: SLF001
                if not move:
                    continue
                move_frozen = frozenset(move)
                if move_frozen not in subset_to_dfa:
                    _make_dfa_state(move_frozen)
                    worklist.append(move_frozen)
                dst_dfa = subset_to_dfa[move_frozen]
                dfa.transitions.setdefault(cur_dfa.state_id, {})[sym] = (
                    dst_dfa.state_id
                )

        return dfa

    # ── Hopcroft minimization (Task 5) ────────────────────────────────────

    def minimize(self) -> "Dfa":
        """Hopcroft minimization — collapse equivalent states.

        Reference: Hopcroft (1971), "An n log n algorithm for minimizing
        states in a finite automaton". Pseudocode follows Wikipedia
        "DFA minimization" §Hopcroft's algorithm.

        Returns a new Dfa where each state corresponds to an equivalence
        class. Language is preserved: this.accepts(x) ⇔ minimized.accepts(x).
        """
        if not self.states or self.start is None:
            return Dfa()

        all_ids: set[int] = {s.state_id for s in self.states}
        accepts: set[int] = {s.state_id for s in self.states if s.is_accept}
        non_accepts: set[int] = all_ids - accepts

        # ── Build predecessors index: predecessors[c][dst_id] = set(src_id) ──
        predecessors: dict[str, dict[int, set[int]]] = {}
        alphabet: set[str] = set()
        for src_id, edges in self.transitions.items():
            for sym, dst_id in edges.items():
                alphabet.add(sym)
                predecessors.setdefault(sym, {}).setdefault(dst_id, set()).add(src_id)

        # ── Initial partition P = { F, Q\F } (drop empty parts) ──
        partition: list[frozenset[int]] = []
        if accepts:
            partition.append(frozenset(accepts))
        if non_accepts:
            partition.append(frozenset(non_accepts))

        # Worklist W initialized with BOTH halves of initial partition
        # (Hopcroft 1971 Wikipedia spec — the "smaller half" optimization
        # only applies to refinement steps, not init).
        worklist: list[frozenset[int]] = list(partition)

        while worklist:
            A = worklist.pop()
            for c in alphabet:
                # X = pre-image of A on c: { src : δ(src, c) ∈ A }
                preds_for_c = predecessors.get(c, {})
                X: set[int] = set()
                for dst_id in A:
                    X |= preds_for_c.get(dst_id, set())
                if not X:
                    continue
                X_frozen = frozenset(X)

                # For each Y in P split by X.
                new_partition: list[frozenset[int]] = []
                for Y in partition:
                    intersect = Y & X_frozen
                    diff = Y - X_frozen
                    if intersect and diff:
                        new_partition.append(intersect)
                        new_partition.append(diff)
                        # Worklist update.
                        if Y in worklist:
                            # Replace Y with both halves.
                            worklist.remove(Y)
                            worklist.append(intersect)
                            worklist.append(diff)
                        else:
                            # Add smaller half.
                            if len(intersect) <= len(diff):
                                worklist.append(intersect)
                            else:
                                worklist.append(diff)
                    else:
                        new_partition.append(Y)
                partition = new_partition

        # ── Build minimized DFA from partition ────────────────────────────
        # state_id → block index
        block_of: dict[int, int] = {}
        for idx, block in enumerate(partition):
            for sid in block:
                block_of[sid] = idx

        # Index source states by ID (was O(n) lookup per block — 50K² ops).
        src_by_id: dict[int, DfaState] = {s.state_id: s for s in self.states}

        new = Dfa()
        block_to_new: dict[int, DfaState] = {}
        for idx, block in enumerate(partition):
            sample_id = next(iter(block))
            sample_state = src_by_id[sample_id]
            ds = DfaState(
                state_id=idx,
                is_accept=sample_state.is_accept,
                nfa_states=frozenset().union(
                    *[src_by_id[sid].nfa_states for sid in block]
                ),
            )
            new.states.append(ds)
            block_to_new[idx] = ds

        start_block = block_of[self.start.state_id]
        new.start = block_to_new[start_block]

        for idx, block in enumerate(partition):
            sample_id = next(iter(block))
            sample_edges = self.transitions.get(sample_id, {})
            if not sample_edges:
                continue
            new_edges: dict[str, int] = {}
            for sym, dst_id in sample_edges.items():
                new_edges[sym] = block_of[dst_id]
            new.transitions[idx] = new_edges

        return new
