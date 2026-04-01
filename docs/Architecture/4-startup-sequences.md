# 4. Startup Sequences

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
