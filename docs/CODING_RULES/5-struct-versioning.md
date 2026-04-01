# 5. Struct Versioning

## 5.1 Memory-Mapped Structs

```cpp
#pragma pack(push, 1)
struct SharedState {
    uint32_t magic;          // 'NKEY' = 0x59454B4E
    uint32_t structVersion;  // Increment on layout change
    uint32_t structSize;     // sizeof(SharedState)
    
    // ... fields ...
    
    uint8_t reserved[512];   // Future expansion
};
#pragma pack(pop)

// ✅ REQUIRED: Validate on load
bool ValidateSharedState(const SharedState* state) {
    if (state->magic != 0x59454B4E) return false;
    if (state->structSize != sizeof(SharedState)) return false;
    if (state->structVersion > CURRENT_VERSION) return false;
    return true;
}
```

## 5.2 SharedState vs TOML: When to Use Which

```
SharedState (instant, 0 IO)     TOML (deferred 30s, persisted)
─────────────────────────────   ─────────────────────────────
Fixed-size, small values:       Variable-size data:
  • Feature flag bools            • Excluded apps list
  • Input method, spell check     • TSF apps list
  • Code table                    • Macro table
  • Hotkey config (packed)        • Per-app code table map
  • Convert hotkey (packed)

Rule: if it fits in a few bytes → SharedState. If variable-size → TOML + re-signal.
```

SharedState packs feature bools into a 3-byte bitmask (24 flags max):
- Bits 0-15: `featureFlags[2]` (core flags)
- Bits 16-23: `extFeatureFlags` (extended flags, 7 bits remaining)
- Access via `GetFeatureFlags()` / `SetFeatureFlags()` (returns/takes `uint32_t`)
- Hotkeys: `SetHotkey()`/`GetHotkey()` pack 4 bools + wchar_t into 3 bytes

```cpp
// Adding a new toggle (7-step checklist):
// 1. Add constant to FeatureFlags namespace (SharedState.h)
// 2. Add bool to TypingConfig (TypingConfig.h)
// 3. Add encode/decode in EncodeFeatureFlags/DecodeFeatureFlags (SharedState.h)
// 4. Add load/save in ConfigManager (ConfigManager.cpp)
// 5. Add UI handler in SettingsDialog (SettingsDialog.cpp)
// 6. Add ApplyConfig line in HookEngine (HookEngine.cpp)
// 7. ⚠️  Add field copy in SharedStateManager::Write() (SharedStateManager.cpp)
//    Write() copies field-by-field (NOT memcpy). Read() uses full struct copy.
//    Missing step 7 = toggle works in Debug but FAILS in Release!
```

---
