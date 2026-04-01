# 4. Interface-Based Design

## 4.1 Engine Interface

```cpp
// ✅ REQUIRED: All engines implement IInputEngine
class TelexEngine : public IInputEngine { ... };
class VniEngine : public IInputEngine { ... };

// ✅ TSF/Hook use interface, not concrete type
class TextService {
    std::unique_ptr<IInputEngine> engine_;  // Swappable!
};
```

## 4.2 Dependency Injection

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
