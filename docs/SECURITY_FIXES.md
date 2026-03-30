# NexusKey Security Fixes

> **Threat model:** Local Vietnamese IME running on a personal Windows machine.
> Primary attacker = malicious software running in the same user session (no admin required).
> Network MITM = low priority for user-triggered updates.

---

## Priority Overview

| ID | Issue | Exploitable by local malware? | Fix effort |
|----|-------|-------------------------------|------------|
| **FIX-1** | Named objects created without DACL | ✅ Yes — pre-create object before NexusKey starts | 1 hour |
| **FIX-2** | Config file has no size/length limits | ✅ Yes — write large config → crash/OOM | 30 min |
| **FIX-3** | HKCU CLSID can override TSF DLL registration | ✅ Yes — inject DLL into all TSF apps | 30 min |
| **FIX-4** | Shared memory enum fields not validated | ✅ Yes — if FIX-1 not done | 15 min |
| **FIX-5** | Seqlock memory barrier in wrong position | ⚠️ Race condition — data corruption | 5 min |
| **FIX-6** | Keystroke buffer not cleared on focus change | ⚠️ Requires DLL injection first | 10 min |
| **FIX-7** | `--install-update` accepts arbitrary zip path | ✅ Yes — local malware replaces zip file | 10 min |

**Skip for now:** cert pinning, temp file randomization, schtasks escaping, Authenticode signing (need cert purchase).

---

## FIX-1 — Named Windows Objects Must Have Restricted DACL

### Affected files
- `src/core/ipc/SharedStateManager.cpp` — `CreateFileMappingW`
- `src/core/config/ConfigEvent.cpp` — `CreateEventW`
- `src/app/main.cpp` — `CreateMutexW` (single-instance guard)

### Why it matters
All three objects are created with `nullptr` security attributes → default DACL → any user-session
process can open them. A malicious process can pre-create `Local\NexusKeySharedState` before
NexusKey starts, then write arbitrary values into shared memory (corrupt `inputMethod`, `flags`,
disable the engine). The config event can be signaled by any process to force a config reload
from a malware-written `config.toml`.

### Step 1 — Create `src/core/ipc/SecurityHelpers.h`

```cpp
#pragma once
#include <windows.h>
#include <sddl.h>

namespace NextKey {

// Returns a SECURITY_ATTRIBUTES that grants full access only to SYSTEM and the
// creator/owner. Call LocalFree(sa.lpSecurityDescriptor) after the handle is created.
//
// SDDL breakdown:
//   D:PAI          — DACL, protected, auto-inherited
//   (A;;GA;;;SY)   — Allow GENERIC_ALL to SYSTEM
//   (A;;GA;;;CO)   — Allow GENERIC_ALL to Creator/Owner
inline SECURITY_ATTRIBUTES MakeCreatorOnlySecurityAttributes() noexcept {
    PSECURITY_DESCRIPTOR pSD = nullptr;
    ConvertStringSecurityDescriptorToSecurityDescriptorW(
        L"D:PAI(A;;GA;;;SY)(A;;GA;;;CO)",
        SDDL_REVISION_1,
        &pSD,
        nullptr);
    // If ConvertString fails (shouldn't on any supported Windows version),
    // pSD stays nullptr → CreateFileMapping uses default DACL → safer than crashing.
    SECURITY_ATTRIBUTES sa{};
    sa.nLength = sizeof(SECURITY_ATTRIBUTES);
    sa.lpSecurityDescriptor = pSD;
    sa.bInheritHandle = FALSE;
    return sa;
}

} // namespace NextKey
```

### Step 2 — Apply to `SharedStateManager.cpp`

```cpp
#include "SecurityHelpers.h"

// In SharedStateManager::Create():
auto sa = NextKey::MakeCreatorOnlySecurityAttributes();
pImpl_->hMapping = CreateFileMappingW(
    INVALID_HANDLE_VALUE,
    &sa,                  // ← was nullptr
    PAGE_READWRITE,
    0,
    sizeof(SharedState),
    SHARED_MEM_NAME
);
if (sa.lpSecurityDescriptor) LocalFree(sa.lpSecurityDescriptor);
```

### Step 3 — Apply to `ConfigEvent.cpp`

