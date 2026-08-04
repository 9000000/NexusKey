#!/usr/bin/env python3
"""Point this repository at a different published engine release.

Usage:
  update_engine.py engine-v1.1.0
  update_engine.py engine-v1.1.0 --token "$VKEY_ENGINE_TOKEN"

Run this after cutting an engine release in VKey-rs. It downloads that release's
`engine.lock`, writes it and the tag into `extern/vkey_engine/`, then proves the
pair works by fetching the library through the normal path.

Why a script: the lock and the tag are two files that must agree, and nothing at
build time can tell you they do not — `fetch_engine.py` verifies the download
against whichever lock is committed, so a stale lock beside a new tag fails with
a hash mismatch that reads like a corrupt download. Updating both together, and
proving them against the real release before committing, is the whole job.
"""

from __future__ import annotations

import argparse
import json
import os
import shutil
import subprocess
import sys
import tempfile
import urllib.error
import urllib.request
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
ENGINE_DIR = ROOT / "extern" / "vkey_engine"
REPO = "phatMT97/VKey-rs"


def api(url: str, token: str, accept: str) -> urllib.request.addinfourl:
    request = urllib.request.Request(url)
    request.add_header("Accept", accept)
    request.add_header("Authorization", f"Bearer {token}")
    return urllib.request.urlopen(request, timeout=60)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("tag", help="engine release tag, e.g. engine-v1.1.0")
    parser.add_argument(
        "--token",
        default=os.environ.get("VKEY_ENGINE_TOKEN", ""),
        help="defaults to $VKEY_ENGINE_TOKEN",
    )
    args = parser.parse_args()

    if not args.token:
        print(
            "no token. The engine release repository is private:\n"
            "  export VKEY_ENGINE_TOKEN=...   (or pass --token)",
            file=sys.stderr,
        )
        return 1

    try:
        with api(
            f"https://api.github.com/repos/{REPO}/releases/tags/{args.tag}",
            args.token,
            "application/vnd.github+json",
        ) as response:
            release = json.load(response)
    except urllib.error.HTTPError as error:
        hint = " — wrong tag, or the token cannot read that repository" if error.code == 404 else ""
        print(f"cannot read release {args.tag}: HTTP {error.code}{hint}", file=sys.stderr)
        return 1
    except urllib.error.URLError as error:
        print(f"cannot reach GitHub: {error}", file=sys.stderr)
        return 1

    if release.get("draft"):
        print(
            f"{args.tag} is still a draft. Draft assets are not downloadable by tag,\n"
            f"so publish it before pointing this repository at it.",
            file=sys.stderr,
        )
        return 1

    assets = {a["name"]: a["id"] for a in release.get("assets", [])}
    if "engine.lock" not in assets:
        print(
            f"{args.tag} has no engine.lock asset; it has: {', '.join(sorted(assets)) or '(none)'}",
            file=sys.stderr,
        )
        return 1

    with api(
        f"https://api.github.com/repos/{REPO}/releases/assets/{assets['engine.lock']}",
        args.token,
        "application/octet-stream",
    ) as response:
        lock_bytes = response.read()

    previous = (ENGINE_DIR / "engine.lock").read_bytes()
    (ENGINE_DIR / "engine.lock").write_bytes(lock_bytes)
    (ENGINE_DIR / "engine.release").write_text(args.tag + "\n", encoding="utf-8")

    # Prove the pair before leaving it committed. A lock that does not describe
    # the library in its own release would otherwise only fail in CI.
    staging = Path(tempfile.mkdtemp(prefix="vkey-engine-check-"))
    try:
        result = subprocess.run(
            [
                sys.executable,
                str(ROOT / "tools" / "fetch_engine.py"),
                "--lock", str(ENGINE_DIR / "engine.lock"),
                "--dest", str(staging),
                "--repo", REPO,
                "--tag", args.tag,
                "--token", args.token,
            ],
            cwd=ROOT,
        )
        if result.returncode != 0:
            (ENGINE_DIR / "engine.lock").write_bytes(previous)
            print(
                "\nthe release's own lock does not verify against its library.\n"
                "engine.lock has been restored; nothing was changed.",
                file=sys.stderr,
            )
            return 1
    finally:
        shutil.rmtree(staging, ignore_errors=True)

    lock = dict(
        line.split("=", 1)
        for line in lock_bytes.decode().splitlines()
        if "=" in line
    )
    print(
        f"\nupdated to {args.tag}\n"
        f"  abi_version {lock.get('abi_version')}\n"
        f"  byte_len    {lock.get('byte_len')}\n"
        f"  sha256      {lock.get('sha256')}\n\n"
        f"Commit extern/vkey_engine/engine.lock and engine.release together.\n"
        f"Delete build-engine/ so the next build fetches the new library."
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
