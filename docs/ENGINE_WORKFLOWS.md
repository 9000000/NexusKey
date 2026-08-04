# Engine Workflows

## Short version

```powershell
.\vkey.ps1 local      # build and run here, with the Rust engine
.\vkey.ps1 test       # release the engine and start the build testers download
```

Once, in a new terminal afterwards:

```powershell
setx VKEY_ENGINE_TOKEN "<token>"
```

That is the whole day-to-day. Everything below is what those two do and how to
drive the pieces yourself when something needs unpicking.

The one thing worth knowing before you need it: **pushing a NexusKey commit does
not carry an engine change with it.** The engine is fetched from the release
named in `extern/vkey_engine/engine.release`, so getting engine work in front of
a tester means `.\vkey.ps1 test`, not `git push`.

---

The prebuilt engine is no longer committed — `extern/vkey_engine/lib/` is
gitignored — so every path below either fetches it or does not need it.

## One-time setup

The engine release lives in the private `phatMT97/VKey-rs`, so fetching needs a
token. Create a fine-grained PAT scoped to that one repository with
**Contents: Read-only**, then:

```powershell
setx VKEY_ENGINE_TOKEN "<token>"     # persists; open a new terminal after
```

```bash
export VKEY_ENGINE_TOKEN=<token>     # add to your shell profile
```

Fine-grained tokens expire. When yours does, the fetch fails during a build and
looks like a build problem — the message names the token, so read it.

---

## 1. Build and run locally

Driving CMake yourself is two extra pieces: fetch the engine once, and say you
want it. **A bare `cmake -B build` now builds without the Rust engine** — the
option defaults off so a fork does not need the noncommercial artifact — so a
command that used to include the engine silently stops doing so.

```powershell
.\tools\fetch-engine.ps1                          # once; no-op afterwards
cmake -B build -G "Visual Studio 18 2026" -A x64 -DVKEY_USE_RUST_ENGINE=ON -DVKEY_ENGINE_ROOT=build-engine
cmake --build build --config Release
```

Only the configure line needs the two flags; rebuilds are unchanged because the
cache remembers them. You only revisit this after deleting `CMakeCache.txt`.

Or let a script do all three:

```powershell
.\internal\tools\build_app.ps1 -Release -Run      # Sciter edition
.\internal\tools\build_lite.ps1 -Release -Run     # Classic edition
```

Each fetches the engine into `build-engine/` on first use and skips it after, so
the edit-build loop is unchanged. Add `-Clean` to reconfigure from scratch.

Working on something unrelated to the engine? Skip it entirely and skip the token
with it:

```powershell
cmake -B build -G "Visual Studio 17 2022" -A x64      # engine off by default
```

CMake warns when it configures this way, because `RustInputEngineTest` and half
of `EngineFactoryTest` are inside `#ifdef VKEY_USE_RUST_ENGINE` — a green `ctest`
from that build does not cover the Rust engine.

## 2. Build for users to test

**Actions → Build → Run workflow.** Nothing to install, no token on your machine.

The defaults are the ones you want: tests on, engine included, Sciter edition.
Untick `sign_binaries` for a quick build that skips SignPath.

The artifact carries the app, `sciter.dll`, the engine, and both notice files —
`THIRD_PARTY_NOTICES.txt` for Sciter and the icons, `vkey_engine.LICENSE.txt` for
the engine. Those are licence conditions, not decoration; do not hand someone the
binaries without them.

## 3. Change the engine itself

The engine source is in VKey-rs. To try a local build of it here, without cutting
a release:

```bash
cd ~/code/VKey-rs
bash tools/sync_nexuskey_engine.sh          # builds mingw into NexusKey/build-engine/
```

Then build against it:

```powershell
cmake -B build -G "Visual Studio 17 2022" -A x64 -DVKEY_USE_RUST_ENGINE=ON -DVKEY_ENGINE_ROOT=build-engine
```

It writes to the gitignored `build-engine/`, including that build's own
`engine.lock`, so nothing tracked changes and there is no committed lock to
restore. The lock in `extern/vkey_engine/` keeps describing the published
release, which is what CI fetches and verifies.

Back to the released engine:

```bash
rm -rf build-engine        # the next build fetches it again
```

Your local build is mingw and the released one is MSVC, so the two have different
hashes. That is expected, and it is exactly why the local build gets its own lock
in its own directory rather than overwriting the committed one.

## 3b. Get an engine change in front of testers

The engine is not committed here, so pushing a NexusKey commit does not carry an
engine change with it — CI fetches whatever `engine.release` names. Shipping a
change to testers means cutting an engine release and repointing this repository
at it. One command, from VKey-rs:

```bash
cd ~/code/VKey-rs
python3 tools/ship_engine.py --build
```

It bumps the patch version, tags, waits for the release workflow, publishes as a
prerelease, updates this repository's `engine.lock` and `engine.release`, commits
and pushes them, and starts the test build. Roughly five minutes, mostly the
Windows runner.

Version numbers are not meaningful in this loop — they exist so a build pins an
exact one. `--version X.Y.Z` if you want to choose.

VKey-rs must have a clean tree: a tag points at HEAD, so uncommitted engine work
would not be in the release.

## 4. Move to a new engine release

After cutting and **publishing** `engine-vX.Y.Z` in VKey-rs (draft assets are not
downloadable by tag):

```bash
python3 tools/update_engine.py engine-vX.Y.Z
```

It writes the release's own `engine.lock` and the tag into
`extern/vkey_engine/`, then proves the pair by fetching the library through the
normal path. If the release's lock does not verify against its own library it
restores the previous lock and changes nothing.

Commit `engine.lock` and `engine.release` **together** — they are two files that
have to agree, and nothing at build time can tell you they do not. A stale lock
beside a new tag fails with a hash mismatch that reads like a corrupt download.

Cutting a product release does this for you: `internal/tools/release_tag.sh`
compares the pin against the newest engine release and offers to update it before
the version-bump commit, so a release does not quietly ship whatever engine
happened to be pinned last.

---

## When something fails

| Message | Cause |
| --- | --- |
| `VKEY_ENGINE_TOKEN is unset or empty` | no token, or it expired |
| `HTTP 404 — wrong tag, or the token cannot read that repository` | tag typo, or the PAT is not scoped to VKey-rs |
| `sha256 mismatch` / `byte length mismatch` | `engine.lock` and `engine.release` disagree; use `update_engine.py` (§4) rather than editing either by hand |
| `No prebuilt engine at ...` | nothing fetched yet; §1, or build with `-DVKEY_USE_RUST_ENGINE=OFF` |
| `is still a draft` | publish the release first |

Related: [`../extern/vkey_engine/LICENSE`](../extern/vkey_engine/LICENSE) for why
the engine is noncommercial-only, and the engine release runbook in VKey-rs for
cutting a release in the first place.