```cpp
#include "../ipc/SecurityHelpers.h"

// In ConfigEvent::Create():
auto sa = NextKey::MakeCreatorOnlySecurityAttributes();
hEvent_ = CreateEventW(
    &sa,                  // ← was nullptr
    FALSE, FALSE,
    CONFIG_EVENT_NAME
);
if (sa.lpSecurityDescriptor) LocalFree(sa.lpSecurityDescriptor);
```

### Step 4 — Apply to `main.cpp` single-instance mutex

```cpp
#include "../core/ipc/SecurityHelpers.h"

// In WinMain, single-instance check:
auto sa = NextKey::MakeCreatorOnlySecurityAttributes();
HANDLE hMutex = CreateMutexW(
    &sa,                  // ← was nullptr
    TRUE,
    L"Local\\NexusKey_Main_Mutex"
);
if (sa.lpSecurityDescriptor) LocalFree(sa.lpSecurityDescriptor);
```

---

## FIX-2 — Config File Must Have Size and Length Limits

### Affected file
- `src/core/config/ConfigManager.cpp`

### Why it matters
`toml::parse_file()` has no file size guard. A malicious process can write a `config.toml`
with a 100 MB macro value. When the user presses a macro trigger key, `HookEngine` tries to
allocate a `vector<INPUT>` with ~100 M entries → OOM crash. The config event signal (FIX-1)
makes this trivially triggerable without any user interaction.

### Fix — Add guards in `ConfigManager.cpp`

```cpp
#include <filesystem>

// At the start of LoadConfig() / the internal parse function, before toml::parse_file():
static constexpr uintmax_t kMaxConfigFileSizeBytes = 1 * 1024 * 1024; // 1 MB

std::error_code ec;
auto fileSize = std::filesystem::file_size(configPath_, ec);
if (ec || fileSize > kMaxConfigFileSizeBytes) {
    NEXTKEY_LOG(L"[ConfigManager] Config file too large or unreadable (%llu bytes), using defaults",
                static_cast<unsigned long long>(fileSize));
    return; // fall through to compiled defaults
}

auto config = toml::parse_file(configPath_.string());
```

```cpp
// In LoadMacros(), when iterating the [macros] table:
static constexpr size_t kMaxMacroKeyLen    = 32;
static constexpr size_t kMaxMacroValueLen  = 512;

for (auto& [key, value] : *macrosTable) {
    std::string triggerUtf8 = key;
    std::string expansionUtf8 = value.value_or<std::string>("");

    if (triggerUtf8.size() > kMaxMacroKeyLen || expansionUtf8.size() > kMaxMacroValueLen) {
        NEXTKEY_LOG(L"[ConfigManager] Macro entry too long, skipping (trigger=%zu, expansion=%zu)",
                    triggerUtf8.size(), expansionUtf8.size());
        continue; // skip oversized entries, don't abort load
    }
    // ... existing load logic
}
```

```cpp
// In LoadPerAppCodeTable(), after reading the table:
static constexpr size_t kMaxPerAppEntries = 256;
size_t entryCount = 0;

for (auto& [appName, codeTable] : *perAppTable) {
    if (++entryCount > kMaxPerAppEntries) {
        NEXTKEY_LOG(L"[ConfigManager] Per-app table exceeds limit (%zu), truncating", kMaxPerAppEntries);
        break;
    }
    // ... existing load logic
}
```

---

## FIX-3 — Remove HKCU CLSID Override at Startup

### Affected file
- `src/tsf/Register.cpp` or called from `src/app/main.cpp` on startup

### Why it matters
Any user-level process can write to `HKCU\Software\Classes\CLSID\{NexusKey-GUID}` and point the
`InprocServer32` value to an arbitrary DLL. Because `HKEY_CLASSES_ROOT` merges HKCU on top of
HKLM, Windows loads the attacker's DLL instead of the real `NextKeyTSF.dll` into every TSF-aware
application (Word, Chrome, Notepad, etc.). No admin required.

### Fix — Add `CleanupHkcuClsidOverride()` to `Register.cpp`

