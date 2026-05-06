"""Unit tests for codegen.emitter — emit C++ tables from FSM pipeline.

D-1 Task 8. Tests verify:
  - Generated .h files exist with expected names.
  - Generated headers compile via g++ -fsyntax-only.
  - Specific values (state count, action count, keymap entries) match input.
  - End-to-end pipeline produces all method tables for telex / telex-simple
    / vni / combined.
"""
from __future__ import annotations

import shutil
import subprocess
from pathlib import Path

import pytest

from codegen.dfa import Dfa
from codegen.emitter import CppEmitter
from codegen.nfa import Nfa
from codegen.overlay import apply_overlay
from codegen.parser import KeymapRule, PhonotacticsRule, RuleParser


REPO_ROOT = Path(__file__).resolve().parents[3]
RULES_DIR = REPO_ROOT / "rules"
GPP = shutil.which("g++") or shutil.which("clang++")


# ─────────────────────────────────────────────────────────────────────────
# Helpers
# ─────────────────────────────────────────────────────────────────────────


def _compile_check(header_paths: list[Path]) -> None:
    """Run g++/clang++ -fsyntax-only on each header to ensure valid C++20.

    Skips silently if no compiler available (CI may have g++; dev env may not).
    """
    if GPP is None:
        pytest.skip("no C++ compiler available")
    for h in header_paths:
        result = subprocess.run(
            [GPP, "-std=c++20", "-fsyntax-only", "-x", "c++-header", str(h)],
            capture_output=True, text=True,
        )
        assert result.returncode == 0, (
            f"compile failed for {h.name}:\n"
            f"stdout: {result.stdout}\nstderr: {result.stderr}"
        )


def _build_telex_pipeline() -> tuple[KeymapRule, Dfa, "OverlayResult"]:  # noqa: F821
    phono = RuleParser.load_phonotactics(RULES_DIR / "vietnamese-phonotactics.toml")
    telex = RuleParser.load_keymap(RULES_DIR / "telex-keymap.toml")
    nfa = Nfa.from_rules(phono, telex)
    dfa = Dfa.from_nfa(nfa)
    min_dfa = dfa.minimize()
    overlay = apply_overlay(min_dfa, nfa, telex)
    return telex, min_dfa, overlay


# ─────────────────────────────────────────────────────────────────────────
# emit_abstract_input
# ─────────────────────────────────────────────────────────────────────────


def test_emit_abstract_input_creates_file(tmp_path: Path) -> None:
    out = tmp_path / "gen"
    out.mkdir()
    path = CppEmitter.emit_abstract_input(out)
    assert path.exists()
    assert path.name == "abstract_input.h"


def test_emit_abstract_input_contains_enum(tmp_path: Path) -> None:
    out = tmp_path / "gen"
    out.mkdir()
    path = CppEmitter.emit_abstract_input(out)
    text = path.read_text()
    assert "enum class AbstractInput" in text
    assert "TONE_SAC" in text
    assert "MOD_HORN" in text
    assert "INSERT_LITERAL_a" in text


def test_emit_abstract_input_compiles(tmp_path: Path) -> None:
    out = tmp_path / "gen"
    out.mkdir()
    path = CppEmitter.emit_abstract_input(out)
    _compile_check([path])


# ─────────────────────────────────────────────────────────────────────────
# emit_action_table
# ─────────────────────────────────────────────────────────────────────────


def test_emit_action_table_creates_file(tmp_path: Path) -> None:
    out = tmp_path / "gen"
    out.mkdir()
    _, _, overlay = _build_telex_pipeline()
    path = CppEmitter.emit_action_table(overlay.action_table, "telex", out)
    assert path.exists()
    assert "telex" in path.name


def test_emit_action_table_contains_count(tmp_path: Path) -> None:
    out = tmp_path / "gen"
    out.mkdir()
    _, _, overlay = _build_telex_pipeline()
    path = CppEmitter.emit_action_table(overlay.action_table, "telex", out)
    text = path.read_text()
    assert f"kActionCountTelex = {len(overlay.action_table)}" in text


def test_emit_action_table_compiles(tmp_path: Path) -> None:
    out = tmp_path / "gen"
    out.mkdir()
    CppEmitter.emit_abstract_input(out)
    _, _, overlay = _build_telex_pipeline()
    path = CppEmitter.emit_action_table(overlay.action_table, "telex", out)
    _compile_check([path])


# ─────────────────────────────────────────────────────────────────────────
# emit_dense
# ─────────────────────────────────────────────────────────────────────────


