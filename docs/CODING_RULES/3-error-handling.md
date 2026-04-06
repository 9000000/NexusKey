# 3. Error Handling

## 3.1 Never Block User

```cpp
// ❌ FORBIDDEN: Blocking dialogs on error
MessageBox(nullptr, L"Error!", L"NextKey", MB_OK);

// ✅ REQUIRED: Async notification + fallback
NotificationManager::ShowToast(L"NextKey", L"Config error, using defaults");
return DefaultConfig();
```

## 3.2 Fallback Chain

```cpp
TypingConfig LoadConfig() {
    // 1. Try SharedMemory
    if (auto config = SharedStateManager::Get()) {
        return config->typing;
    }
    
    // 2. Try TOML file
    if (auto config = LoadFromToml()) {
        return *config;
    }
    
    // 3. Return defaults (never fail)
    return TypingConfig::Default();
}
```

## 3.3 HRESULT Handling

```cpp
// ✅ Check and log, but don't crash
HRESULT hr = pContext->GetSelection(...);
if (FAILED(hr)) {
    LOG_WARNING(L"GetSelection failed: 0x%08X", hr);
    return S_OK;  // Graceful degradation
}
```

## 3.4 `[[nodiscard]]` Return Values

MSVC treats discarded `[[nodiscard]]` as error (`/WX`). Always handle or explicitly discard:

```cpp
// ❌ Triggers C4834 → error C2220
floatingIcon.Create(hInstance);

// ✅ Explicitly discard with (void) cast
(void)floatingIcon.Create(hInstance);

// ✅ Or check the result
if (!floatingIcon.Create(hInstance)) {
    NEXTKEY_LOG(L"FloatingIcon creation failed");
}
```

---
