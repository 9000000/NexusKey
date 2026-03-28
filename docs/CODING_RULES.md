# NextKey Coding Rules

> **Purpose:** Coding standards và best practices cho NextKey hybrid IME development.  
> **Applies to:** All C++ code in `src/` directory.

---

## 1. Namespace & Organization

### 1.1 Namespace Hierarchy

```cpp
// REQUIRED: All code MUST be under NextKey namespace
namespace NextKey {
// SharedState, ConfigManager, TelexEngine, VniEngine, EngineFactory, etc.
// Sub-namespaces used for type isolation:
//   NextKey::Telex, NextKey::Vni, NextKey::SpellCheck,
//   NextKey::CodeTableConverter, NextKey::TSF
// Constants-only sub-namespaces: SharedFlags, FeatureFlags
}
```

### 1.2 Header Organization

```cpp
// Order of includes (with blank line between groups)
#include "pch.h"              // Precompiled header first

#include "OwnHeader.h"        // The .h for this .cpp

#include <windows.h>          // System headers
#include <msctf.h>

#include <memory>             // STL headers
#include <string>

#include "core/engine/..."    // Project headers (relative to src/)
```

### 1.3 No `using namespace` in Headers

```cpp
// ❌ FORBIDDEN in .h files
using namespace std;

// ✅ REQUIRED: Explicit prefixes
std::wstring result;
std::unique_ptr<Engine> engine;
```

---

## 2. Memory & Resource Management

### 2.1 Smart Pointers

```cpp
// ✅ Preferred: RAII everywhere
std::unique_ptr<IInputEngine> engine = std::make_unique<TelexEngine>();

// ❌ Avoid: Raw pointers for ownership
IInputEngine* engine = new TelexEngine();  // Who deletes?
```

### 2.2 COM Objects

```cpp
// ✅ Use ATL smart pointers for COM
CComPtr<ITfDocumentMgr> documentMgr;
CComPtr<ITfContext> context;

// ❌ Avoid: Manual AddRef/Release
ITfContext* pContext = nullptr;  // Easy to leak
```

### 2.3 Windows Handles

```cpp
// ✅ Use RAII wrappers or explicit cleanup
class HandleGuard {
    HANDLE handle_;
public:
    ~HandleGuard() { if (handle_) CloseHandle(handle_); }
};

// ❌ Avoid: Unguarded handles
HANDLE hFile = CreateFile(...);
// ... forget to close ...
```

---

## 3. Error Handling

### 3.1 Never Block User

```cpp
// ❌ FORBIDDEN: Blocking dialogs on error
MessageBox(nullptr, L"Error!", L"NextKey", MB_OK);

// ✅ REQUIRED: Async notification + fallback
NotificationManager::ShowToast(L"NextKey", L"Config error, using defaults");
return DefaultConfig();
```

### 3.2 Fallback Chain

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

### 3.3 HRESULT Handling

```cpp
// ✅ Check and log, but don't crash
HRESULT hr = pContext->GetSelection(...);
if (FAILED(hr)) {
    LOG_WARNING(L"GetSelection failed: 0x%08X", hr);
    return S_OK;  // Graceful degradation
}
```

---

## 4. Interface-Based Design

### 4.1 Engine Interface

```cpp
// ✅ REQUIRED: All engines implement IInputEngine
class TelexEngine : public IInputEngine { ... };
class VniEngine : public IInputEngine { ... };

// ✅ TSF/Hook use interface, not concrete type
class TextService {
    std::unique_ptr<IInputEngine> engine_;  // Swappable!
};
```

### 4.2 Dependency Injection

```cpp
// ✅ Prefer: Constructor injection
class KeyEventSink {
public:
    explicit KeyEventSink(IInputEngine& engine) : engine_(engine) {}
private:
    IInputEngine& engine_;
};

// ❌ Avoid: Global/static engines
static TelexEngine g_engine;  // Hard to test, can't swap
```

---

## 5. Struct Versioning

### 5.1 Memory-Mapped Structs

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

### 5.2 SharedState vs TOML: When to Use Which

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

## 6. Logging

### 6.1 Log Levels

```cpp
// Use appropriate levels
LOG_DEBUG(L"Key pressed: 0x%02X", vkCode);     // Dev only
LOG_INFO(L"Engine initialized");                // Normal operation
LOG_WARNING(L"SharedMemory unavailable");       // Degraded but working
LOG_ERROR(L"TSF registration failed: 0x%08X"); // Needs attention
```

### 6.2 Process Context

```cpp
// ✅ Include process name in TSF DLL logs
LOG_INFO(L"[%s] TextService activated", GetCurrentProcessName());

// Output: "[notepad.exe] TextService activated"
```

### 6.3 Performance

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

## 7. Debug Architecture

### 7.1 Compile-Time Debug Macros

```cpp
// Debug code MUST be wrapped in NEXTKEY_DEBUG
#ifdef NEXTKEY_DEBUG
    // This code ONLY exists in debug builds
    NEXTKEY_LOG(L"ProcessKey: vk=0x%02X, state=%d", vkCode, state);
#endif

// Or use the macro that auto-compiles out
NEXTKEY_LOG(L"Key processed");  // Becomes ((void)0) in release
```

### 7.2 Never Block in Debug Code

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

### 7.3 Debug Build Configuration

```cpp
// Debug builds should define:
// - NEXTKEY_DEBUG       : Enable debug logging/inspection
// - _DEBUG              : Standard MSVC debug mode

// Release builds:
// - NDEBUG              : Disable asserts
// - No NEXTKEY_DEBUG    : All debug code compiles out
```

### 7.1 SharedState Access

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

### 7.2 Engine State

