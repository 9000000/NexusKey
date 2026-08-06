#!/usr/bin/env python3
"""Download the prebuilt engine described by engine.lock and verify it.

Usage:
  fetch_engine.py --lock ... --dest ... --repo OWNER/NAME --tag engine-v1.0.0
  fetch_engine.py --lock ... --dest ... --base-url https://host/path
  fetch_engine.py --lock ... --dest ... --from-file /local/vkey_engine.dll

Then configure with `-DVKEY_ENGINE_ROOT=<dest>`.

Use `--repo/--tag` when the release lives in a private repository: its
`releases/download/<tag>/<asset>` URL answers 404 even to a valid bearer token,
because that path authenticates a browser session rather than a token. The asset
has to be resolved through the API and fetched by its id with an
`application/octet-stream` Accept header. `--base-url` remains correct for a
public release, where the plain download URL works.

Why this exists: `vkey_engine.dll` embeds a syllable dictionary derived from a
CC BY-NC corpus, so it is not licensed under this repository's AGPL-3.0 and does
not belong in a tree whose license tells recipients the opposite. `engine.lock`
and `include/vkey_engine.h` stay committed — they are text, carry no corpus data,
and the lock is the trust anchor this script checks against.

The lock is the authority, not the download. Byte length and SHA-256 come from
the committed lock, and a file that does not match both is deleted rather than
installed. There is no `.sha256` sidecar fetched alongside the artifact: a digest
from the same place as the file it describes proves nothing.

Scope: the lock describes the Windows x86_64 DLL only (`target=windows-x86_64`),
which is what CI builds. There is no lock entry for `libvkey_engine.so`, so this
script will not pretend to verify one — Linux developers keep using the VKey-rs
sync script, whose output is trusted because it is produced locally.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import shutil
import sys
import urllib.error
import urllib.parse
import urllib.request
from pathlib import Path

LOCK_PATTERN = re.compile(
    r"^VKEY-ENGINE-LOCK-V2\n"
    r"abi_version=(?P<abi>[0-9]+)\n"
    r"target=(?P<target>windows-x86_64)\n"
    r"asset_name=(?P<asset>vkey_engine\.dll)\n"
    r"installed_name=vkey_engine\.dll\n"
    r"byte_len=(?P<bytes>[0-9]+)\n"
    r"sha256=(?P<sha>[0-9a-f]{64})\n"
    r"counter=(?P<counter>[0-9]+)\n$"
)

MAX_LOCK_BYTES = 1024
# The plan caps the engine artifact at 8 MiB; refuse anything larger before it
# reaches the disk rather than after.
MAX_ENGINE_BYTES = 8 * 1024 * 1024
ALLOWED_HOSTS = {
    "api.github.com",
    "github.com",
    "objects.githubusercontent.com",
    "release-assets.githubusercontent.com",
}


class FetchError(Exception):
    """Raised when the engine cannot be obtained and verified."""


def read_lock(path: Path) -> dict[str, str]:
    raw = path.read_bytes()
    if len(raw) > MAX_LOCK_BYTES:
        raise FetchError(f"{path} is larger than {MAX_LOCK_BYTES} bytes")
    match = LOCK_PATTERN.match(raw.decode("utf-8"))
    if not match:
        raise FetchError(f"{path} is not a canonical VKEY-ENGINE-LOCK-V2 file")
    return match.groupdict()


def check_url(url: str) -> None:
    parsed = urllib.parse.urlsplit(url)
    if parsed.scheme != "https":
        raise FetchError(f"refusing non-HTTPS URL: {url}")
    if parsed.hostname not in ALLOWED_HOSTS:
        raise FetchError(
            f"refusing host {parsed.hostname!r}; allowed: {sorted(ALLOWED_HOSTS)}"
        )


class CheckedRedirects(urllib.request.HTTPRedirectHandler):
    """Validate every hop, not only the URL we started with."""

    def redirect_request(self, req, fp, code, msg, headers, newurl):  # noqa: D102
        check_url(newurl)
        return super().redirect_request(req, fp, code, msg, headers, newurl)


def asset_url(repo: str, tag: str, asset: str, token: str | None) -> str:
    """Resolve a release asset to its API download URL.

    A private repository's releases/download/<tag>/<asset> path returns 404 to a
    bearer token, so the asset id has to come from the API first.
    """
    url = f"https://api.github.com/repos/{repo}/releases/tags/{tag}"
    check_url(url)
    request = urllib.request.Request(url)
    request.add_header("Accept", "application/vnd.github+json")
    if token:
        request.add_header("Authorization", f"Bearer {token}")
    with urllib.request.urlopen(request, timeout=60) as response:
        release = json.load(response)
    for candidate in release.get("assets", []):
        if candidate.get("name") == asset:
            return f"https://api.github.com/repos/{repo}/releases/assets/{candidate['id']}"
    names = ", ".join(sorted(a.get("name", "?") for a in release.get("assets", [])))
    raise FetchError(f"release {tag} has no asset named {asset!r}; it has: {names}")


def download(url: str, target: Path, token: str | None, api: bool = False) -> None:
    check_url(url)
    request = urllib.request.Request(url)
    # The API returns release metadata as JSON unless the octet-stream is asked
    # for by name, in which case it redirects to the storage host.
    request.add_header("Accept", "application/octet-stream" if api else "*/*")
    if token:
        request.add_header("Authorization", f"Bearer {token}")
    opener = urllib.request.build_opener(CheckedRedirects)
    written = 0
    with opener.open(request, timeout=120) as response, target.open("wb") as out:
        while chunk := response.read(64 * 1024):
            written += len(chunk)
            if written > MAX_ENGINE_BYTES:
                raise FetchError(f"artifact exceeds {MAX_ENGINE_BYTES} bytes")
            out.write(chunk)


def verify(path: Path, lock: dict[str, str]) -> None:
    expected_bytes = int(lock["bytes"])
    actual_bytes = path.stat().st_size
    if actual_bytes != expected_bytes:
        raise FetchError(
            f"byte length mismatch: lock says {expected_bytes}, got {actual_bytes}"
        )
    digest = hashlib.sha256(path.read_bytes()).hexdigest()
    if digest != lock["sha"]:
        raise FetchError(f"sha256 mismatch: lock says {lock['sha']}, got {digest}")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--lock", type=Path, required=True)
    parser.add_argument("--dest", type=Path, required=True, help="engine root to populate")
    parser.add_argument("--base-url", help="HTTPS prefix; asset_name is appended")
    parser.add_argument("--repo", help="OWNER/NAME holding the release (API mode)")
    parser.add_argument("--tag", help="exact release tag; never `latest`")
    parser.add_argument("--from-file", type=Path, help="copy a local file instead")
    parser.add_argument("--token", help="bearer token for a private release asset")
    args = parser.parse_args()

    if bool(args.repo) != bool(args.tag):
        parser.error("--repo and --tag go together")
    if args.repo and not args.token:
        parser.error("--repo needs --token; a private release asset is not anonymous")

    try:
        lock = read_lock(args.lock)
    except (OSError, FetchError) as error:
        print(f"fetch_engine: {error}", file=sys.stderr)
        return 1

    dest_dir = args.dest / "lib" / "win-x64"
    dest_dir.mkdir(parents=True, exist_ok=True)
    final = dest_dir / lock["asset"]
    staging = dest_dir / f".{lock['asset']}.partial"

    try:
        if args.from_file:
            shutil.copyfile(args.from_file, staging)
        elif args.repo:
            url = asset_url(args.repo, args.tag, lock["asset"], args.token)
            download(url, staging, args.token, api=True)
        elif args.base_url:
            download(f"{args.base_url.rstrip('/')}/{lock['asset']}", staging, args.token)
        else:
            raise FetchError("need --repo/--tag, --base-url or --from-file")
        verify(staging, lock)
    except (OSError, urllib.error.URLError, FetchError) as error:
        staging.unlink(missing_ok=True)
        print(f"fetch_engine: {error}", file=sys.stderr)
        print("nothing was installed.", file=sys.stderr)
        return 1

    staging.replace(final)

    # The signature is what a Release build actually verifies — the lock hash only
    # backs Debug. Fetch it beside the DLL. A release predating signing has none;
    # say so rather than leaving a silent gap for the loader to trip over.
    signature = f"{lock['asset']}.sig"
    signature_staging = dest_dir / f".{signature}.partial"
    try:
        if args.from_file:
            source = args.from_file.with_name(args.from_file.name + ".sig")
            if source.exists():
                shutil.copyfile(source, signature_staging)
            else:
                raise FetchError(f"no signature beside {args.from_file}")
        elif args.repo:
            url = asset_url(args.repo, args.tag, signature, args.token)
            download(url, signature_staging, args.token, api=True)
        else:
            download(f"{args.base_url.rstrip('/')}/{signature}", signature_staging, args.token)
        if signature_staging.stat().st_size != 72:
            raise FetchError(f"{signature} must be exactly 72 bytes")
        signature_staging.replace(dest_dir / signature)
    except (OSError, urllib.error.URLError, FetchError) as error:
        signature_staging.unlink(missing_ok=True)
        print(f"fetch_engine: warning — no usable {signature} ({error}).", file=sys.stderr)
        print("Debug builds still load this engine via the lock hash; Release will not.",
              file=sys.stderr)

    # The lock and header are committed, so copy them beside the library only when
    # the destination is a different directory than the one they live in.
    source_root = args.lock.resolve().parent
    if args.dest.resolve() != source_root:
        (args.dest / "engine.lock").write_bytes(args.lock.read_bytes())
        header = source_root / "include" / "vkey_engine.h"
        if header.exists():
            (args.dest / "include").mkdir(parents=True, exist_ok=True)
            (args.dest / "include" / "vkey_engine.h").write_bytes(header.read_bytes())
        else:
            print(
                f"fetch_engine: warning — no header at {header}; configure will fail",
                file=sys.stderr,
            )

    print(
        f"fetch_engine: ok — {final} ({lock['bytes']} bytes, abi {lock['abi']}, "
        f"counter {lock['counter']})\n"
        f"configure with -DVKEY_ENGINE_ROOT={args.dest}"
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