```cpp
// In Register.cpp — call this from DllRegisterServer() and from main.cpp on startup

static const wchar_t* kClsidString = L"{YOUR-NEXUSKEY-CLSID-HERE}"; // same GUID as Globals.cpp

void CleanupHkcuClsidOverride() noexcept {
    wchar_t keyPath[256];
    swprintf_s(keyPath, L"Software\\Classes\\CLSID\\%s", kClsidString);

    // Check if HKCU override exists
    HKEY hKey = nullptr;
    LSTATUS ls = RegOpenKeyExW(HKEY_CURRENT_USER, keyPath, 0, KEY_READ, &hKey);
    if (ls != ERROR_SUCCESS) return; // not present, nothing to do
    RegCloseKey(hKey);

    // Remove the entire HKCU subtree for this CLSID
    ls = RegDeleteTreeW(HKEY_CURRENT_USER, keyPath);
    if (ls == ERROR_SUCCESS) {
        NEXTKEY_LOG(L"[Register] Removed HKCU CLSID override for NexusKey TSF");
    } else {
        NEXTKEY_LOG(L"[Register] Warning: could not remove HKCU CLSID override (error=%ld)", ls);
    }
}
```

```cpp
// In main.cpp WinMain, after app initialization:
CleanupHkcuClsidOverride(); // Remove any malware-planted HKCU CLSID override
```

---

## FIX-4 — Validate Shared Memory Enum Fields Before Use

### Affected file
- `src/tsf/EngineController.cpp` — `ApplySharedState()`

### Why it matters
If FIX-1 is not yet applied (or during the race window at startup), a malicious process
can write `inputMethod = 0xFF` into shared memory. Casting an out-of-range value to an enum
and passing it to `EngineFactory::Create()` is undefined behavior — potential crash or wrong
engine instantiation.

### Fix

```cpp
void EngineController::ApplySharedState(const SharedState& state) {
    // Validate inputMethod (valid range: 0–2)
    InputMethod newMethod = InputMethod::Telex; // safe default
    if (state.inputMethod <= 2) {
        newMethod = static_cast<InputMethod>(state.inputMethod);
    } else {
        NEXTKEY_LOG(L"[EngineController] Invalid inputMethod %d from shared state, using Telex",
                    state.inputMethod);
    }

    // Validate optimizeLevel (valid range: 0–2)
    uint8_t optimizeLevel = 0;
    if (state.optimizeLevel <= 2) {
        optimizeLevel = state.optimizeLevel;
    }

    // Validate codeTable (valid range: 0–4, check against CodeTable enum max)
    // Add similar guard for state.codeTable if it's used here

    config_.inputMethod    = newMethod;
    config_.optimizeLevel  = optimizeLevel;
    config_.spellCheckEnabled = state.spellCheck != 0;
    DecodeFeatureFlags(state.GetFeatureFlags(), config_);

    currentMethod_ = newMethod;
    engine_ = EngineFactory::Create(config_);
}
```

---

## FIX-5 — Seqlock Memory Barrier Order

### Affected file
- `src/core/ipc/SharedStateManager.cpp` — `Read()`

### Why it matters
The current code reads `epoch` before calling `MemoryBarrier()`. On weakly-ordered architectures
(ARM) the CPU can speculate the epoch read and return a stale value while the struct is mid-write.
The retry check then passes on corrupted data.

### Fix

The current code:
```cpp
uint32_t before = pImpl_->pState->epoch;  // read epoch
MemoryBarrier();                           // barrier AFTER — wrong on ARM
state = *pImpl_->pState;
MemoryBarrier();
uint32_t after = pImpl_->pState->epoch;
```

Correct seqlock read pattern:
```cpp
// Acquire fence before reading epoch so subsequent loads are not reordered before it.
// On x86 this is a no-op (TSO guarantees), but correct on ARM/ARM64 too.
uint32_t before = static_cast<volatile const SharedState*>(pImpl_->pState)->epoch;
std::atomic_thread_fence(std::memory_order_acquire); // C++11 portable, replaces MemoryBarrier
state = *pImpl_->pState;
std::atomic_thread_fence(std::memory_order_acquire);
uint32_t after = static_cast<volatile const SharedState*>(pImpl_->pState)->epoch;

if (before == after && (before & 1) == 0) {
    return state;
}
```

> Note: On x86/x64 (the only target for NexusKey) the original code is functionally safe due to
> TSO memory model. Fix anyway for correctness and future ARM compatibility.

---

## FIX-6 — Clear Keystroke Buffer on Focus Change

### Affected file
- `src/app/system/HookEngine.cpp` — `ResetComposition()`

