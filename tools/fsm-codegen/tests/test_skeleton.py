"""Skeleton smoke tests — verify pkg imports + module stubs exist.

Real per-module tests in test_parser.py / test_nfa.py / test_dfa.py / test_emitter.py /
test_verifier.py — added in subsequent D-1 tasks.
"""
from __future__ import annotations


def test_codegen_pkg_imports() -> None:
    import codegen

    assert codegen.__version__ == "0.1.0"


def test_modules_loadable() -> None:
    from codegen import cli, dfa, emitter, nfa, parser, verifier

    assert hasattr(cli, "main")
    assert hasattr(parser, "RuleParser")
    assert hasattr(parser, "PhonotacticsRule")
    assert hasattr(parser, "KeymapRule")
    assert hasattr(nfa, "Nfa")
    assert hasattr(dfa, "Dfa")
    assert hasattr(verifier, "Verifier")
    assert hasattr(emitter, "CppEmitter")


def test_cli_help_runs() -> None:
    """CLI parser should not crash on --help (returns SystemExit(0))."""
    import pytest

    from codegen import cli

    with pytest.raises(SystemExit) as excinfo:
        cli.main(["--help"])
    assert excinfo.value.code == 0


def test_cli_missing_rules_dir_returns_2() -> None:
    from codegen import cli

    rc = cli.main(["--rules", "/nonexistent/path/abc", "--output", "/tmp"])
    assert rc == 2
