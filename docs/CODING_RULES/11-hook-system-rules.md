# 11. Hook System Rules

> `LowLevelKeyboardProc` is the most latency-sensitive code in the entire system.
> Windows removes hooks that exceed `LowLevelHooksTimeout` (default 300ms).
> Every microsecond counts. Every syscall is a risk.

This rule operationalizes Pillar #1 (Nhanh) from `docs/PHILOSOPHY.md`. Violation of any clause below is a P0 reviewer-must-block issue.

## 11.1 The Two-Tier Budget

The hook callback budget has two tiers — an aspirational tier for state-machine work and a measured tier for total callback time including unavoidable output dispatch.

**Tier 1 — Engine pure CPU (aspirational, < 1µs):**

- Engine state-machine work: < 1µs (pure CPU)
- State reads: atomic only (no mutex)

**Tier 2 — Total hook callback (measured, < 30ms p99):**

The total callback may exceed Tier 1 because output dispatch (`SendInput`, `SendMessageTimeoutW`, deliberate `Sleep` for IPC reorder prevention) is bounded by app message-loop latency, not by Tier 1. The measured target is informed by the Sprint 1 baseline series:

- Target: < 30ms p99 in `chaos.toml` across the 5 baseline hosts (Notepad Win11 RichEditD2DPT, Notepad++, Chrome omnibox, Discord Electron, ChatGPT Chromium). Current measured worst-case p99 = 14–17ms (see `docs/baselines/perf-baseline-channeltraits-chaos.md`).
- Hard ceiling: `LowLevelHooksTimeout / 3` ≈ 100ms. Beyond this, Windows is at risk of silently dropping the hook.

The aspirational 1ms total budget was retired 2026-05-09 — it never matched codebase reality (see `HookEngine.cpp:3173-3174`: *"the budget is bounded by `LowLevelHooksTimeout`, not by the channel itself"*) and the SPSC ring proposal that would have closed the gap was closed by `docs/plans/2026-05-09-hook-engine-ring-buffer-kill.md` after the trade-off (sync 0ms lag for non-Vietnamese keystrokes vs async +1–2ms tax on all keystrokes) was made explicit.

```cpp
// GOOD: Atomic read, no lock
bool isViet = vietnameseMode_.load(std::memory_order_relaxed);

// FORBIDDEN: Mutex in hot path
std::lock_guard<std::recursive_mutex> _lock(stateMutex_);  // may block 5-15 ms!
```

## 11.2 Forbidden Operations in Hook Callback

These operations have **unbounded latency** and MUST NOT appear in any code path reachable from `LowLevelKeyboardProc`:

| Operation | Typical latency | Alternative |
|-----------|----------------|-------------|
| `std::mutex::lock()` (contended) | 0–∞ ms | `std::atomic` or lock-free |
| `CreateFile` / `ReadFile` | 1–100 ms | Cache or background thread |
| `CreateToolhelp32Snapshot` | 3–10 ms | Cache result, refresh on focus change |
| `Sleep()` | exact ms | Never in hook thread |
| `malloc` / `new` / `std::string()` | 0.01–1 ms | Pre-allocated buffers |
| `FlushFileBuffers` | 1–50 ms | Async write or drop |
| `SHGetFolderPathW` | 0.1–5 ms | Cache at startup |
| TOML parse (`toml::parse_file`) | 1–10 ms | Background thread + atomic swap |

## 11.3 The Contention Law

```
Hook thread READS state  ←→  Main thread WRITES state
Hook thread MUST NEVER wait for main thread.
```

### Correct patterns

**Atomic flags** (for booleans, enums, counters):

```cpp
// Write side (main thread)
isElectronApp_.store(true, std::memory_order_release);

// Read side (hook thread) — zero contention
bool electron = isElectronApp_.load(std::memory_order_acquire);
```

**RCU for config** (for complex structs):

```cpp
// Write side (main thread)
auto newConfig = std::make_shared<TypingConfig>(parseToml());
std::atomic_store(&activeConfig_, newConfig);

// Read side (hook thread) — zero contention
auto config = std::atomic_load(&activeConfig_);
```

**Two-phase focus detection** (for OnFocusChanged):

