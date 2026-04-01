# 8. TSF-Specific Rules

## 8.1 Edit Session Safety

```cpp
// ✅ REQUIRED: All text modifications via edit session
HRESULT ModifyText(ITfContext* pContext) {
    // Request sync edit session
    return pContext->RequestEditSession(
        clientId_,
        pEditSession,
        TF_ES_SYNC | TF_ES_READWRITE,
        &hrSession
    );
}

// ❌ FORBIDDEN: Direct text manipulation
pRange->SetText(...);  // May crash other apps
```

## 8.2 Composition Lifecycle

```cpp
// ✅ Always clean up composition on deactivate
void OnDeactivate() {
    if (compositionManager_.IsComposing()) {
        compositionManager_.EndComposition();
    }
    engine_->Reset();
}
```

---
