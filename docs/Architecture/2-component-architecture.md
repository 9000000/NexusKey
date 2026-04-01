# 2. Component Architecture

### 2.1 System Overview

```
┌─────────────────────────────────────────────────────────────────┐
│                         USER SPACE                               │
│                                                                  │
│  ┌────────────────────────────────────────────────────────────┐ │
│  │                     NexusKey Core (EXE)                     │ │
│  │  ┌──────────────┐  ┌──────────────┐  ┌──────────────────┐  │ │
│  │  │ Settings UI  │  │ Config Mgr   │  │ Shared Memory    │  │ │
│  │  │ (Sciter)     │  │ (TOML R/W)   │  │ Writer           │  │ │
│  │  └──────────────┘  └──────────────┘  └──────────────────┘  │ │
│  └────────────────────────────────────────────────────────────┘ │
│         │                    │                    │              │
│         │ UI Events          │ File I/O           │ Memory-map   │
│         ▼                    ▼                    ▼              │
│  ┌─────────────┐      ┌─────────────┐      ┌─────────────┐      │
│  │ config.toml │      │ Named Event │      │ SharedState │      │
│  │ (on disk)   │      │ (signal)    │      │ (56 bytes)  │      │
│  └─────────────┘      └─────────────┘      └─────────────┘      │
│         │                    │                    │              │
│         └────────────────────┼────────────────────┘              │
│                              │                                   │
│  ┌───────────────────────────┼───────────────────────────────┐  │
│  │              TSF Engine (DLL, per-process)                 │  │
│  │                           │                                │  │
│  │    ┌──────────────────────▼───────────────────────────┐   │  │
│  │    │              Config Snapshot                      │   │  │
│  │    │         (immutable during word)                   │   │  │
│  │    └───────────────────────────────────────────────────┘   │  │
│  │                           │                                │  │
│  │    ┌──────────────────────▼───────────────────────────┐   │  │
│  │    │           Transformation Engine                   │   │  │
│  │    │    (Telex/VNI rules, tone logic, buffer)         │   │  │
│  │    └───────────────────────────────────────────────────┘   │  │
│  └────────────────────────────────────────────────────────────┘ │
│                                                                  │
└─────────────────────────────────────────────────────────────────┘
```

### 2.2 Component Responsibilities

| Component | Responsibility | Does NOT Do |
|-----------|----------------|-------------|
| **Core (EXE)** | Config management, UI, system tray | Key processing, text transformation |
| **TSF Engine (DLL)** | Key capture, Vietnamese transformation, text output | Config storage, UI display |
| **Hook Engine (DLL)** | Fallback for non-TSF apps | Primary input method |
| **config.toml** | Persistent config storage | Runtime state |
| **SharedState** | Runtime signals between Core ↔ Engine | Config data, typing state |

---
