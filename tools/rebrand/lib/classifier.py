"""Map (path, line, match) → category 1-7 per spec rules."""
from __future__ import annotations
import re
from dataclasses import dataclass
from enum import Enum
from pathlib import Path
from typing import Dict, List


class Category(Enum):
    BRAND_STRING = "BRAND_STRING"           # 1 — commit 1
    BINARY_NAME = "BINARY_NAME"             # 2 — commit 4 (gộp với FILENAME)
    RUNTIME_IPC = "RUNTIME_IPC"             # 3 — commit 2
    PERSISTENT_ID = "PERSISTENT_ID"         # 4 — commit 3
    FILENAME = "FILENAME"                   # 5 — commit 4
    NAMESPACE_KEEP = "NAMESPACE_KEEP"       # 6 — excluded
    HISTORICAL_KEEP = "HISTORICAL_KEEP"     # 7 — excluded
    UNCLASSIFIED = "UNCLASSIFIED"           # scan fails


@dataclass
class ClassifierContext:
    # Reserved for callers that route FILENAME hits — scan.py handles file
    # renames externally via the renames[] list, so classify() does not read
    # this field. Kept for symmetry with the spec's classification rules.
    file_renames: Dict[str, str]
    namespace_keep_regex: List[str]
    brand_to_binary_regex: List[str]


_BINARY_SUFFIX_RE = re.compile(
    r"\.(exe|dll|zip|sha256|sigstore\.json|sigstore)\b"
)
_CMAKE_TARGET_RE = re.compile(
    r"\b(?:NextKey|NexusKey)[A-Z][A-Za-z]*\b"
)
_RUNTIME_IPC_HINTS = (
    "Local\\\\NexusKey", "Local\\NexusKey",
    "SharedState", "ClassicSettings", "Mutex",
)


def classify(
    rel_path: Path,
    line_no: int,
    line: str,
    match: str,
    in_guid_window: bool,
    ctx: ClassifierContext,
    was_excluded: bool,
) -> Category:
    if was_excluded:
        return Category.HISTORICAL_KEEP

    ext = rel_path.suffix.lower()
    name = rel_path.name
    is_cmake = name == "CMakeLists.txt" or ext == ".cmake"

    if ext in (".cpp", ".h", ".in"):
        for rx in ctx.namespace_keep_regex:
            if re.search(rx, line):
                return Category.NAMESPACE_KEEP
        if in_guid_window:
            return Category.PERSISTENT_ID
        for hint in _RUNTIME_IPC_HINTS:
            if hint in line:
                return Category.RUNTIME_IPC
        if _BINARY_SUFFIX_RE.search(match) or _BINARY_SUFFIX_RE.search(line):
            return Category.BINARY_NAME
        return Category.BRAND_STRING

    if ext in (".rc", ".manifest", ".json", ".yml", ".yaml", ".def", ".idl"):
        if _BINARY_SUFFIX_RE.search(match) or _BINARY_SUFFIX_RE.search(line):
            return Category.BINARY_NAME
        if _CMAKE_TARGET_RE.search(match):
            return Category.BINARY_NAME
        return Category.BRAND_STRING

    if is_cmake:
        return Category.BINARY_NAME

    if ext == ".md":
        if _BINARY_SUFFIX_RE.search(line) or _BINARY_SUFFIX_RE.search(match):
            return Category.BINARY_NAME
        for rx in ctx.brand_to_binary_regex:
            if re.search(rx, line):
                return Category.BINARY_NAME
        return Category.BRAND_STRING

    if ext in (".html", ".js", ".css", ".py", ".sh", ".toml"):
        if _BINARY_SUFFIX_RE.search(line):
            return Category.BINARY_NAME
        return Category.BRAND_STRING

    return Category.UNCLASSIFIED
