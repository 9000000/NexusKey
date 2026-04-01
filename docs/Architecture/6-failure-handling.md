# 6. Failure Handling

### 6.1 Core Crash Behavior

```
Core Running          Core Crashes           Engine Behavior
─────────────         ────────────           ───────────────
     │                     │                       │
     │                     X                       │
     │                                             │
     │                                      Continue typing
     │                                      with current
     │                                      config snapshot
     │                                             │
     │                                      No reconnection
     │                                      attempts
     │                                             │
     │                                      No user
     │                                      notification
     │                                             │
Core Restarts ─────────────────────────────────────┤
     │                                             │
     │                                      At next word
     │                                      boundary: pick
     │                                      up new config
```

### 6.2 Failure Matrix

| Failure | Engine Response | User Impact |
|---------|-----------------|-------------|
| Core not running | Use config snapshot or defaults | None (typing works) |
| config.toml missing | Use compiled defaults | Reduced features |
| config.toml corrupt | Use compiled defaults | Reduced features |
| Shared memory unavailable | Skip runtime sync | Config changes delayed until restart |
| Named event fails | Fall back to file polling | Slightly delayed config updates |

### 6.3 Recovery Priority

```
1. USER TYPING ALWAYS WORKS (non-negotiable)
2. Features degrade gracefully
3. Errors logged but never shown to user during typing
4. Recovery happens silently at word boundaries
```

---