```cpp
// ✅ Each TSF instance owns its engine (no sharing)
class TextService {
    std::unique_ptr<IInputEngine> engine_;  // Per-instance
};

// ❌ Avoid: Shared engine state across threads
static IInputEngine* g_sharedEngine;  // Race conditions!
```

---

## 8. TSF-Specific Rules

### 8.1 Edit Session Safety

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

### 8.2 Composition Lifecycle

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

## 9. Naming Conventions

> [!IMPORTANT]
> **KHÔNG copy tên biến từ code cũ.** Khi port từ VietType/OpenKey, PHẢI rename về NextKey conventions.

### 9.1 Basic Conventions

| Element | Convention | Example |
|---------|------------|---------|
| Classes | PascalCase | `TelexEngine`, `SharedStateManager` |
| Methods | PascalCase | `ProcessKey()`, `GetComposition()` |
| Local variables | camelCase | `inputMethod`, `keyCode` |
| Member variables | camelCase + trailing_ | `engine_`, `configVersion_` |
| Constants | UPPER_SNAKE | `FEATURE_MACRO`, `MAX_COMPOSITION_LEN` |
| Enums (type) | PascalCase | `InputMethod`, `EngineFeature` |
| Enums (values) | PascalCase | `InputMethod::Telex`, `EngineFeature::SpellCheck` |
| Interfaces | IPrefix | `IInputEngine`, `IFeatureProcessor` |
| Template params | Single uppercase | `T`, `TConfig` |

### 9.2 Rename Rules When Porting

**VietType patterns → NextKey:**

| VietType (old) | NextKey (new) | Reason |
|----------------|---------------|--------|
| `_engine` | `engine_` | Trailing underscore for members |
| `_threadMgr` | `threadMgr_` | Consistent member naming |
| `_clientId` | `clientId_` | Consistent member naming |
| `TelexConfig` | `TypingConfig` | Unified config name |
| `TelexStates` | `EngineState` (internal) | Generic for all engines |
| `VietType::` | `NextKey::` | Namespace change |

**OpenKey patterns → NextKey:**

| OpenKey (old) | NextKey (new) | Reason |
|---------------|---------------|--------|
| `vLanguage` | `isVietnameseMode` | Descriptive, no `v` prefix |
| `vInputType` | `inputMethod` | Clear meaning |
| `vFreeMark` | `allowFreeTonePosition` | Self-documenting |
| `vQuickTelex` | `quickTelexEnabled` | Boolean naming |
| `vCheckSpelling` | `spellCheckEnabled` | Boolean naming |
| `vUseMacro` | `macroEnabled` | Boolean naming |
| `hBPC` | `backspaceCount` | Clear meaning |
| `hNCC` | `characterCount` | Clear meaning |
| `vCode` | `outputCode` | Clear meaning |

### 9.3 Forbidden Patterns

```cpp
// ❌ FORBIDDEN: Hungarian notation prefixes
int vLanguage;       // "v" prefix
int hBPC;            // Cryptic abbreviation
HANDLE hFile;        // "h" for handle OK in Win32 API, but not internal vars

// ✅ REQUIRED: Descriptive names
int languageMode;
int backspaceCount;
HANDLE configFile;   // Or use RAII wrapper

// ❌ FORBIDDEN: Single letters (except loop counters)
int c;               // What is this?
wchar_t k;           // Key? Char? Unknown

// ✅ REQUIRED: Full names
int characterCode;
wchar_t keyChar;

// ❌ FORBIDDEN: Abbreviations without context
int idx;             // Index of what?
int cnt;             // Count of what?

// ✅ REQUIRED: Contextual names
int compositionIndex;
int keystrokeCount;
```

### 9.4 Boolean Naming

```cpp
// ✅ GOOD: Reads like a question
bool isComposing;
bool hasOutput;
bool shouldCommitPending;
bool macroEnabled;
bool spellCheckEnabled;

// ❌ BAD: Unclear
bool flag;
bool status;
bool check;
bool vUseMacro;  // "v" prefix adds nothing
```

### 9.5 Struct/Class Field Naming

```cpp
// ✅ GOOD: Consistent member style
struct TypingConfig {
    InputMethod inputMethod;       // camelCase
    bool spellCheckEnabled;        // Boolean with "Enabled"
    bool oaUyTone1;               // Feature flag, descriptive
    uint32_t featureFlags;         // camelCase
};

class TextService {
    TfClientId clientId_;          // trailing_ for members
    std::unique_ptr<IInputEngine> engine_;
    CComPtr<ITfThreadMgr> threadMgr_;
};

---

## 10. Documentation

### 10.1 Public API Comments

```cpp
/// @brief Process a single key press
/// @param keyCode Virtual key code (VK_*)
/// @param modifiers Shift/Ctrl/Alt state
/// @return Result indicating if key was consumed
/// @note Thread-safe, can be called from any thread
virtual EngineResult ProcessKey(uint16_t keyCode, uint32_t modifiers) = 0;
```

### 10.2 Why Comments

```cpp
// ✅ Explain WHY, not WHAT
// Debounce config reload to avoid thrashing when user
// rapidly switches between windows
if (GetTickCount64() - lastReload_ < 100) {
    return;
}

// ❌ Avoid: Obvious comments
// Increment counter
counter++;
```

---

## Summary Checklist

Before committing code, verify:

- [ ] All code under `NextKey::` namespace
- [ ] No `using namespace` in headers
- [ ] Smart pointers for ownership
- [ ] Errors don't block user (async notify + fallback)
- [ ] Engines implement `IInputEngine`
- [ ] Memory-mapped structs have `reserved[]` fields
- [ ] TSF text changes via edit sessions only
- [ ] Logs include process name for TSF code
- [ ] WHY comments for non-obvious logic
