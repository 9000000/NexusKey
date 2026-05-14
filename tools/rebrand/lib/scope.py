"""Glob-based include/exclude filter for repo file walking."""
from __future__ import annotations
import re
from pathlib import Path
from typing import Iterable, List


def _expand_braces(pattern: str) -> List[str]:
    """Expand a single {a,b,c} group. No nested braces in our patterns."""
    m = re.search(r"\{([^{}]+)\}", pattern)
    if not m:
        return [pattern]
    expanded = []
    for alt in m.group(1).split(","):
        expanded.extend(_expand_braces(pattern[:m.start()] + alt + pattern[m.end():]))
    return expanded


def _glob_to_regex(pattern: str) -> re.Pattern:
    out = []
    i = 0
    while i < len(pattern):
        c = pattern[i]
        if c == "*":
            if i + 1 < len(pattern) and pattern[i + 1] == "*":
                # ** matches any depth including zero
                out.append(".*")
                i += 2
                if i < len(pattern) and pattern[i] == "/":
                    i += 1
            else:
                out.append("[^/]*")
                i += 1
        elif c == "?":
            out.append("[^/]")
            i += 1
        elif c == ".":
            out.append(r"\.")
            i += 1
        else:
            out.append(re.escape(c))
            i += 1
    return re.compile("^" + "".join(out) + "$")


def _matches_any(path_str: str, patterns: Iterable[str]) -> bool:
    for raw in patterns:
        for expanded in _expand_braces(raw):
            if _glob_to_regex(expanded).match(path_str):
                return True
    return False


def file_in_scope(rel_path: Path, include: List[str], exclude: List[str]) -> bool:
    s = str(rel_path).replace("\\", "/")
    if _matches_any(s, exclude):
        return False
    return _matches_any(s, include)


def iter_files(root: Path, include: List[str], exclude: List[str]) -> Iterable[Path]:
    for f in root.rglob("*"):
        if not f.is_file():
            continue
        rel = f.relative_to(root)
        if file_in_scope(rel, include, exclude):
            yield rel
