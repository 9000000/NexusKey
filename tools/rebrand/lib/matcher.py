"""Find replacement matches in a line, longest-key-first, non-overlapping."""
from __future__ import annotations
from dataclasses import dataclass
from typing import Dict, Iterator


@dataclass(frozen=True)
class Match:
    col: int       # 0-indexed
    match: str
    replacement: str


def find_matches(line: str, replacements: Dict[str, str]) -> Iterator[Match]:
    # Sort keys longest first for greedy matching.
    keys = sorted(replacements.keys(), key=len, reverse=True)
    i = 0
    while i < len(line):
        hit = None
        for k in keys:
            if line.startswith(k, i):
                hit = k
                break
        if hit:
            yield Match(col=i, match=hit, replacement=replacements[hit])
            i += len(hit)
        else:
            i += 1
