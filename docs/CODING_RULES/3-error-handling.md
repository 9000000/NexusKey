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

## 3.4 Async User Actions Must Have UI Feedback

Every user-triggered async operation (button click → background thread) **MUST** provide:

1. **Loading state**: Disable control + show progress text before spawning thread
2. **Error state**: On failure, show **persistent** UI feedback (not just a transient toast)
3. **Restore state**: On completion (success or error), re-enable the control

```cpp
// ❌ BAD: No feedback — user clicks, nothing visible happens for seconds
void onCheckUpdate() {
    std::thread([]() {
        auto info = CheckForUpdate();  // takes 3-5 seconds
        if (!info.ok) {
            ToastPopup::Show(L"Failed", 3000);  // disappears, user misses it
        }
    }).detach();
}

// ✅ GOOD: Loading → result → restore
void onCheckUpdate() {
    setButtonLoading(true, L"Checking...");       // 1. Immediate feedback
    std::thread([hwnd]() {
        auto info = CheckForUpdate();
        if (!info.ok) {
            PostMessage(hwnd, WM_RESULT, ERROR, 0);  // 2. Post back to UI thread
        }
    }).detach();
}
// In WM_RESULT handler:
//   setButtonLoading(false);                      // 3. Restore
//   setStatusText(L"Failed", /*isError=*/true);   // 2. Persistent error
```

**Why**: A background thread with no visual feedback looks like the app froze or ignored the click. A 3-second toast is easily missed — errors that require user action need persistent display.

## 3.5 `[[nodiscard]]` Return Values

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
