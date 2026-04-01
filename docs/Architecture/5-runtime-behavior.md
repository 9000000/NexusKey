# 5. Runtime Behavior

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
