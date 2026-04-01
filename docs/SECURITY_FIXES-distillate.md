---
type: bmad-distillate
sources:
  - "SECURITY_FIXES.md"
downstream_consumer: "NexusKey coding and security review"
created: "2026-04-01"
token_estimate: 1150
parts: 1
---

## Threat Model
- Local Vietnamese IME on personal Windows; primary attacker = malicious user-session software (no admin); network MITM = low priority

## Fix Summary
- **FIX-1** Named objects without DACL — pre-create attack; 1h effort
- **FIX-2** Config file no size limits — OOM crash via large config.toml; 30min
- **FIX-3** HKCU CLSID override — DLL injection into all TSF apps; 30min
- **FIX-4** Shared memory enum validation — UB/crash from out-of-range cast; 15min
- **FIX-5** Seqlock barrier wrong position — data corruption race; 5min
- **FIX-6** Keystroke buffer not cleared on focus — cross-app keystroke leakage; 10min
- **FIX-7** `--install-update` accepts arbitrary zip path — system-wide DLL injection; 10min

## Intentionally Skipped
- TLS cert pinning (user-triggered update, overkill); Authenticode signing (~$200/yr cert needed); schtasks escaping (attacker already controls exe path); temp file name randomization (ms-window race); hotkey disclosure in shared memory (not secrets)

## FIX-1 — Restricted DACL on Named Objects
- **Files**: `src/core/ipc/SharedStateManager.cpp` (CreateFileMappingW); `src/core/config/ConfigEvent.cpp` (CreateEventW); `src/app/main.cpp` (CreateMutexW)
- **Vulnerability**: `nullptr` security attrs → default DACL → any session process can pre-create `Local\NexusKeySharedState`, write arbitrary shared memory values, or signal config event to force reload from malware-written config.toml
- **Fix**: Create `src/core/ipc/SecurityHelpers.h` with `MakeCreatorOnlySecurityAttributes()` returning SDDL `D:PAI(A;;GA;;;SY)(A;;GA;;;CO)` — SYSTEM + Creator/Owner only; call `LocalFree(sa.lpSecurityDescriptor)` after handle creation; graceful fallback if ConvertString fails (pSD stays nullptr → default DACL)
- **Pattern**: `auto sa = NextKey::MakeCreatorOnlySecurityAttributes(); handle = CreateXxxW(..., &sa, ...); if (sa.lpSecurityDescriptor) LocalFree(sa.lpSecurityDescriptor);`
- **Mutex name**: `L"Local\\NexusKey_Main_Mutex"`

## FIX-2 — Config File Size/Length Guards
- **File**: `src/core/config/ConfigManager.cpp`
- **Vulnerability**: `toml::parse_file()` unbounded; malicious 100MB macro value → `vector<INPUT>` OOM crash; trivially triggered via config event signal (FIX-1)
- **Limits**: `kMaxConfigFileSizeBytes = 1MB`; `kMaxMacroKeyLen = 32`; `kMaxMacroValueLen = 512`; `kMaxPerAppEntries = 256`
- **Behavior**: File too large → log warning, use compiled defaults; oversized macro entries → skip (continue); per-app table overflow → truncate (break)

## FIX-3 — Remove HKCU CLSID Override at Startup
- **Files**: `src/tsf/Register.cpp` or `src/app/main.cpp`
- **Vulnerability**: Any user process writes `HKCU\Software\Classes\CLSID\{NexusKey-GUID}\InprocServer32` → HKEY_CLASSES_ROOT merges HKCU over HKLM → attacker DLL loaded into all TSF apps (Word, Chrome, etc.); no admin required
- **Fix**: `CleanupHkcuClsidOverride()` — `RegOpenKeyExW(HKEY_CURRENT_USER, keyPath, ...)` → if exists, `RegDeleteTreeW` entire subtree; call from `DllRegisterServer()` and `WinMain` startup
- **Key path**: `Software\Classes\CLSID\%s` (CLSID from Globals.cpp)

## FIX-4 — Validate Shared Memory Enums
- **File**: `src/tsf/EngineController.cpp` — `ApplySharedState()`
- **Vulnerability**: Out-of-range `inputMethod` (e.g., 0xFF) cast to enum → UB, crash, wrong engine
- **Validation**: `inputMethod` valid 0–2, default `InputMethod::Telex`; `optimizeLevel` valid 0–2, default 0; `codeTable` valid 0–4 (guard needed)

## FIX-5 — Seqlock Barrier Order
- **File**: `src/core/ipc/SharedStateManager.cpp` — `Read()`
- **Bug**: Barrier placed AFTER epoch read → ARM can speculate stale epoch → retry passes on corrupted data
- **Fix**: Read epoch via `volatile const SharedState*` cast; `std::atomic_thread_fence(std::memory_order_acquire)` BEFORE data copy (replaces `MemoryBarrier()`); same fence after data copy, before second epoch read; check `before == after && (before & 1) == 0`
- **Note**: x86 TSO makes original functionally safe; fix for correctness + future ARM

## FIX-6 — Clear Keystroke Buffer on Focus Change
- **File**: `src/app/system/HookEngine.cpp` — `ResetComposition()`
- **Bug**: `inputHistory_` cleared but `commitStack_` persists across app switches → passwords readable from memory by injected DLL
- **Fix**: `SecureZeroMemory(inputHistory_.data(), inputHistory_.size() * sizeof(wchar_t))` before `clear()`; add `CancelCommitUndo()` call to clear commitStack_

## FIX-7 — Validate ZIP Path for `--install-update`
- **File**: `src/app/main.cpp`
- **Vulnerability**: Arbitrary zip path → malware replaces update ZIP → copies over `NextKeyTSF.dll` → system-wide DLL injection
- **Fix**: Case-insensitive prefix check that zip path starts with `GetTempPathW()` result; reject + log + return 1 if outside %TEMP%
- **Pattern**: `std::transform` to lowercase both paths; `zipLower.substr(0, tempLower.size()) != tempLower` → reject

## Verification
- FIX-1: Process Hacker → Object Explorer → NexusKeySharedState Security tab shows only SYSTEM + current user
- FIX-2: 2MB `[macros]` in config.toml → log warning, use defaults, no crash
- FIX-3: Create `HKCU\...\CLSID\{guid}\InprocServer32` → notepad.exe → start NexusKey → key removed
- FIX-4: Debug breakpoint, set `state.inputMethod = 0xFF` → log + Telex fallback, no crash
- FIX-5: Existing `SharedStateTest.cpp` tests must pass
- FIX-6: Type word → commit → switch app → verify `commitStack_` empty
- FIX-7: `NexusKey.exe "--install-update" "C:\Windows\evil.zip"` → rejection log, no installer run
