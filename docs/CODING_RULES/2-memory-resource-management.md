# 2. Memory & Resource Management

## 2.1 Smart Pointers

```cpp
// ✅ Preferred: RAII via factory (Path G unified TypingEngine)
std::unique_ptr<IInputEngine> engine = EngineFactory::Create(config);

// ❌ Avoid: Raw pointers for ownership
IInputEngine* engine = new TypingEngine(config);  // Who deletes?
```

## 2.2 COM Objects

```cpp
// ✅ Use ATL smart pointers for COM
CComPtr<ITfDocumentMgr> documentMgr;
CComPtr<ITfContext> context;

// ❌ Avoid: Manual AddRef/Release
ITfContext* pContext = nullptr;  // Easy to leak
```

## 2.3 Windows Handles

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
