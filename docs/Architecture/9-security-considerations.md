# 9. Security Considerations

### 9.1 Shared Memory Permissions

```c
// Create with restricted access
SECURITY_ATTRIBUTES sa = { sizeof(sa), NULL, FALSE };
// TODO: Set DACL to allow only same-user access

HANDLE hMapFile = CreateFileMapping(
    INVALID_HANDLE_VALUE,
    &sa,
    PAGE_READWRITE,
    0,
    sizeof(SharedState),
    L"Local\\NexusKeySharedState"  // Local = same session only
);
```

### 9.2 Config File Permissions

- Location: `%APPDATA%\NexusKey\config.toml`
- Permissions: User-only read/write
- Never store credentials or sensitive data

### 9.3 Named Event Security

```c
// Use Local\ prefix for session isolation
L"Local\\NexusKeyConfigReady"

// Or Global\ with proper DACL for cross-session (admin tools)
L"Global\\NexusKeyConfigReady"
```

---
