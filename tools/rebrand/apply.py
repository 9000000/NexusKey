#!/usr/bin/env python3
"""Apply rebrand_plan.json per category with smoke + git commit per category."""
from __future__ import annotations
import argparse
import json
import re
import sys
from collections import defaultdict
from pathlib import Path
from typing import Dict, List

sys.path.insert(0, str(Path(__file__).resolve().parent.parent.parent))

from tools.rebrand.lib.git_ops import (
    stage, commit, move, restore_worktree, is_clean,
)
from tools.rebrand.lib.smoke import run_smoke


COMMIT_ORDER: List[tuple[str, str, str]] = [
    ("BRAND_STRING",
     "BRAND_STRING",
     "rebrand: docs and user-facing strings (NexusKey -> VKey)"),
    ("RUNTIME_IPC",
     "RUNTIME_IPC",
     "rebrand: IPC identifiers (mutex, shared mem, window class)"),
    ("PERSISTENT_ID",
     "PERSISTENT_ID",
     "rebrand: TSF CLSID + Profile GUID + registry paths"),
    ("BINARY_NAME_AND_FILENAME",
     "BINARY_NAME|FILENAME",
     "rebrand: binary names + file renames"),
]


def _load_plan(root: Path) -> dict:
    return json.loads(
        (root / "tools" / "rebrand" / "out" / "rebrand_plan.json")
        .read_text(encoding="utf-8")
    )


def _verify_plan_fresh(root: Path, edits: list[dict]) -> list[str]:
    drift: list[str] = []
    per_file = defaultdict(list)
    for e in edits:
        per_file[e["file"]].append(e)
    for f, items in per_file.items():
        try:
            lines = (root / f).read_text(encoding="utf-8").splitlines()
        except FileNotFoundError:
            drift.append(f"{f}: missing")
            continue
        for e in items:
            if e["line"] - 1 >= len(lines) or lines[e["line"] - 1] != e["before"]:
                actual = lines[e["line"] - 1] if e["line"] - 1 < len(lines) else "<EOF>"
                drift.append(f"{f}:{e['line']} expected {e['before']!r} got {actual!r}")
    return drift


def _apply_edits(root: Path, edits: list[dict]) -> set[Path]:
    touched: set[Path] = set()
    per_file = defaultdict(list)
    for e in edits:
        per_file[e["file"]].append(e)
    for f, items in per_file.items():
        path = root / f
        lines = path.read_text(encoding="utf-8").splitlines()
        # Apply in reverse order per line so col offsets stay valid when
        # multiple matches share a line
        items.sort(key=lambda x: (x["line"], -x["col"]))
        for e in items:
            i = e["line"] - 1
            line = lines[i]
            lines[i] = line[:e["col"]] + e["replacement"] + line[e["col"] + len(e["match"]):]
        ending = "\n" if path.read_text(encoding="utf-8").endswith("\n") else ""
        path.write_text("\n".join(lines) + ending, encoding="utf-8")
        touched.add(Path(f))
    return touched


def _apply_renames(root: Path, renames: list[dict]) -> set[Path]:
    """Move files via `git mv`. Returns only NEW paths — git mv already
    stages both old (removed) and new (added) in the index, so the caller
    should `git add` only the new path (in case it has further content edits)."""
    touched: set[Path] = set()
    for r in renames:
        src = Path(r["from"]).as_posix().rstrip("/")
        dst = Path(r["to"]).as_posix().rstrip("/")
        move(root, Path(src), Path(dst))
        touched.add(Path(dst))
    return touched


def _rollback(root: Path, touched: set[Path], renames: list[dict]) -> None:
    """Undo a category apply: reverse renames first, then restore edited files.

    Order matters: `git mv` updated the index, so a plain `git restore` on the
    new path won't undo the rename. We reverse the moves with `git mv dst src`,
    then `git restore --staged --worktree` for the touched edit files.
    """
    # Reverse renames (latest first, in case of cascading dir renames)
    for r in reversed(renames):
        src = Path(r["from"]).as_posix().rstrip("/")
        dst = Path(r["to"]).as_posix().rstrip("/")
        try:
            move(root, Path(dst), Path(src))
        except Exception as exc:
            print(f"[rollback] failed to reverse rename {dst} -> {src}: {exc}",
                  file=sys.stderr)
    # Restore worktree for everything else
    restore_worktree(root, sorted(touched))


def _apply_category(root: Path, plan: dict, label: str, regex: str, msg: str,
                    skip_smoke: bool) -> None:
    matcher = re.compile(f"^({regex})$")
    edits = [e for e in plan["edits"] if matcher.match(e["category"])]
    renames = plan["renames"] if "FILENAME" in regex else []

    if not edits and not renames:
        print(f"[{label}] no edits, skipping")
        return

    drift = _verify_plan_fresh(root, edits)
    if drift:
        print(f"FAIL [{label}]: plan stale, re-scan needed", file=sys.stderr)
        for d in drift[:20]:
            print(f"  {d}", file=sys.stderr)
        if len(drift) > 20:
            print(f"  ... and {len(drift) - 20} more", file=sys.stderr)
        sys.exit(1)

    touched: set[Path] = set()
    if edits:
        touched |= _apply_edits(root, edits)
    if renames:
        # Translate any edited path that is about to be renamed to its new
        # location BEFORE the move, so `git add` after `git mv` references
        # the new path (the old one no longer exists on disk).
        rename_map = {Path(r["from"]).as_posix().rstrip("/"):
                      Path(r["to"]).as_posix().rstrip("/") for r in renames}
        translated: set[Path] = set()
        for p in touched:
            s = p.as_posix()
            translated.add(p)
            for old, new in rename_map.items():
                if s == old or s.startswith(old + "/"):
                    translated.discard(p)
                    translated.add(Path(s.replace(old, new, 1)))
                    break
        touched = translated
        touched |= _apply_renames(root, renames)

    stage(root, sorted(touched))

    if not skip_smoke:
        try:
            run_smoke(root)
        except Exception as exc:
            print(f"[{label}] SMOKE FAILED: {exc}", file=sys.stderr)
            _rollback(root, touched, renames)
            sys.exit(2)

    commit(root, msg)
    print(f"[{label}] committed ({len(edits)} edits, {len(renames)} renames)")


def main():
    p = argparse.ArgumentParser()
    p.add_argument("--root", default=".", help="Repo root")
    p.add_argument("--category", required=True,
                   choices=[c[0] for c in COMMIT_ORDER] + ["ALL"])
    p.add_argument("--skip-smoke", action="store_true")
    args = p.parse_args()

    root = Path(args.root).resolve()

    if not is_clean(root):
        print("FAIL: working tree must be clean before apply", file=sys.stderr)
        sys.exit(1)

    plan = _load_plan(root)
    for k in ("new_clsid_text_service", "new_profile_guid"):
        v = plan.get("guids", {}).get(k, "")
        if not v or v == "TBD-uuidgen":
            print(f"FAIL: GUID '{k}' not filled in plan", file=sys.stderr)
            sys.exit(1)

    selected = COMMIT_ORDER if args.category == "ALL" else \
               [c for c in COMMIT_ORDER if c[0] == args.category]
    for label, regex, msg in selected:
        _apply_category(root, plan, label, regex, msg, args.skip_smoke)


if __name__ == "__main__":
    main()
