# 1. Core Design Principles

### 1.1 The Sacred Rule

```
┌─────────────────────────────────────────────────────────┐
│                                                         │
│   "The Core's death must not cause                      │
│    the user's intent to die as well."                   │
│                                                         │
└─────────────────────────────────────────────────────────┘
```

The IME is **system infrastructure**, not an application. Users expect typing to work unconditionally.

### 1.2 Architectural Tenets

| Tenet | Meaning |
|-------|---------|
| **Engine Autonomy** | Engines work without Core running |
| **Word Boundary Sanctity** | Never interrupt mid-word; sync only at boundaries |
| **Graceful Degradation** | Failures reduce features, never block typing |
| **Signals, Not Data** | Shared memory carries hints, not content |
| **Config as Snapshot** | Engines hold immutable config copies, not live references |

---
