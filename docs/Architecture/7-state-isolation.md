# 7. State Isolation

### 7.1 What Lives Where

| State | Location | Lifetime | Shared? |
|-------|----------|----------|---------|
| Input method rules | Engine memory (from TOML) | Until config reload | No |
| Current word buffer | Engine memory | Until commit | No |
| Composition state | Engine memory | Until commit | No |
| User intent | Engine memory | Until commit | No |
| Config epoch | Shared memory | Core lifetime | Yes (read-only for engine) |
| Core liveness | Shared memory | Core lifetime | Yes (read-only for engine) |

### 7.2 Typing State Never Leaves Engine

```
┌─────────────────────────────────────────────┐
│              ENGINE (DLL)                   │
│                                             │
│  ┌───────────────────────────────────────┐  │
│  │         PRIVATE STATE                 │  │
│  │  • keyBuffer: "vieetj"                │  │
│  │  • composition: "việt"                │  │
│  │  • tonePosition: 2                    │  │
│  │  • engineState: COMPOSING             │  │
│  └───────────────────────────────────────┘  │
│                    │                        │
│                    │ NEVER EXPOSED          │
│                    ▼                        │
│              ┌──────────┐                   │
│              │ Firewall │                   │
│              └──────────┘                   │
│                                             │
└─────────────────────────────────────────────┘
          │
          │ Only output: committed text
          ▼
    ┌─────────────┐
    │ Application │
    └─────────────┘
```

---
