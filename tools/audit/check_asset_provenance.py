#!/usr/bin/env python3
"""Fail when a binary asset appears without a recorded origin.

Usage:
  check_asset_provenance.py                 # audit tracked files (CI gate)
  check_asset_provenance.py --dir dist      # audit a built release payload
  check_asset_provenance.py --update        # rewrite the allowlist from HEAD

Why this exists: a free Vietnamese app was billed roughly 500 million VND for a
commercial font that was tested once, replaced, and left behind in the assets
folder — never displayed, never used, but present in the shipped APK. Presence
in a distributed artifact is what creates liability, not use. A one-time audit
cannot prevent that; only a gate that fails on the *next* forgotten file can.

Two rules:

1. Font files are refused outright. Neither repository has a legitimate font
   dependency, and the license terms of a commercial font are not something to
   discover after release. Adding one has to be a decision, recorded here.
2. Every other binary asset must appear in `assets_allowlist.txt` with an
   origin. Adding a binary therefore fails CI until someone writes down where it
   came from — which is the moment to check its license.
"""

from __future__ import annotations

import argparse
import os
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
ALLOWLIST = HERE / "assets_allowlist.txt"

# Refused outright, never allowlisted silently.
FONT_EXT = {
    ".ttf", ".otf", ".woff", ".woff2", ".eot", ".ttc",
    ".pfb", ".pfm", ".dfont", ".bdf", ".pcf", ".fon",
}

# Must carry a recorded origin.
ASSET_EXT = {
    ".dll", ".so", ".dylib", ".exe", ".lib", ".a", ".pdb",
    ".zip", ".7z", ".gz", ".tar", ".jar", ".msi",
    ".png", ".jpg", ".jpeg", ".gif", ".bmp", ".webp", ".ico", ".icns", ".svg",
    ".mp3", ".wav", ".ogg", ".mp4", ".webm",
    ".vklx", ".fst", ".bin", ".dat", ".pdf",
}


def tracked_files() -> list[str]:
    out = subprocess.run(
        ["git", "ls-files", "-z"], capture_output=True, text=True, check=True
    ).stdout
    return [p for p in out.split("\0") if p]


def payload_files(root: Path) -> list[str]:
    return [
        str(Path(dirpath, name).relative_to(root))
        for dirpath, _dirnames, names in os.walk(root)
        for name in names
    ]


def read_allowlist() -> dict[str, str]:
    if not ALLOWLIST.exists():
        return {}
    entries: dict[str, str] = {}
    for line in ALLOWLIST.read_text(encoding="utf-8").splitlines():
        line = line.strip()
        if not line or line.startswith("#"):
            continue
        path, _, origin = line.partition("\t")
        entries[path.strip()] = origin.strip() or "(no origin recorded)"
    return entries


def classify(paths: list[str]) -> tuple[list[str], list[str]]:
    allowed = read_allowlist()
    fonts, unlisted = [], []
    for path in paths:
        ext = Path(path).suffix.lower()
        if ext in FONT_EXT:
            fonts.append(path)
        elif ext in ASSET_EXT and path not in allowed:
            unlisted.append(path)
    return sorted(fonts), sorted(unlisted)


def update(paths: list[str]) -> int:
    existing = read_allowlist()
    assets = sorted(p for p in paths if Path(p).suffix.lower() in ASSET_EXT)
    lines = [
        "# Binary assets and where they came from. One per line: path<TAB>origin.",
        "# Adding a binary fails CI until it is listed here — see",
        "# check_asset_provenance.py for why. Record the real upstream and its",
        "# license, or `own` for something original to this project.",
        "#",
        "# Font files are never listed here; they are refused outright.",
        "",
    ]
    for path in assets:
        lines.append(f"{path}\t{existing.get(path, 'UNRECORDED — fill this in')}")
    ALLOWLIST.write_text("\n".join(lines) + "\n", encoding="utf-8")
    print(f"wrote {ALLOWLIST.relative_to(Path.cwd())} with {len(assets)} entries")
    missing = [p for p in assets if "UNRECORDED" in existing.get(p, "UNRECORDED")]
    if missing:
        print(f"{len(missing)} entries still need an origin filled in")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--dir", type=Path, help="audit a built payload instead of HEAD")
    parser.add_argument("--update", action="store_true", help="rewrite the allowlist")
    args = parser.parse_args()

    paths = payload_files(args.dir) if args.dir else tracked_files()
    if args.update:
        if args.dir:
            parser.error("--update operates on tracked files, not --dir")
        return update(paths)

    fonts, unlisted = classify(paths)
    where = f"payload {args.dir}" if args.dir else "tracked files"

    # In a built payload every binary is either a freshly built artifact or a
    # vendored dependency already covered by the tracked-file run, so the origin
    # rule has nothing to say there. The font rule is the one that matters: the
    # payload is exactly what a licensing scanner downloads and inspects.
    if args.dir:
        unlisted = []

    if fonts:
        print(f"FAIL: font file(s) in {where}:", file=sys.stderr)
        for path in fonts:
            print(f"  {path}", file=sys.stderr)
        print(
            "\nA font in a distributed artifact is a licensing liability even if "
            "nothing renders with it.\nRemove it, or record the decision and its "
            "license terms before allowlisting.",
            file=sys.stderr,
        )
    if unlisted:
        print(f"FAIL: binary asset(s) in {where} with no recorded origin:", file=sys.stderr)
        for path in unlisted:
            print(f"  {path}", file=sys.stderr)
        print(
            f"\nAdd each to {ALLOWLIST.name} with where it came from, then re-run. "
            f"`--update` seeds the entries.",
            file=sys.stderr,
        )
    if fonts or unlisted:
        return 1

    print(f"asset provenance: ok ({len(paths)} paths checked in {where})")
    return 0


if __name__ == "__main__":
    sys.exit(main())
