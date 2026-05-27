# 12. Worker-Thread Doctrine

> Heavy work executes exclusively on the `MainThreadWorker` thread.
> WinEvent + LL hook callbacks are produce-only.
> Hook-thread state mutations route through the mailbox.

This rule operationalizes Pillar #1 (Nhanh) and Pillar #4 (Mở rộng không ảnh hưởng perf) from `docs/PHILOSOPHY.md`. It complements Rule 11 by naming the **single** thread that owns off-hook heavy work, so the answer to "where does this run?" is unambiguous for every new feature.

## 12.1 Why a doctrine — not just a guideline

Pre-Wave-3 the off-hook story was fragmented:

| Heavy work | Thread (pre-Wave-3) |
|---|---|
| `Classify()` (Win32 inspection, snapshot) | Main (WinEventProc) **OR** worker (OnTickPoll) |
| `QuickSyncFromSharedState` slow body | Whoever called it — main, worker, **or hook** |
| TOML reload | Worker (via `pendingConfigReload_` deferral) |
| Engine recreate | Hook thread (via `kConfigApply` mailbox) |

The two-thread `Classify` story produced a real race on `appProfileCache_` and `webView2PositiveCache_` (plain `unordered_map` / `unordered_set` mutated from main and worker concurrently — UB). The "whoever called it" QuickSync slow body allowed `make_shared<TypingConfig>` to run on the LL hook thread, a strict Rule 11.2 violation.

Patching each site with a lock or RCU container is a **band-aid**. The structural fix is to name a single thread for all off-hook heavy work and route producers through it. That thread already exists: `MainThreadWorker`.

## 12.2 The doctrine

**One owner per concern, expressed as one thread:**

| Concern | Owner thread | Reason |
|---|---|---|
| Engine state mutation (`engine_`, `previousComposition_`, `commitState_`) | **Hook thread** | Rule 11.3 single-writer for the hot path. |
| Off-hook heavy work (Win32 classify, TOML parse, snapshot rebuild, heap allocation, file I/O) | **Worker thread** (`MainThreadWorker`) | Rule 11.2 forbids it on hook; main thread has UI duties (Sciter rendering, dialogs). |
| Win32 callbacks pinned to main (WinEventProc, hotkey LL callbacks) | **Produce-only** — must NOT do heavy work themselves | They run on the WinEvent installer thread / LL hook thread. Any heavy work blocks the message loop or trips `LowLevelHooksTimeout`. |

Concretely:

1. **`WinEventProc` and any other main-thread Win32 callback** captures the event into an atomic latch + signals the worker. It does **NOT** call `Classify` or any other heavy function inline.
2. **`MainThreadWorker::workHandler`** owns the drain. On Signal it runs the full chain: `QuickSync` slow body, `Classify`, `RebuildSnapshotFromToml`, and posts the result to the hook thread via the mailbox.
3. **`MainThreadWorker::tickHandler`** continues to own the 200ms cadence (CJK detect, foreground-PID fallback). When it needs heavy work, it calls into the same drain helpers — it's already on the right thread.
4. **Hook thread (`LowLevelKeyboardProc`)** detects state changes (SharedState epoch bump, etc.) by setting a dirty bit and signaling the worker. It MUST NOT run the slow body itself, even when "it could" — that's how Rule 11.2 erodes.

```cpp
// FORBIDDEN — heavy work inline on main from WinEventProc
void CALLBACK FocusOwner::WinEventProc(...) {
    auto cls = self->Classify(hwnd, ctx);   // touches unordered_map<HWND, AppProfile>
    self->hookEngine->Post(kFocusChanged, std::move(cls));
}

// CORRECT — produce-only: latch + signal
void CALLBACK FocusOwner::WinEventProc(...) {
    self->pendingClassifyHwnd_.store(encode(hwnd), std::memory_order_release);
    self->onClassifyRequested_();   // wired to worker.Signal()
}
```

```cpp
// FORBIDDEN — heap allocation on hook slow path
void HookEngine::QuickSyncFromSharedState() {
    if (epochSame()) return;                 // fast path OK
    config_.store(std::make_shared<...>(cfg), release);  // on hook? Rule 11.2 violation.
}

// CORRECT — hook side signals worker, worker re-runs and drains
void HookEngine::QuickSyncFromSharedState() {
    if (epochSame()) return;                 // fast path
    if (isHookThread()) {
        // Do NOT advance lastEpoch_ — worker still needs to observe the change.
        if (workerSignalFn_) workerSignalFn_();
        return;
    }
    RunSlowBody();   // safe on main/worker (heap alloc OK here)
}
```

