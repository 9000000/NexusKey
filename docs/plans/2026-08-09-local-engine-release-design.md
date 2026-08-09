# Local engine in Release builds

## Goal

`vkey.cmd local` must build the optimized Release configuration against the
engine currently synced from the sibling `VKey-rs` checkout. It must not replace
that engine with a published artifact as a side effect.

## CLI behavior

- `vkey.cmd local` defaults to `-Engine Local`.
- `-Engine Local` requires the artifacts produced in `build-engine/` by
  `VKey-rs/tools/sync_nexuskey_engine.sh`, skips `fetch-engine.ps1`, and builds
  Release unless `-DebugBuild` is explicitly supplied.
- `-Engine Released` preserves the existing fetch-and-build workflow for a
  published, signed engine.
- A missing local engine is an error with the exact sync command. The script
  never silently changes the selected engine source.

## Trust boundary

Official Release builds continue to require the detached engine signature.
CMake gains an option, defaulting to OFF, that explicitly compiles the existing
exact-size-and-SHA-256 development trust path into a local Release build. Only
`vkey.cmd local -Engine Local` enables it. The generated lock remains bound to
the local DLL, so replacing the DLL still fails verification.

The Released path explicitly disables this option on every configure. This
prevents a shared CMake cache from retaining local trust after switching back to
a signed engine.

## Verification

Check Windows PowerShell 5.1-compatible syntax, verify the CMake option defaults
OFF, and configure/build the available host test target where the environment
supports it. The help text and engine workflow documentation must describe both
engine selections and the local Release trust model.
