# 6. Logging

## 6.1 Log Levels

```cpp
// Use appropriate levels
LOG_DEBUG(L"Key pressed: 0x%02X", vkCode);     // Dev only
LOG_INFO(L"Engine initialized");                // Normal operation
LOG_WARNING(L"SharedMemory unavailable");       // Degraded but working
LOG_ERROR(L"TSF registration failed: 0x%08X"); // Needs attention
```

## 6.2 Process Context

```cpp
// ✅ Include process name in TSF DLL logs
LOG_INFO(L"[%s] TextService activated", GetCurrentProcessName());

// Output: "[notepad.exe] TextService activated"
```

## 6.3 Performance

```cpp
// ❌ Avoid: Logging in hot paths
void OnKeyDown(WPARAM vkCode) {
    LOG_DEBUG(L"Key: %d", vkCode);  // 100+ calls/sec = slow!
}

// ✅ Prefer: Conditional or sampled logging
#ifdef NEXTKEY_DEBUG
    if (g_logLevel >= LOG_LEVEL_DEBUG) { ... }
#endif
```

---