```cpp
// Phase 1: CLASSIFY — no lock, no state writes
auto result = ClassifyFocusedWindow(hwnd);  // Win32 API calls, process scan

// Phase 2: APPLY — short lock, state writes only
{
    std::lock_guard<std::mutex> _lock(stateMutex_);
    applyClassification(result);  // < 0.1 ms
}
```

### Forbidden patterns

```cpp
// Lock held during heavy work
std::lock_guard _lock(stateMutex_);
IsWebView2App(hwnd, path);  // 3-10 ms holding lock!

// Hook thread waits for config reload
std::lock_guard _lock(stateMutex_);
ReloadFromToml();  // file I/O holding lock!

// Classification runs for non-typing windows
if (IsTrayWindow(hwnd)) {
    // bug: still runs full classification below
}
ClassifyWindow(...);  // wasted 1-5 ms
```

## 11.4 Early-Return Hierarchy

The hook callback should return as early as possible. Check conditions in this order:

```cpp
LRESULT CALLBACK LowLevelKeyboardProc(int nCode, WPARAM wParam, LPARAM lParam) {
    // 1. Own synthetic events — return IMMEDIATELY (no processing)
    if (pKey->dwExtraInfo == NEXUSKEY_EXTRA_INFO) return CallNextHookEx(...);

    // 2. nCode < 0 — Windows says pass through
    if (nCode < 0) return CallNextHookEx(...);

    // 3. No instance — return (shouldn't happen)
    if (!self) return CallNextHookEx(...);

    // 4. Currently sending — skip (re-entrant guard)
    if (self->sending_) return CallNextHookEx(...);

    // 5. English mode + not macro-relevant — fast pass-through
    // (avoid engine processing entirely)

    // 6. Modifier keys held — pass through
    if (ctrl || alt || win) return CallNextHookEx(...);

    // 7. Finally: engine processing (the actual work)
}
```

## 11.5 Exception Safety

```cpp
// REQUIRED: Top-level catch in every hook callback
try {
    // ... hook logic ...
} catch (const std::exception& e) {
    // Rate-limit: max 1 log per second
    static DWORD lastLog = 0;
    DWORD now = GetTickCount();
    if (now - lastLog > 1000) {
        CrashLog(L"context", e.what());
        lastLog = now;
    }
    // ALWAYS reset state — prevents corrupt composition
    if (self) self->ResetComposition();
} catch (...) {
    // Same pattern for non-std exceptions
}
return CallNextHookEx(nullptr, nCode, wParam, lParam);
```

**Rules:**

- `CrashLog` is rate-limited (1/sec) — prevents I/O storm.
- `ResetComposition()` is mandatory — prevents state corruption cascade.
- The catch block is a safety net, not normal flow. If it fires, find and fix the root cause.
- Never re-throw from hook callback (causes `STATUS_FATAL_USER_CALLBACK_EXCEPTION`).

## 11.6 SendInput Timing

```cpp
// SendInput for standard Win32 apps: batch everything
bsEvents.insert(bsEvents.end(), charEvents.begin(), charEvents.end());
TrackedSendInput(bsEvents.data(), count);  // single call, FIFO guaranteed

// SendInput for Electron/Console: split with Sleep
// ONLY in DispatchSendInput, NEVER in hook callback
TrackedSendInput(bsEvents.data(), bsCount);
Sleep(delayMs);  // OK here — we are past the hook return
TrackedSendInput(charEvents.data(), charCount);
```

`Sleep()` is acceptable in `DispatchSendInput` because the hook has already returned `1` (eaten the key). The Sleep happens during the output phase, not the capture phase.

---

## Summary: Hook Performance Checklist

- [ ] Hook callback returns within 1 ms
- [ ] No `mutex.lock()` that contends with main thread
- [ ] No file I/O reachable from hook callback
- [ ] No process/module enumeration in hook callback
- [ ] No heap allocation on the common path
- [ ] Early-return for synthetic events, modifiers, English mode
- [ ] Focus classification runs outside any lock
- [ ] Config reload happens on background thread
- [ ] Exception handling is rate-limited and resets state
- [ ] `Sleep()` only in output dispatch, never in hook body
