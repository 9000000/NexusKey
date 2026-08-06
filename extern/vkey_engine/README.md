# vkey_engine (Vendored Prebuilt Engine)

`vkey_engine` is the portable core engine for Vietnamese text processing used by **VKey**. The engine logic is written in Rust (housed in the closed-source `vkey-rs` repository) and compiled into a standalone dynamic library (`vkey_engine.dll` on Windows / `libvkey_engine.so` on Linux) exposing a pure C ABI defined in `include/vkey_engine.h`.

This directory contains the C header and the security lock manifest. **It no
longer contains the prebuilt libraries.**

## The libraries are fetched, not committed

`vkey_engine.dll`, `libvkey_engine.dll.a` and `libvkey_engine.so` were removed on
2026-08-03 and `lib/` is gitignored. They embed a Vietnamese syllable dictionary
derived from a CC BY-NC corpus, so they are not covered by this repository's
GPL-3.0 licence and do not belong in a tree whose licence tells recipients the
opposite. See [`LICENSE`](LICENSE): noncommercial use only, and that restriction
is inherited rather than chosen.

What stays committed is text that carries no corpus data and pins what a download
must be:

| File | Role |
| --- | --- |
| `engine.lock` | ABI version, byte length and SHA-256 of the exact library. The trust anchor — a download is checked against this, never the other way round. |
| `engine.release` | The exact release tag to fetch. Kept beside the lock so the two cannot drift apart. |
| `include/vkey_engine.h` | The C ABI. |

To populate `lib/`:

```bash
python3 tools/fetch_engine.py \
    --lock extern/vkey_engine/engine.lock \
    --dest build-engine \
    --repo phatMT97/VKey-rs \
    --tag "$(cat extern/vkey_engine/engine.release)" \
    --token "$GH_ENGINE_TOKEN"
```

Then configure with `-DVKEY_ENGINE_ROOT=build-engine`. CI does exactly this. The
release repository is private, so the fetch needs a token with read access to it.
Without one, build with `-DVKEY_USE_RUST_ENGINE=OFF`: that is the default, and
VKey runs on its in-tree C++ engine, fully functional.

The released library is built with **MSVC** on a Windows runner.
`tools/sync_nexuskey_engine.sh` in VKey-rs cross-compiles **mingw** instead, so
its output is a different binary with a different hash and will not satisfy a
released lock. That script stays a local development convenience; the release
workflow is the authority for anything published.

The CMake snippet below links the import library. VKey itself does not do that —
see the note under it — and the import library is no longer in the tree.

---

## 🚀 Quickstart: How to Use `vkey_engine` in Your C/C++ Project

If you want to integrate `vkey_engine` into your own application or IME shell, follow these steps:

### 1. Include Header & Link Binary
Add `include/vkey_engine.h` to your project include path. Link against `vkey_engine.dll` (Windows) or `libvkey_engine.so` (Linux).

#### CMake Example:
```cmake
# Add include directory
target_include_directories(your_app PRIVATE extern/vkey_engine/include)

# Link prebuilt library (Windows GCC/MinGW import lib, or the .so directly)
if(WIN32)
    target_link_libraries(your_app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/extern/vkey_engine/lib/win-x64/libvkey_engine.dll.a)
else()
    target_link_libraries(your_app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/extern/vkey_engine/lib/linux-x64/libvkey_engine.so)
endif()
```

