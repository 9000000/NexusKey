# 1. Namespace & Organization

## 1.1 Namespace Hierarchy

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

## 1.2 Header Organization

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

## 1.3 No `using namespace` in Headers

```cpp
// ❌ FORBIDDEN in .h files
using namespace std;

// ✅ REQUIRED: Explicit prefixes
std::wstring result;
std::unique_ptr<Engine> engine;
```

---
