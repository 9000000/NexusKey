# 6. Logging

> Production uses a single macro `NEXTKEY_LOG` (defined in `src/core/Debug.h:20`) which compiles to `((void)0)` in release builds — see Rule 7.1. Hot-path code additionally uses `HOOK_LOG` (rate-limited variant). Crash-recovery logs use `CrashLog` (Rule 11.5).

## 6.1 Log Usage

```cpp
// ✅ Use NEXTKEY_LOG everywhere — compiled out in release
NEXTKEY_LOG(L"Engine initialized (method=%d)", static_cast<int>(method));
NEXTKEY_LOG(L"SharedMemory unavailable, falling back to defaults");
NEXTKEY_LOG(L"TSF registration failed: 0x%08X", hr);

// ✅ Hook hot path: HOOK_LOG (compiled out in release; rate-limited in debug)
HOOK_LOG(L"FOCUS changed — resetting composition (count=%zu)", engine_->Count());
```

## 6.2 Process Context

```cpp
// ✅ Include process name in TSF DLL logs (the DLL is loaded per-process)
NEXTKEY_LOG(L"[%s] TextService activated", GetCurrentProcessName());

// Output (in debug build): "[notepad.exe] TextService activated"
```

## 6.3 Performance

```cpp
// ❌ Avoid: Logging in hot paths without the rate-limited macro
void LowLevelKeyboardProc(...) {
    NEXTKEY_LOG(L"Key: %d", vkCode);  // 100+ calls/sec when debug build is in use
}

// ✅ Prefer: HOOK_LOG (rate-limited) or guard by event significance
HOOK_LOG(L"  TOGGLE-DOWN (vk=0x%02X)", vkCode);  // only on rare events

// ✅ Release builds compile all NEXTKEY_LOG / HOOK_LOG to ((void)0) — zero cost
//    so the macro itself is safe in hot paths; only debug build pays.
```

---