> **VKey itself does not link the import library.** It loads the DLL at runtime with `LoadLibraryExW` only after the length/hash verification described in [Security & Trust Model](#-security--trust-model-audit-guide). Static import linking loads the DLL before any of your code runs, which bypasses that check — if you need the same trust guarantees, resolve the symbols with `GetProcAddress` after verifying the file.

---

### 2. C / C++ Code Example

```c
#include <stdio.h>
#include <stdint.h>
#include "vkey_engine.h"

int main(void) {
    // 1. Check runtime binary status before creating engine
    if (vkey_engine_runtime_status() != VKEY_ENGINE_RUNTIME_OK) {
        fprintf(stderr, "vkey_engine library is tampered or improperly named!\n");
        return 1;
    }

    // 2. Create a Telex engine with modern orthography & spell checking
    uint32_t features = VKEY_FEAT_MODERN_ORTHOGRAPHY | VKEY_FEAT_SPELL_CHECK;
    VKeyEngine *engine = vkey_engine_create(VKEY_METHOD_TELEX, features);
    if (!engine) {
        fprintf(stderr, "Failed to create engine instance\n");
        return 1;
    }

    // 3. Push keystrokes: 'v', 'i', 'e', 't', 's' -> "việt"
    vkey_engine_push_char(engine, 'v');
    vkey_engine_push_char(engine, 'i');
    vkey_engine_push_char(engine, 'e');
    vkey_engine_push_char(engine, 't');
    vkey_engine_push_char(engine, 's');

    // 4. Peek rendered composition (UTF-16 buffer)
    uint16_t utf16_buf[256];
    size_t len = vkey_engine_peek_utf16(engine, utf16_buf, 256);
    printf("Active composition length: %zu code units\n", len);

    // 5. Check engine state flags
    if (vkey_engine_is_english_word(engine)) {
        printf("Word is flagged as English bypass\n");
    }

    // 6. Commit composition (consumes text and resets engine buffer)
    uint16_t committed[256];
    size_t committed_len = vkey_engine_commit_utf16(engine, committed, 256);
    printf("Committed UTF-16 text length: %zu\n", committed_len);

    // 7. Cleanup engine instance
    vkey_engine_destroy(engine);
    return 0;
}
```

---

### 3. Key Operations API Summary

| Task | C Function | Description |
| :--- | :--- | :--- |
| **Create Engine** | `vkey_engine_create(method, features)` | Instantiates an engine (`VKEY_METHOD_TELEX`, `VKEY_METHOD_VNI`, etc.). |
| **Push Character** | `vkey_engine_push_char(engine, codepoint)` | Inputs a Unicode scalar (e.g. ASCII key `'a'`). |
| **Backspace** | `vkey_engine_backspace(engine)` | Erases the last rendered character in composition. |
| **Peek Text** | `vkey_engine_peek_utf16(engine, buf, cap)` | Reads rendered composition as UTF-16 without consuming. |
| **Commit Text** | `vkey_engine_commit_utf16(engine, buf, cap)` | Emits composition text into target buffer and resets engine. |
| **Reset Buffer** | `vkey_engine_reset(engine)` | Clears current composition buffer immediately. |
| **Destroy Engine** | `vkey_engine_destroy(engine)` | Frees engine memory. |

---

## 📐 Architecture & System Boundary

```
 ┌─────────────────────────────────────────────────────────────┐
 │                         VKey Host                           │
 │     (Windows TSF IME Shell / Hook Fallback / Sciter UI)     │
 └──────────────┬──────────────────────────────┬───────────────┘
                │                              │
        Direct Engine Call            Dynamic Verification & Load
     (vkey_engine_push_char,           (RustEngineTrust.cpp &
      vkey_engine_commit_utf16)         RustEngineLoader.cpp)
                │                              │
                ▼                              ▼
 ┌─────────────────────────────────────────────────────────────┐
 │                     include/vkey_engine.h                   │
 │                     (C ABI Surface - v6)                    │
 └──────────────────────────────┬──────────────────────────────┘
                                │
                                ▼
 ┌─────────────────────────────────────────────────────────────┐
 │                vkey_engine.dll / .so                        │
 │     (Prebuilt Portable Engine Core - Rust vkey-rs)          │
 └─────────────────────────────────────────────────────────────┘
```

VKey acts as the OS-level shell (intercepting keyboard input via Windows Text Services Framework or low-level hooks) and delegates Vietnamese diacritic, spell checking, lexicon lookup, tone placement, and custom mapping logic to `vkey_engine`. All strings cross the ABI boundary as UTF-16 code units (`uint16_t*`) to match the native Windows `wchar_t` representation, so the host needs no transcoding step. Text is copied into a caller-supplied buffer (`peek_utf16` / `commit_utf16`); the engine never hands out a pointer into its own state.

---

## 📁 Directory Structure

```
extern/vkey_engine/
├── README.md               # This technical documentation & usage guide
├── engine.lock             # Binary integrity manifest (ABI version, size, SHA-256)
├── include/
│   └── vkey_engine.h       # C ABI header (defines VKEY_ENGINE_ABI_VERSION)
└── lib/
    ├── win-x64/
    │   ├── vkey_engine.dll      # Windows x64 prebuilt runtime library
    │   └── libvkey_engine.dll.a # GNU import library (optional; VKey loads at runtime)
    └── linux-x64/
        └── libvkey_engine.so # Linux x64 build (for cross-platform tests)
```

---

## 🔒 Security & Trust Model (Audit Guide)

Because VKey loads an external binary (`vkey_engine.dll`) for advanced input processing, strict defense-in-depth security measures are enforced to prevent DLL hijacking, tampering, or malicious replacement.

### 1. Compile-Time Hash Pinning (`engine.lock`)
The `engine.lock` file pins the expected binary metadata:
```ini
VKEY-ENGINE-LOCK-V1
abi_version=6
target=windows-x86_64
asset_name=vkey_engine.dll
installed_name=vkey_engine.dll
byte_len=1261568
sha256=38a89c2d7641e09c0e005e23b4a0606dd7c7a77649837e2efaa611de438028a8
```
During CMake build, `VKeyEngineLock.h.in` compiles these values (`kAbiVersion`, `kByteLength`, `kSha256`) directly into the `VKeyApp.exe` binary.

### 2. Time-Of-Check to Time-Of-Use (TOCTOU) Race Prevention
In [`RustEngineLoader.cpp`](../../src/core/engine/RustEngineLoader.cpp):
1. VKey opens `vkey_engine.dll` using `CreateFileW` with **exclusive write/delete share flags** (`FILE_SHARE_READ` only). This locks the file on disk, preventing any concurrent process from modifying or replacing the file during verification.
2. The opened handle is passed to [`VerifyRustEngineFileHandle()`](../../src/core/engine/RustEngineTrust.cpp).
3. The exact file size is verified using `GetFileSizeEx` against `VKeyEngineLock::kByteLength`.
4. The file is streamed through Windows CNG (Cryptography Next Generation) API (`BCryptOpenAlgorithmProvider` / `BCryptHashData`) to compute its SHA-256 hash.
5. The hash digest is compared against `VKeyEngineLock::kSha256` in constant time (`HashMatches()`) to eliminate timing side-channel leaks.

### 3. Safe Dynamic Loading & File Identity Binding
1. If trusted, `LoadLibraryExW` is invoked with restricted search flags (`LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32`) to prevent DLL search path hijacking (`PATH` / CWD attacks).
2. After loading, [`SameFile()`](../../src/core/engine/RustEngineLoader.cpp#L69-L88) obtains the OS file handle of the newly loaded module via `ModulePath()` and compares its `dwVolumeSerialNumber`, `nFileIndexHigh`, and `nFileIndexLow` against the verified handle using `GetFileInformationByHandle`. This guarantees Windows actually loaded the verified file on disk and not a spoofed module.

### 4. Engine Self-Verification (`vkey_engine_runtime_status`)
The Rust engine binary includes internal runtime checks:
- On startup, `vkey_engine_runtime_status()` checks that its loaded module filename matches `vkey_engine.dll` (Windows) or `libvkey_engine.so` (Linux).
- `vkey_engine_create()` strictly requires `vkey_engine_runtime_status() == VKEY_ENGINE_RUNTIME_OK` (0); otherwise it returns `NULL`.

---

## 🌐 Distribution & On-Demand Download Policy

To keep standard release distributions lightweight, VKey release ZIPs omit `vkey_engine.dll` by default.

### Secure Download Flow
When the user toggles **Advanced Engine Mode** in VKey:
1. **URL Whitelisting**: [`AdvancedEngineDownloadPolicy.cpp`](../../src/app/system/AdvancedEngineDownloadPolicy.cpp) enforces that download requests can ONLY target official GitHub release endpoints:
   - `github.com`
   - `objects.githubusercontent.com`
   - `release-assets.githubusercontent.com`
2. **Release Tag Alignment**: The download URL is constructed directly from the host version: `https://github.com/phatMT97/VKey/releases/download/v{VERSION}/vkey_engine.dll`.
3. **Atomic Verification & Install**: The downloaded file is written to a temporary location, verified against the compiled `engine.lock` SHA-256 hash, and atomically moved adjacent to `VKeyApp.exe`.

---

## 🛠️ C ABI Reference (`include/vkey_engine.h`)

The library exposes a thread-safe, opaque C handle model (`VKeyEngine*`). All state mutations for a specific handle must be serialized by the caller.

| ABI Version | Feature / Surface Added |
| :--- | :--- |
| **v1** | Core lifecycle (`create`, `destroy`, `reset`), key pushing (`push_char`), backspace, UTF-16 composition peeking (`peek_utf16`), committing (`commit_utf16`), input methods (Telex, VNI, Simple Telex, Combined, User-Defined), feature bitfield flags. |
| **v2** | Host status queries: English word detection (`is_english_word`), tone escape status (`is_tone_escaped`), quick consonant detection (`has_active_quick_consonant`), raw physical keystroke peeking (`peek_raw_utf16`), text seeding (`seed_text_utf16`). |
| **v3** | Embedded lexicon lookup & smart restore override (`set_lexicon`), correction indicator (`last_commit_was_corrected`), suggestion candidate inspection (`suggest_count`, `suggest_utf16`). |
| **v4** | User-defined custom keymaps (`set_custom_keymap`) supporting 35+ canonical Vietnamese input actions (`VKEY_KEY_ACTION_*`). |
| **v5** | Process-wide spell-check exclusion dictionary (`set_spell_exclusions_utf16`). |
| **v6** | Enforced binary runtime identity validation (`vkey_engine_runtime_status`). |

### Key Input Methods & Feature Bitfield
```c
/* Methods */
#define VKEY_METHOD_TELEX        0u
#define VKEY_METHOD_VNI          1u
#define VKEY_METHOD_SIMPLE_TELEX 2u
#define VKEY_METHOD_COMBINED     3u
#define VKEY_METHOD_USER_DEFINED 4u

/* Feature Flags Bitmask */
#define VKEY_FEAT_MODERN_ORTHOGRAPHY   (1u << 0) // Modern vs classic tone placement (e.g., hòa vs hoà)
#define VKEY_FEAT_QUICK_START_CONSONANT (1u << 1) // Consonant shortcuts (e.g., f -> ph)
#define VKEY_FEAT_QUICK_CONSONANT      (1u << 2) // Quick consonant combinations (e.g., cc -> ch)
#define VKEY_FEAT_QUICK_END_CONSONANT  (1u << 3) // Ending consonant shortcuts (e.g., g -> ng)
#define VKEY_FEAT_SPELL_CHECK          (1u << 4) // Vietnamese spell checking validation
#define VKEY_FEAT_ALLOW_ENGLISH_BYPASS (1u << 5) // Automatically pass through non-Vietnamese words
#define VKEY_FEAT_ALLOW_ZWJF           (1u << 6) // Zero-width joiner support
#define VKEY_FEAT_SPELL_SUGGEST        (1u << 7) // Enable lexicon spell suggestion candidates
```

---

## 🔄 Synchronization & Maintenance (Maintainer Workflow)

> **Note for External Auditors & Contributors**: The `vkey-rs` repository is private/closed-source and accessible only to core maintainers. This section is documented for architecture completeness to explain how prebuilt engine updates are integrated into VKey.

For core maintainers updating `vkey_engine` artifacts from `vkey-rs`:
1. In the `vkey-rs` repository, run:
   ```bash
   tools/sync_nexuskey_engine.sh /path/to/VKey
   ```
2. The script copies updated headers (`vkey_engine.h`), prebuilt binaries (`.dll`, `.dll.a`, `.so`), updates `engine.lock`, and triggers CMake code generation for `VKeyEngineLock.h`.
3. Run VKey unit tests to verify ABI compatibility:
   ```bash
   cmake -B build-linux -G "Unix Makefiles" -DCMAKE_BUILD_TYPE=Debug
   cmake --build build-linux --target VKeyTests
   ./build-linux/tests/VKeyTests
   ```

---

## 🔍 How to Audit the Binary

To verify that the binary distributed in this repository matches `engine.lock`:

1. **Calculate SHA-256 Digest**:
   - **Linux**: `sha256sum extern/vkey_engine/lib/win-x64/vkey_engine.dll`
   - **Windows PowerShell**: `Get-FileHash extern\vkey_engine\lib\win-x64\vkey_engine.dll -Algorithm SHA256`
2. **Compare output**: Verify that the computed SHA-256 string and file byte size match line 6 & 7 in [`engine.lock`](engine.lock).
3. **ABI Version Check**: Confirm `VKEY_ENGINE_ABI_VERSION` in [`include/vkey_engine.h`](include/vkey_engine.h#L24) matches `abi_version` in `engine.lock`.
