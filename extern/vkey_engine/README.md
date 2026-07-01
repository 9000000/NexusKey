# vkey_engine (vendored, prebuilt)

Closed-source build artifact of the portable Vietnamese input engine. The Rust
source lives in the separate vkey-rs repository and is **not** distributed here.

- `include/vkey_engine.h` — the C ABI (see `VKEY_ENGINE_ABI_VERSION`).
- `lib/win-x64/vkey_engine.dll` — Windows x64 runtime library (loaded at runtime).
- `lib/win-x64/vkey_engine.dll.a` — GNU import lib (optional; runtime loading needs no import lib).
- `lib/linux-x64/libvkey_engine.so` — Linux x64 build, for the portable engine tests.

Consumed by `src/core/engine/RustInputEngine.*` via runtime dynamic loading, so
no link-time toolchain matching (MSVC vs GNU) is required.
