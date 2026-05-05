"""Emit C++ constexpr tables from minimized DFA.

Outputs:
  src/core/engine/generated/fsm_table_<method>.h  (FsmCell[state][input])
  src/core/engine/generated/keymap_<method>.h     (raw key → AbstractInput)
  src/core/engine/generated/abstract_input.h      (enum)

Implementation deferred to D-1 Task 8.
"""
from __future__ import annotations

from pathlib import Path

from .dfa import Dfa
from .parser import KeymapRule


# Threshold from design §3.4 — switch dense → sparse encoding above 80 KB.
DENSE_SIZE_THRESHOLD_BYTES = 80 * 1024


class CppEmitter:
    @staticmethod
    def emit_dense(dfa: Dfa, method_name: str, output_dir: Path) -> Path:
        """Emit dense 2D constexpr array fsm_table_<method>.h.

        Implementation: D-1 Task 8.
        """
        raise NotImplementedError("Task 8")

    @staticmethod
    def emit_sparse(dfa: Dfa, method_name: str, output_dir: Path) -> Path:
        """Emit sorted-transition array for binary search (size fallback).

        Implementation: D-1 Task 8.
        """
        raise NotImplementedError("Task 8")

    @staticmethod
    def emit_keymap(keymap: KeymapRule, output_dir: Path) -> Path:
        """Emit keymap_<method>.h with rawKey[256] → AbstractInput array."""
        raise NotImplementedError("Task 8")

    @staticmethod
    def emit_abstract_input(output_dir: Path) -> Path:
        """Emit abstract_input.h enum (TONE_SAC, MOD_HORN, INSERT_LITERAL_a, ...)."""
        raise NotImplementedError("Task 8")