### Why it matters
`ResetComposition()` clears `inputHistory_` but NOT `commitStack_`. A committed word (including
passwords typed in composition mode) persists in `commitStack_` across application switches until
`CancelCommitUndo()` is explicitly called. If a malicious DLL is injected into the NexusKey
process, it can read committed words directly from memory.

### Fix

```cpp
void HookEngine::ResetComposition() noexcept {
    // ... existing reset logic ...

    // Clear keystroke history using secure erase to prevent heap forensics
    SecureZeroMemory(inputHistory_.data(), inputHistory_.size() * sizeof(wchar_t));
    inputHistory_.clear();

    // Also clear the commit stack on focus change — committed words should not
    // persist across application switches (prevents cross-app keystroke leakage).
    CancelCommitUndo(); // ← add this line
}
```

> `SecureZeroMemory` is the Windows equivalent of `explicit_bzero`. It zeroes the buffer
> before `clear()` releases it, so the data does not remain accessible in freed heap memory.

---

## FIX-7 — Validate ZIP Path in `--install-update`

### Affected file
- `src/app/main.cpp` — `--install-update` command-line handler

### Why it matters
The `--install-update` CLI argument accepts an arbitrary file path. A malicious process can
replace the legitimate update ZIP in `%TEMP%` with a crafted archive, then invoke NexusKey
with the path to their file. Since the installer copies files directly over `NextKeyTSF.dll`
(which loads into all TSF apps), this achieves system-wide DLL injection.

Restricting the accepted path to `%TEMP%` eliminates this: the malware would need to race
against the legitimate downloader in the same temp directory, which is a much harder attack.

### Fix

```cpp
// In main.cpp, --install-update handler, after extracting zipPath:

// Validate that the zip is inside %TEMP% — reject arbitrary paths
wchar_t tempDir[MAX_PATH] = {};
GetTempPathW(MAX_PATH, tempDir);
// Normalize: GetTempPath guarantees trailing backslash
std::wstring tempDirStr(tempDir);

// Case-insensitive prefix check (Windows paths are case-insensitive)
auto zipLower  = zipPath;
auto tempLower = tempDirStr;
std::transform(zipLower.begin(),  zipLower.end(),  zipLower.begin(),  ::towlower);
std::transform(tempLower.begin(), tempLower.end(), tempLower.begin(), ::towlower);

if (zipLower.substr(0, tempLower.size()) != tempLower) {
    NEXTKEY_LOG(L"[main] --install-update rejected: path '%s' is not inside TEMP", zipPath.c_str());
    return 1;
}

RunUpdateInstaller(zipPath);
```

---

## Testing Each Fix

| Fix | How to verify |
|-----|---------------|
| FIX-1 | Run `Process Hacker` → Object Explorer → find `NexusKeySharedState` → check Security tab shows only SYSTEM + current user |
| FIX-2 | Create a `config.toml` with a 2 MB `[macros]` section in `%APPDATA%\NexusKey\` → restart NexusKey → should log warning and use defaults, not crash |
| FIX-3 | Manually create `HKCU\Software\Classes\CLSID\{guid}\InprocServer32` pointing to `notepad.exe` → start NexusKey → verify the key is removed on startup |
| FIX-4 | In a debug build, set a breakpoint in `ApplySharedState`, manually set `state.inputMethod = 0xFF` → verify log message and Telex fallback, no crash |
| FIX-5 | No regression in `SharedStateTest.cpp`. Run existing tests — all must pass. |
| FIX-6 | Type a word, commit with space, click another app → verify `commitStack_` is empty (add temporary debug log if needed) |
| FIX-7 | Run `NexusKey.exe "--install-update" "C:\Windows\evil.zip"` → should log rejection and exit, not run installer |

---

## What Was Intentionally Skipped

| Issue | Reason skipped |
|-------|----------------|
| TLS certificate pinning | User-triggered update, not background auto-update. Overkill for local IME. |
| Authenticode code signing | Requires purchasing a code signing certificate (~$200/yr). Track as future work when distribution scales. |
| `schtasks` parameter escaping | Requires attacker to already control the exe path — if they're there, other vectors are available. |
| Temp file name randomization | Attack window is milliseconds for a user-triggered download. Not worth complexity. |
| Hotkey disclosure in shared memory | Hotkeys are not secrets. Low real-world privacy impact. |
