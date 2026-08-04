# Engine Workflows

What to run, for each of the four things you actually do. The prebuilt engine is
no longer committed — `extern/vkey_engine/lib/` is gitignored — so every path
below either fetches it or does not need it.

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
bash tools/sync_nexuskey_engine.sh          # builds mingw, copies it, rewrites the lock
```

Then build as in §1. **Do not commit the `engine.lock` it rewrites.** That lock
describes your local mingw build; the released engine is MSVC and a different
binary, so committing it makes the tree disagree with `engine.release` and CI's
fetch fails its hash check. To go back:

```bash
git checkout -- extern/vkey_engine/engine.lock
rm -rf build-engine
```

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

---

## When something fails

| Message | Cause |
| --- | --- |
| `VKEY_ENGINE_TOKEN is unset or empty` | no token, or it expired |
| `HTTP 404 — wrong tag, or the token cannot read that repository` | tag typo, or the PAT is not scoped to VKey-rs |
| `sha256 mismatch` / `byte length mismatch` | `engine.lock` and `engine.release` disagree — usually a locally synced mingw lock, see §3 |
| `No prebuilt engine at ...` | nothing fetched yet; §1, or build with `-DVKEY_USE_RUST_ENGINE=OFF` |
| `is still a draft` | publish the release first |

Related: [`../extern/vkey_engine/LICENSE`](../extern/vkey_engine/LICENSE) for why
the engine is noncommercial-only, and the engine release runbook in VKey-rs for
cutting a release in the first place.
