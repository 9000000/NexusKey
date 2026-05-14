"""Thin wrappers around git CLI. All paths are relative to repo root."""
from __future__ import annotations
import subprocess
from pathlib import Path
from typing import Iterable


def _run(cwd: Path, *args: str, check: bool = True) -> subprocess.CompletedProcess:
    return subprocess.run(["git", *args], cwd=cwd, check=check,
                          capture_output=True, text=True)


def is_clean(root: Path) -> bool:
    r = _run(root, "status", "--porcelain")
    return r.stdout.strip() == ""


def stage(root: Path, paths: Iterable[Path]) -> None:
    # -f: force-add tracked files even when their parent dir matches a
    # gitignore pattern. Safe because the rebrand scan already filters out
    # truly-untracked-and-gitignored files via [scope].exclude.
    args = ["add", "-f", "--"] + [str(p) for p in paths]
    _run(root, *args)


def move(root: Path, src: Path, dst: Path) -> None:
    (root / dst.parent).mkdir(parents=True, exist_ok=True)
    _run(root, "mv", str(src), str(dst))


def commit(root: Path, message: str) -> None:
    _run(root, "commit", "-m", message)


def restore_worktree(root: Path, paths: Iterable[Path]) -> None:
    args = ["restore", "--staged", "--worktree", "--"] + [str(p) for p in paths]
    _run(root, *args, check=False)