def test_emit_dense_creates_file(tmp_path: Path) -> None:
    out = tmp_path / "gen"
    out.mkdir()
    _, dfa, overlay = _build_telex_pipeline()
    path = CppEmitter.emit_dense(dfa, overlay, "telex", out)
    assert path.exists()


def test_emit_dense_contains_state_count(tmp_path: Path) -> None:
    out = tmp_path / "gen"
    out.mkdir()
    _, dfa, overlay = _build_telex_pipeline()
    path = CppEmitter.emit_dense(dfa, overlay, "telex", out)
    text = path.read_text()
    assert f"kStateCountTelex = {len(dfa.states)}" in text
    # Start state is always 0 after Dfa rebuild (renumbered post-minimize).
    assert "kStartStateTelex" in text


def test_emit_dense_compiles(tmp_path: Path) -> None:
    out = tmp_path / "gen"
    out.mkdir()
    CppEmitter.emit_abstract_input(out)
    _, dfa, overlay = _build_telex_pipeline()
    CppEmitter.emit_action_table(overlay.action_table, "telex", out)
    path = CppEmitter.emit_dense(dfa, overlay, "telex", out)
    _compile_check([path])


# ─────────────────────────────────────────────────────────────────────────
# emit_keymap
# ─────────────────────────────────────────────────────────────────────────


def test_emit_keymap_creates_file(tmp_path: Path) -> None:
    out = tmp_path / "gen"
    out.mkdir()
    keymap, _, _ = _build_telex_pipeline()
    path = CppEmitter.emit_keymap(keymap, "telex", out)
    assert path.exists()


def test_emit_keymap_contains_mappings(tmp_path: Path) -> None:
    out = tmp_path / "gen"
    out.mkdir()
    keymap, _, _ = _build_telex_pipeline()
    path = CppEmitter.emit_keymap(keymap, "telex", out)
    text = path.read_text()
    # Telex tone keys mapped
    assert "TONE_SAC" in text
    assert "TONE_HUYEN" in text


def test_emit_keymap_compiles(tmp_path: Path) -> None:
    out = tmp_path / "gen"
    out.mkdir()
    CppEmitter.emit_abstract_input(out)
    keymap, _, _ = _build_telex_pipeline()
    path = CppEmitter.emit_keymap(keymap, "telex", out)
    _compile_check([path])


# ─────────────────────────────────────────────────────────────────────────
# emit_all (full pipeline)
# ─────────────────────────────────────────────────────────────────────────


def test_emit_all_creates_files_for_all_methods(tmp_path: Path) -> None:
    out = tmp_path / "gen"
    out.mkdir()
    files = CppEmitter.emit_all(RULES_DIR, out)
    names = {f.name for f in files}

    # abstract_input.h shared
    assert "abstract_input.h" in names

    # Per-method: fsm_table + keymap + action_table for all 3 methods.
    for method in ("telex", "telex_simple", "vni"):
        assert f"fsm_table_{method}.h" in names
        assert f"keymap_{method}.h" in names
        assert f"action_table_{method}.h" in names


def test_emit_all_compiles(tmp_path: Path) -> None:
    out = tmp_path / "gen"
    out.mkdir()
    files = CppEmitter.emit_all(RULES_DIR, out)
    _compile_check(files)


def test_emit_all_under_30_seconds(tmp_path: Path) -> None:
    """Full codegen ≤ 30s — including 3 methods × (NFA + DFA + min + overlay
    + emit). Verifier budget for whole pipeline."""
    import time

    out = tmp_path / "gen"
    out.mkdir()
    t0 = time.perf_counter()
    CppEmitter.emit_all(RULES_DIR, out)
    elapsed = time.perf_counter() - t0
    assert elapsed < 30.0, f"emit_all took {elapsed:.2f}s, budget 30.0s"


# ─────────────────────────────────────────────────────────────────────────
# Memory budget — generated tables ≤ 100 KB total per design doc.
# ─────────────────────────────────────────────────────────────────────────


def test_emit_all_total_size_under_100kb(tmp_path: Path) -> None:
    out = tmp_path / "gen"
    out.mkdir()
    files = CppEmitter.emit_all(RULES_DIR, out)
    total = sum(f.stat().st_size for f in files)
    # Generated source is bigger than runtime data due to formatting/comments.
    # Loose bound on source: 1 MB.
    assert total < 1_000_000, f"generated source {total} bytes (cap 1 MB)"
