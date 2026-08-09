# Engine Workflows

## Short version

```powershell
.\vkey.cmd local              # Release + engine synced from local VKey-rs
.\vkey.cmd local -Engine Released
                              # Release + published, signed engine
.\vkey.cmd local -DebugBuild  # ... Debug, which serves the Sciter UI from files
```

Use `.\vkey.cmd`, not `.\vkey.ps1`. PowerShell does not run commands from the
current directory, so the `.\` is required, and naming the `.cmd` avoids it
picking the `.ps1` sitting beside it. PowerShell refuses unsigned scripts, and this
repository usually sits on a mapped WSL drive, which Windows treats as remote —
so even `RemoteSigned` blocks it. `vkey.cmd` is a batch file, which the execution
policy does not apply to, and it starts PowerShell with the policy bypassed for
that one process. Nothing about the machine changes.

`-DebugBuild` builds Debug, where CMake copies the Sciter UI beside the exe
instead of embedding it with packfolder. Editing HTML or CSS then needs the app
restarted, not rebuilt. (`-Debug` is a PowerShell common parameter, hence the
name.)

Only the published-engine workflow needs a token. Set it once, then open a new
terminal:

```powershell
setx VKEY_ENGINE_TOKEN "<token>"
```

That is the whole day-to-day. Everything below explains what those modes do and how to
drive the pieces yourself when something needs unpicking.

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

`local` means the engine from the adjacent `VKey-rs` source checkout. Sync it
first in WSL:

```bash
cd ~/code/VKey-rs
bash tools/sync_nexuskey_engine.sh
```

Then build the optimized app from PowerShell:

```powershell
cd Z:\home\phatmt\code\NexusKey
.\vkey.cmd local
```

This is still a Release build. Because a locally built engine has no release
signature, this command explicitly enables exact-hash development trust: the DLL
must match `build-engine/engine.lock`. That opt-in is off by default in CMake and
is never used by official builds. The command does not call the fetcher and will
not replace the local DLL.

To build locally against the published engine instead:

```powershell
.\vkey.cmd local -Engine Released
```

That mode fetches the signed engine once and needs `VKEY_ENGINE_TOKEN`.

### Driving CMake directly

Driving CMake yourself is two extra pieces: fetch the engine once, and say you
want it. **A bare `cmake -B build` now builds without the Rust engine** — the
option defaults off so a fork does not need the noncommercial artifact — so a
command that used to include the engine silently stops doing so.

```powershell
.\tools\fetch-engine.ps1                          # once; no-op afterwards
cmake -B build -G "Visual Studio 18 2026" -A x64 -DVKEY_USE_RUST_ENGINE=ON -DVKEY_ENGINE_ROOT=build-engine
cmake --build build --config Release
```

For an unsigned local engine in an optimized build, make the trust opt-in
explicit:

```powershell
cmake -B build -G "Visual Studio 18 2026" -A x64 `
  -DVKEY_USE_RUST_ENGINE=ON `
  -DVKEY_ENGINE_ROOT=build-engine `
  -DVKEY_ALLOW_UNSIGNED_LOCAL_ENGINE=ON
cmake --build build --config Release
```

Only the configure line needs the two flags; rebuilds are unchanged because the
cache remembers them. You only revisit this after deleting `CMakeCache.txt`.

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
.\vkey.cmd local
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

To switch back without deleting anything, use `-Engine Released`. To make the
default local mode work again afterwards, rerun the sync command.

---

## When something fails

| Message | Cause |
| --- | --- |
| `VKEY_ENGINE_TOKEN is unset or empty` | no token, or it expired |
| `HTTP 404 — wrong tag, or the token cannot read that repository` | tag typo, or the PAT is not scoped to VKey-rs |
| `sha256 mismatch` / `byte length mismatch` | `engine.lock` and `engine.release` disagree; do not edit either file independently |
| `No prebuilt engine at ...` | nothing fetched yet; §1, or build with `-DVKEY_USE_RUST_ENGINE=OFF` |

Related: [`../extern/vkey_engine/LICENSE`](../extern/vkey_engine/LICENSE) for why
the engine is noncommercial-only.