The Signal itself acts as the latch — the worker's workHandler always re-runs `SyncConfigFromSharedState`, which observes whatever change made the hook signal in the first place. No separate dirty bit is needed; the absence of one keeps the slot count minimal.

## 12.3 The single-owner test

Before adding a new feature that needs off-hook heavy work, answer:

1. **Who computes?** Must be the worker thread. If you're tempted to compute on main "because the event arrived there", you're producing a future race.
2. **Who consumes?** If the hook thread needs the result, route via mailbox (`HookCommandMailbox`). Posting a `shared_ptr<const T>` payload is RCU-safe and lock-free on the hook side.
3. **Who triggers?** Producers (main callbacks, hook detections) set an atomic latch + signal worker. Latches coalesce — if 100 focus events fire in 10ms, the worker classifies once with the latest.

If you can't answer all three with a single thread name, the feature is mis-designed. Stop and re-architect.

## 12.4 The latch + signal pattern

Producers don't enqueue work directly; they latch the relevant state into an atomic slot and signal the worker. The worker drains the slot when it wakes:

```cpp
// Producer (any thread, including LL callback):
focus_.PendingClassifyHwnd().store(encode(hwnd), std::memory_order_release);
worker_.Signal();

// Worker drain (single thread):
void HookEngine::DrainPendingClassify() {
    uintptr_t encoded = focus_.PendingClassifyHwnd().exchange(0, std::memory_order_acquire);
    if (encoded == 0) return;   // nothing pending
    HWND hwnd = decode(encoded);
    // ... heavy work here, single-threaded, lock-free caches safe.
}
```

**Properties:**

- **Coalescing**: multiple signals between drains = one drain. Per-event work doesn't pile up under burst.
- **Lock-free**: producer + consumer are both atomic ops. No mutex on the hot path.
- **Backpressure-free**: producer never blocks. If worker is busy, latch wins; older value is overwritten (acceptable for "use latest focus" semantics).

For payload types larger than a pointer, use `std::atomic<std::shared_ptr<T>>` (Rule 11.3 RCU pattern) instead of a packed integer.

## 12.5 Exemptions (none silent)

One narrow exemption exists; it must be documented in a comment with the reason:

1. **OnTickPoll's PID-change branch calling Classify directly** — already on the worker thread, so calling `Classify` directly preserves single-writer. Comment must cite this rule and identify the call site as already-on-worker.

No startup-window exemption is needed: WinEventProc is `WINEVENT_OUTOFCONTEXT`, so its delivery is gated on the main message loop pumping. As long as `MainThreadWorker::Start` runs before `GetMessageW` is first called (see `main.cpp` initialization order), the worker is alive by the time any WinEvent dispatches.

Any other exemption proposed in a PR must come with a doctrine update — not a special case.

## 12.6 Audit-allow markers

Source lines that look like Rule 11.3 violations to the grep-based audit (`tools/audit/check_hook_thread_no_mutex.sh`) but are legitimate pass-by-reference patterns (e.g., `make_unique<Gate>(atomic_field_)`) must carry an inline `// audit-allow: <reason>` annotation. The audit script's exclude regex skips lines with this marker.

Example:

```cpp
coordinator_.RegisterGate(
    std::make_unique<NextKey::Pipeline::EnglishBiasGate>(vietnameseMode_));  // audit-allow: gate stores const ref, IsRaised() uses .load(acquire)
```

The reason after the colon is mandatory — reviewers should see why the line is exempted without leaving the file.

## 12.7 Why this matters for Pillar #4

The doctrine is what lets Wave 4+ add new features without re-thinking threading every time. New gate? Store atomic by const ref, audit-allow the construction site. New off-hook computation? It goes on the worker. New hook detection? Latch + signal.

The pre-doctrine pattern produced a race in `Classify` even though every individual call site looked sensible. The doctrine prevents that class of bug at the architecture level — not by adding more locks, but by removing the ambiguity about which thread owns what.

---

**Cross-references:**

- Rule 11.2 — Forbidden Operations in Hook Callback (this doctrine enforces 11.2 at the architectural level).
- Rule 11.3 — The Contention Law (the latch + signal pattern is a generalization).
- `src/app/system/MainThreadWorker.h` — owner-thread implementation.
- `src/app/system/HookCommandMailbox.h` — worker → hook payload channel.
- Wave 3 PR 3.6 — first PR to enforce this doctrine end-to-end.
