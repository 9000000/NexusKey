# NexusKey Runtime Architecture

> **Purpose:** Define the runtime behavior, communication patterns, and resilience principles for NexusKey IME.
> **Complements:** [Planning.md](./Planning.md) (component structure), [CODING_RULES.md](./CODING_RULES.md) (coding standards)

---

## 1. Core Design Principles

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

## 2. Component Architecture

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

## 3. Communication Architecture

### 3.1 Two-Channel Design

```
┌─────────────────────────────────────────────────────────────┐
│                                                             │
│   TOML Config = "What are the rules?"                       │
│   Shared Memory = "Is there news?"                          │
│                                                             │
│   NEVER shared: typing state, buffer, user intent           │
│                                                             │
└─────────────────────────────────────────────────────────────┘
```

### 3.2 Channel Specifications

| Channel | Content | Access Pattern | Update Frequency |
|---------|---------|----------------|------------------|
| **config.toml** | Input method, tone rules, preferences | Read at word boundary | Rare (user changes settings) |
| **SharedState** | Epoch, liveness, pending flag | Glance anytime (cheap) | Frequent (heartbeat) |
| **Engine-local** | Buffer, current word, composition | Never leaves engine | Every keystroke |

### 3.3 TOML Config Structure

```toml
# ~/.nexuskey/config.toml

[input]
method = "telex"          # telex, vni, viqr
enabled = true

[tone]
allow_free_position = false
oa_uy_tone1 = true        # óa/úy style

[commit]
on_space = true
on_punctuation = true
on_invalid = true

[features]
macro_enabled = true
spell_check = false
smart_switch = true
```

### 3.4 Shared Memory Structure

```c
// 56 bytes total - signals and config snapshot
struct SharedState {
    // Header (12 bytes)
    uint32_t magic;           // 'NKEY' = 0x59454B4E
    uint32_t structVersion;   // Struct layout version (increment on change)
    uint32_t structSize;      // sizeof(SharedState) for forward compat

    // Synchronization (4 bytes)
    uint32_t epoch;           // Seqlock counter (even=stable, odd=writing)

    // Runtime flags (4 bytes)
    uint32_t flags;           // Bitmask: VIETNAMESE_MODE, ENGINE_ENABLED, SPELL_CHECK

    // Config data (3 bytes)
    uint8_t  inputMethod;     // 0=Telex, 1=VNI, 2=SimpleTelex
    uint8_t  spellCheck;      // Spell check enabled
    uint8_t  optimizeLevel;   // Optimization level

    // Feature flags (3 bytes, little-endian uint32_t packed into 3 bytes)
    // Bits 0-15: featureFlags[2]  — 16 core flags (MODERN_ORTHO..EXCLUDE_APPS)
    // Bits 16-23: extFeatureFlags — 8 extended flags (AUTO_CAPS_MACRO..)
    // Get/Set via GetFeatureFlags()/SetFeatureFlags() which combine 3 bytes → uint32_t
    uint8_t  featureFlags[2]; // Bits 0-15
    uint8_t  extFeatureFlags; // Bits 16-23

    // Extended config (1 byte)
    uint8_t  codeTable;       // CodeTable enum value

    // Hotkey config (6 bytes, packed: 1 byte modifiers + 2 bytes key each)
    uint8_t  hotkeyMods;      // bits: [0]=ctrl [1]=shift [2]=alt [3]=win
    uint8_t  hotkeyKeyLo;     // wchar_t key, low byte
    uint8_t  hotkeyKeyHi;     // wchar_t key, high byte
    uint8_t  convertMods;     // convert hotkey (same encoding)
    uint8_t  convertKeyLo;
    uint8_t  convertKeyHi;

    // Reserved (23 bytes)
    uint8_t  reserved[23];    // Future expansion (7 flag bits remaining: 17-23)
};
```

---

## 4. Startup Sequences

### 4.1 Engine Startup (TSF DLL Loaded)

```
TSF Engine Loaded by Windows
            │
            ▼
    ┌───────────────────┐
    │ Config file exists?│
    └─────────┬─────────┘
              │
        Yes ──┴── No/Corrupt
         │              │
         ▼              ▼
    Load from       Use compiled
    config.toml     defaults
         │              │
         └──────┬───────┘
                │
                ▼
    ┌───────────────────────┐
    │ Copy to config snapshot│
    │ (immutable in engine)  │
    └───────────┬───────────┘
                │
                ▼
    ┌───────────────────────┐
    │ Start event listener   │
    │ (background thread)    │
    └───────────┬───────────┘
                │
                ▼
        Engine OPERATIONAL
        (Vietnamese typing works)
```

### 4.2 Compiled Defaults

When config.toml is missing or corrupt, engine uses hardcoded safe defaults:

```c
// Compiled into binary - engine ALWAYS works
static const TypingConfig DEFAULT_CONFIG = {
    .input_method = INPUT_METHOD_TELEX,
    .tone_allow_free = false,
    .oa_uy_tone1 = true,
    .commit_on_space = true,
    .commit_on_punct = true,
};
```

