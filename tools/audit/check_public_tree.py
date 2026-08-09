#!/usr/bin/env python3
"""Reject files that belong in the private internal/ submodule.

With no arguments, audit tracked plus visible untracked files in the current
public working tree. Pass paths explicitly to classify a proposed file before
creating or staging it:

    python3 tools/audit/check_public_tree.py docs/new-user-guide.md
    python3 tools/audit/check_public_tree.py docs/plans/new-design.md

The second command fails and names the internal destination. This is a placement
gate, not a secrecy scanner: credentials must never be committed to either repo.
"""

from __future__ import annotations

import subprocess
import sys
from pathlib import Path, PurePosixPath

ROOT = Path(__file__).resolve().parents[2]
ALLOWLIST = ROOT / "tools" / "audit" / "public_tools_allowlist.txt"

PRIVATE_PREFIXES = {
    ".planning/": "internal/.planning/",
    "docs/Architecture/": "internal/docs/Architecture/",
    "docs/CODING_RULES/": "internal/docs/CODING_RULES/",
    "docs/archive/": "internal/docs/archive/",
    "docs/baselines/": "internal/docs/baselines/",
    "docs/plans/": "internal/docs/plans/",
    "docs/superpowers/": "internal/docs/superpowers/",
    "rules/": "internal/rules/",
}

PRIVATE_FILES = {
    "CLAUDE.md": "internal/CLAUDE.md",
    "HANDOFF.md": "internal/HANDOFF.md",
    "PROJECT_MAP.md": "internal/PROJECT_MAP.md",
    "REFACTOR_STATUS.md": "internal/docs/REFACTOR_STATUS.md",
    "TODO.md": "internal/docs/TODO.md",
    "tools/update_engine.py": "internal/tools/update_engine.py",
}

SECRET_NAMES = {".env", ".env.local", "id_rsa", "id_ed25519"}
SECRET_SUFFIXES = {".key", ".p12", ".pem", ".pfx"}


def normalize(raw: str) -> str:
    candidate = Path(raw)
    absolute = candidate.resolve() if candidate.is_absolute() else (ROOT / candidate).resolve()
    try:
        relative = absolute.relative_to(ROOT.resolve())
    except ValueError:
        return "../outside-public-tree"
    return PurePosixPath(relative.as_posix()).as_posix()


def read_tool_allowlist() -> list[str]:
    entries: list[str] = []
    for line in ALLOWLIST.read_text(encoding="utf-8").splitlines():
        line = line.strip()
        if not line or line.startswith("#"):
            continue
        path, separator, reason = line.partition("\t")
        if not separator or not reason.strip():
            raise ValueError(f"malformed public tool allowlist entry: {line!r}")
        entries.append(path.strip())
    return entries


def tool_is_allowed(path: str, entries: list[str]) -> bool:
    return any(
        path.startswith(entry) if entry.endswith("/") else path == entry
        for entry in entries
    )


def classify(path: str, tool_entries: list[str]) -> tuple[str, str] | None:
    if path == "internal" or path.startswith("internal/"):
        return None
    if path == "../outside-public-tree" or path.startswith("../"):
        return "outside the public repository", "classify it in its owning repository"
    if path in PRIVATE_FILES:
        return "maintainer-only file", PRIVATE_FILES[path]
    for prefix, destination in PRIVATE_PREFIXES.items():
        if path.startswith(prefix):
            return "private planning/development material", destination + path[len(prefix):]

    name = PurePosixPath(path).name.lower()
    suffix = PurePosixPath(path).suffix.lower()
    if name in SECRET_NAMES or suffix in SECRET_SUFFIXES:
        return "credential/key material", "do not commit credentials to either repository"
    if path.startswith("tools/") and not tool_is_allowed(path, tool_entries):
        return (
            "unreviewed public tool",
            "internal/tools/ (or add a reviewed public purpose to tools/audit/public_tools_allowlist.txt)",
        )
    return None


def working_tree_paths() -> list[str]:
    result = subprocess.run(
        ["git", "ls-files", "-z", "--cached", "--others", "--exclude-standard"],
        cwd=ROOT,
        capture_output=True,
        check=True,
    )
    paths = result.stdout.decode("utf-8", errors="surrogateescape").split("\0")
    return sorted(
        path for path in paths
        if path and (ROOT / path).exists()
    )


def main() -> int:
    try:
        tool_entries = read_tool_allowlist()
    except (OSError, ValueError) as error:
        print(f"public-tree audit configuration error: {error}", file=sys.stderr)
        return 2

    paths = [normalize(path) for path in sys.argv[1:]] if len(sys.argv) > 1 else working_tree_paths()
    violations: list[tuple[str, str, str]] = []
    for path in paths:
        verdict = classify(path, tool_entries)
        if verdict:
            violations.append((path, verdict[0], verdict[1]))

    if violations:
        print("FAIL: public/private file placement:", file=sys.stderr)
        for path, reason, destination in violations:
            print(f"  {path}: {reason}", file=sys.stderr)
            print(f"    -> {destination}", file=sys.stderr)
        print(
            "\nClassify every new file before creation. Main is for user/build/CI "
            "material; maintainer and agent working material belongs in internal/.",
            file=sys.stderr,
        )
        return 1

    print(f"public-tree placement: ok ({len(paths)} path(s) checked)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
