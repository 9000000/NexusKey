# vkey_engine (vendored, prebuilt)

Closed-source build artifact of the portable Vietnamese input engine. The Rust
source lives in the separate vkey-rs repository and is **not** distributed here.

- `include/vkey_engine.h` — the C ABI (see `VKEY_ENGINE_ABI_VERSION`).
- `engine.lock` — ABI, byte length and SHA-256 compiled into NexusKey.
- `lib/win-x64/vkey_engine.dll` — Windows x64 runtime library (loaded at runtime).
- `lib/win-x64/vkey_engine.dll.a` — GNU import lib (optional; runtime loading needs no import lib).
- `lib/linux-x64/libvkey_engine.so` — Linux x64 build, for the portable engine tests.

`tools/sync_nexuskey_engine.sh` in VKey-rs updates the header, artifacts and lock
together. NexusKey loads only the module-relative official filename and verifies
the locked Windows artifact before executing it. Release ZIPs omit the DLL; when
the user first enables Advanced mode, NexusKey downloads the exact
`vkey_engine.dll` asset from the matching NexusKey release tag, verifies it
against the compiled lock, and atomically installs it beside the executable.
