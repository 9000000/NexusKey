"""CLI entry point for FSM codegen tool.

Usage:
    python -m codegen.cli --rules rules/ --output src/core/engine/generated/
    fsm-codegen --rules rules/ --output src/core/engine/generated/

Pipeline (D-1 Task 1 stub; full impl in subsequent tasks):
    1. Parse rules/*.toml
    2. Build NFA per method
    3. NFA → DFA via subset construction
    4. Minimize DFA
    5. Apply tone/modifier overlay
    6. Verify exhaustively against 20,504 syllables
    7. Emit C++ tables to output dir
"""
from __future__ import annotations

import argparse
import sys
from pathlib import Path


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(prog="fsm-codegen")
    parser.add_argument(
        "--rules",
        type=Path,
        required=True,
        help="Directory containing *.toml rule files (phonotactics + keymaps).",
    )
    parser.add_argument(
        "--output",
        type=Path,
        required=True,
        help="Directory to emit generated C++ headers (must exist).",
    )
    parser.add_argument(
        "--method",
        choices=["telex", "telex-simple", "vni", "combined", "all"],
        default="all",
        help="Which method table(s) to emit (default: all).",
    )
    parser.add_argument(
        "--verify",
        action="store_true",
        default=True,
        help="Run exhaustive 20,504-syllable verifier (default: on).",
    )
    args = parser.parse_args(argv)

    if not args.rules.is_dir():
        print(f"error: --rules path not a directory: {args.rules}", file=sys.stderr)
        return 2
    if not args.output.is_dir():
        print(f"error: --output path not a directory: {args.output}", file=sys.stderr)
        return 2

    # D-1 Task 1: stub. Real pipeline wired Tasks 2-8.
    print(f"fsm-codegen v0.1.0 (skeleton — D-1 Task 1 stub)")
    print(f"  rules: {args.rules.resolve()}")
    print(f"  output: {args.output.resolve()}")
    print(f"  method: {args.method}")
    print(f"  verify: {args.verify}")
    print("  (pipeline impl pending Tasks 2-8)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
