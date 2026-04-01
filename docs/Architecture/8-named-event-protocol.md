# 8. Named Event Protocol

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
