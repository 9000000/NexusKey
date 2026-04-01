# 7. Debug Architecture

## 7.1 Compile-Time Debug Macros

```cpp
// Debug code MUST be wrapped in NEXTKEY_DEBUG
#ifdef NEXTKEY_DEBUG
    // This code ONLY exists in debug builds
    NEXTKEY_LOG(L"ProcessKey: vk=0x%02X, state=%d", vkCode, state);
#endif

// Or use the macro that auto-compiles out
NEXTKEY_LOG(L"Key processed");  // Becomes ((void)0) in release
```

## 7.2 Never Block in Debug Code

```cpp
// ❌ FORBIDDEN: Debug code that affects behavior
#ifdef NEXTKEY_DEBUG
    Sleep(100);  // Timing changes break production!
    MessageBox(nullptr, L"Debug", L"", MB_OK);  // Blocks user!
#endif

// ✅ ALLOWED: Logging and state inspection only
#ifdef NEXTKEY_DEBUG
    NEXTKEY_LOG(L"State: %s", GetStateName(state));
    auto info = engine.GetDebugInfo();  // Read-only
#endif
```

## 7.3 Debug Build Configuration

```cpp
// Debug builds should define:
// - NEXTKEY_DEBUG       : Enable debug logging/inspection
// - _DEBUG              : Standard MSVC debug mode

// Release builds:
// - NDEBUG              : Disable asserts
// - No NEXTKEY_DEBUG    : All debug code compiles out
```

## 7.1 SharedState Access

```cpp
// SharedState is read-heavy, write-rare
// ✅ Use simple version check, no locks for reads
void OnFocus() {
    uint32_t currentVersion = g_sharedState->configVersion;
    if (currentVersion != cachedVersion_) {
        ReloadConfig();
        cachedVersion_ = currentVersion;
    }
}
```

## 7.2 Engine State

```cpp
// ✅ Each TSF instance owns its engine (no sharing)
class TextService {
    std::unique_ptr<IInputEngine> engine_;  // Per-instance
};

// ❌ Avoid: Shared engine state across threads
static IInputEngine* g_sharedEngine;  // Race conditions!
```

---