### 4.3 Core Startup (After Engine Already Running)

```
Core Process Starts
         │
         ▼
    Load/create config.toml
         │
         ▼
    Open shared memory
         │
         ▼
    Write SharedState:
    - config_epoch = current
    - core_alive = 1
         │
         ▼
    Signal named event ──────────► Engine receives event
    "Global\NexusKeyConfigReady"            │
         │                                   ▼
         │                          Mark "pending update"
         │                                   │
         ▼                                   ▼
    Core Ready                      At next word boundary:
                                    reload config.toml
```

### 4.4 Late Engine Start (Core Already Running)

```
Engine Loaded (Core already running)
         │
         ▼
    Config file exists? ──── Yes (Core created it)
         │
         ▼
    Load config.toml
         │
         ▼
    Open shared memory
         │
         ▼
    Read SharedState:
    - core_alive = 1 ✓
    - config_epoch = N
         │
         ▼
    Store epoch, start event listener
         │
         ▼
    Engine OPERATIONAL
    (full config from start)
```

---

## 5. Runtime Behavior

### 5.1 Word Boundary as Universal Sync Point

| Event | When Engine Reacts |
|-------|-------------------|
| Core starts | Next word boundary |
| Core crashes | No reaction (continue with snapshot) |
| Core restarts | Next word boundary |
| Config change | Next word boundary |
| User switches apps | Immediate (not mid-word) |

### 5.2 Config Update Flow

```
User changes Telex → VNI in Settings
              │
              ▼
       Core writes config.toml
              │
              ▼
       Core increments config_epoch
       Core sets pending_update = 1
              │
              ▼
       Core signals named event
              │
              ▼
       Engine receives signal
              │
              ▼
    ┌─────────────────────────┐
    │ Currently mid-word?     │
    └───────────┬─────────────┘
                │
          Yes ──┴── No
           │          │
           ▼          ▼
     Mark pending   Reload config
     (wait for      immediately
      commit)            │
           │             │
           ▼             │
     On commit:          │
     reload config       │
           │             │
           └──────┬──────┘
                  │
                  ▼
           Next word uses VNI
```

### 5.3 Engine Word Boundary Logic

```c
void on_word_commit(Engine* engine) {
    // 1. Commit current composition to application
    commit_text(engine->composition);

    // 2. Check for pending config update
    SharedState* shared = get_shared_state();
    if (shared && shared->config_epoch != engine->cached_epoch) {
        reload_config(engine);
        engine->cached_epoch = shared->config_epoch;
    }

    // 3. Clear state for next word
    clear_composition(engine);
}
```

---

## 6. Failure Handling

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

## 7. State Isolation

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

## 8. Named Event Protocol

### 8.1 Event Name

```
Global\NexusKeyConfigReady
```

### 8.2 Event Semantics

- **Type:** Manual-reset event
- **Initial state:** Non-signaled
- **On Core start:** Set (signaled)
- **On config change:** Pulse (set then reset)

### 8.3 Engine Event Handling

```c
// Background thread in engine
void config_watcher_thread(Engine* engine) {
    HANDLE event = OpenEvent(
        SYNCHRONIZE,
        FALSE,
        L"Global\\NexusKeyConfigReady"
    );

    while (engine->running) {
        DWORD result = WaitForSingleObject(event, 1000);

        if (result == WAIT_OBJECT_0) {
            // Event signaled - mark pending update
            engine->pending_config_update = true;
        }

        // Also check if event exists (Core alive)
        // If event disappears, Core crashed - no action needed
    }

    CloseHandle(event);
}
```

---

## 9. Security Considerations

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

## 10. Summary: The Two Questions

Every component interaction answers one of two questions:

| Question | Answered By | When Asked |
|----------|-------------|------------|
| **"What are the rules?"** | config.toml | At word boundary |
| **"Is there news?"** | SharedState | Anytime (cheap glance) |

Everything else stays local to the engine.

---

## Appendix A: Quick Reference

### A.1 File Locations

| File | Path | Purpose |
|------|------|---------|
| Config | `%APPDATA%\NexusKey\config.toml` | User settings |
| Log | `%APPDATA%\NexusKey\logs\` | Debug logs |
| Dictionary | `%APPDATA%\NexusKey\dict\` | Custom words |

### A.2 Named Objects

| Object | Name | Type |
|--------|------|------|
| Shared memory | `Local\NexusKeySharedState` | File mapping |
| Config event | `Local\NexusKeyConfigReady` | Manual-reset event |

### A.3 Magic Numbers

| Value | Meaning |
|-------|---------|
| `0x59454B4E` | 'NKEY' - SharedState magic |
| `56` | SharedState size (bytes) |

---

> **Document Version:** 1.0
> **Last Updated:** 2025-02-03
> **Status:** First Principles Complete
