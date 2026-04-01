# 3. Communication Architecture

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
