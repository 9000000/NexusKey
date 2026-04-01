# 9. Naming Conventions

> [!IMPORTANT]
> **KHÔNG copy tên biến từ code cũ.** Khi port từ VietType/OpenKey, PHẢI rename về NextKey conventions.

## 9.1 Basic Conventions

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

## 9.2 Rename Rules When Porting

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

## 9.3 Forbidden Patterns

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

## 9.4 Boolean Naming

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

## 9.5 Struct/Class Field Naming

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

# 10. Documentation

## 10.1 Public API Comments

```cpp
/// @brief Process a single key press
/// @param keyCode Virtual key code (VK_*)
/// @param modifiers Shift/Ctrl/Alt state
/// @return Result indicating if key was consumed
/// @note Thread-safe, can be called from any thread
virtual EngineResult ProcessKey(uint16_t keyCode, uint32_t modifiers) = 0;
```

## 10.2 Why Comments

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

# Summary Checklist

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
