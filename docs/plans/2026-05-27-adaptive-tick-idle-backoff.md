# Adaptive Tick Idle Backoff — restore Classic-build idle RAM parity with v2.1.24

**Date**: 2026-05-27
**Status**: Plan v1 — test-first, awaiting approval
**Branch**: `feat/architecture-review-v3.1`
**Predecessor**: HEAD `7504673` (Wave 3 PR 3.8 SmartSwitch state split, post Sprint 1 D10 + Wave 3 PR 3.1/3.6)
**Goal**: Restore the ~0.5 MB Private WS idle parity that v2.1.24 Classic exhibited (trim to ~1.11 MB) but v3.0.0 Classic loses (stuck at ~1.64 MB). Cause is the new `MainThreadWorker` dedicated thread (Sprint 1 D10) firing every 200 ms unconditionally — its tick + thread stack/TEB pages stay warm forever, blocking Windows working-set aging. Fix: idle-backoff cadence (200 ms active → 1 s after 10 s idle → 5 s after 60 s idle), reset on hook activity or focus change. Doctrine-compliant: worker stays the single off-hook owner; only its cadence adapts.

## 0. Why this exists

### Benchmark data (`tools/measure-ram.ps1`, 2026-05-27, 4 binaries × 5 min idle each)

| Build | Private WS start | Private WS min | Private WS end | Δ trim |
|---|---|---|---|---|
| v2.1.24 Classic | 1.70 MB | **1.11 MB** | 1.16 MB | **−0.54 MB** ✓ |
| v3.0.0 Classic | 1.69 MB | 1.64 MB | 1.69 MB | **0.00 MB** ✗ |
| v2.1.24 Sciter | 1.67 MB | 1.61 MB | 1.66 MB | −0.01 MB |
| v3.0.0 Sciter | 1.62 MB | 1.37 MB | 1.43 MB | −0.19 MB |

**Finding**: Backend regression (HookEngine + MainThreadWorker shared by Sciter and Classic). Sciter masks the regression because the 890 KB packed UI archive (`src/app/resources.cpp`) ages out post-init, dominating private-WS trim. Classic has no such trimmable surface → regression visible.

### Root cause — verified by code diff (not speculation)

| v2.1.24 | v3.0.0 |
|---|---|
| `focusPollTimer_ = SetTimer(nullptr, 0, 200, FocusPollTimerProc)` in `HookEngine.cpp:198` | `g_mainThreadWorker.SetTickHandler([](){ g_hookEngine.OnTickPoll(); }); SetTickInterval(200ms); Start()` in `main.cpp:552-556` + `main_lite.cpp:617-621` |
| Callback dispatch on **main thread** (already warm via `GetMessageW` loop) | Callback dispatch on **dedicated thread** with own stack + TEB |
| `FocusPollTimerProc` touches `s_instance`, `stateMutex_`, `lastForegroundPid_` (~3 pages) | `OnTickPoll` touches `Perf::Histogram::Enabled()`, `pendingConfigReload_`, `sharedStatePtr_`, `lifecycle_.Mailbox()`, `focus_.LastForegroundPid()` (~6 pages) |

Cost of dedicated-worker design (held resident permanently):
- 2 dedicated thread TEBs + initial committed stacks (MainThreadWorker + HookLifecycle PR 3.1): ~16–32 KB
- `OnTickPoll` page-touch set × every 200 ms (no aging): ~24 KB
- `std::function` vtable + lambda + `std::condition_variable` internals: ~10–30 KB
- Wave 2 RCU `shared_ptr<TypingConfig>` snapshot held alive: ~50–200 KB (macros + exclude lists)
- Mailbox + bookkeeping: ~5–10 KB
- **Total ~100–300 KB** matching observed regression magnitude.

### Constraint conflict (why "just gỡ MainThreadWorker" is wrong)

| Constraint | Forbids |
|---|---|
| `docs/CODING_RULES/12-worker-thread-doctrine.md` §12.2 | Routing OnTickPoll heavy work back to main thread. `ReloadFromToml` 35–100 ms cold cache must stay on worker. |
| Wave 3 PR 3.6 (`ec97b2a fix(hookengine): worker-thread doctrine fixes 3 races`) | Undoing the dedicated worker would re-introduce 3 fixed races on `appProfileCache_`, `webView2PositiveCache_`, and `make_shared<TypingConfig>` on hook thread. |
| `docs/PHILOSOPHY.md` Pillar Mượt | Long config reload (1–100 ms) on main thread = Sciter render stutter. |
| `docs/PHILOSOPHY.md` Pillar Nhanh | Hook callback < 1 ms (Rule #11.1). Any adaptive trigger must add ≤ 1 atomic store on the hook hot path. |

**→ Cadence adaptation is the only fix-shape that fits all constraints. Worker stays. Doctrine stays. Only the interval changes when idle.**

## 1. Pre-code questions (mandatory per PHILOSOPHY §3)

### Q1 — Right place?

**Activity timestamp** (the "last activity" signal):
- Writers: hook thread (keystroke detected) and main thread (WinEvent focus change). Both are produce-only per Rule 12.4 latch+signal pattern.
- Reader: worker thread inside `OnTickPoll`.
- Right type: `std::atomic<uint64_t>`. Right module: `src/app/system/HookEngine.h` (already coordinates hook/focus/worker through its mailbox and owns `lastForegroundPid_` which has the same writer/reader topology).
- Right field name: `lastActivityTickMs_`. Right access pattern: relaxed memory order on both store and load (no synchronization-with required; the value is a hint, not a synchronization signal — worst case the worker uses a slightly stale value and the cadence resolves on the next tick).

**Adaptive cadence math**:
- Pure function `ComputeTickInterval(idleMs) → ms`. **Linux-portable** (no Win32) → goes in a new tiny header `src/app/system/AdaptiveTick.h`. GTest-friendly without a Win32 stub.
- Reason it doesn't live inside HookEngine: HookEngine is Win32-only; the math is pure. Keeping it portable means GTest covers it without `#ifdef _WIN32`.

**Retune call (worker → tick owner)**:
- HookEngine doesn't own `MainThreadWorker` (main.cpp does). Use the same callback-injection pattern as the existing `SetWorkerSignalFn` (wired in `main.cpp:548` before `HookEngine::Start`).
- New seam: `HookEngine::SetTickRetuneFn(std::function<void(std::chrono::milliseconds)>)`. Main wires `[](ms){ g_mainThreadWorker.SetTickInterval(ms); }`.

### Q2 — Impact / blast radius?

| Surface | Change | Cost |
|---|---|---|
| Hook hot path (`LowLevelKeyboardProc → ProcessKeyDown`) | +1 atomic store (relaxed) of `GetTickCount64()` value | ~5 ns; well within Rule #11.1 < 1 ms budget |
| WinEventProc main-thread callback | +1 atomic store on focus change | ~5 ns; not on hook path |
| `OnTickPoll` body | +1 atomic load + 1 compare + (when crossing threshold) 1 callback to `MainThreadWorker::SetTickInterval` | Negligible; already on worker |
| `SharedState` struct | **No change** — `lastActivityTickMs_` is process-local. No version bump per Rule 5. |
| Public API | New `HookEngine::SetTickRetuneFn` (mirrors existing `SetWorkerSignalFn`). One callsite in each of `main.cpp` and `main_lite.cpp`. |
| Build (CMake) | No new files registered (new files go under existing `NEXTKEY_ENGINE_SOURCES` and `NEXTKEY_TEST_SOURCES` targets). |
| Cross-process | None. |

**CJK / focus responsiveness — explicit trade-off:**
- After 60 s idle, worker tick = 5 s. `CheckLayoutChange` (CJK detection) runs on this cadence via mailbox.
- Edge case: user changes system IME (e.g., Microsoft IME 한 mode) without any focus change OR keystroke. Detection lag = up to 5 s.
- **Mitigation**: (a) when user does change layout, they almost always click the language bar = focus event = WinEvent fires = `MarkActivity()` resets cadence to 200 ms. (b) when user types into the new IME, the next keystroke (even if "eaten" by the system IME) wakes the hook → `MarkActivity()` → reset.
- **Residual risk**: pure-keyboard layout switch (Win+Space) with no focus change. Lag bounded to 5 s. Acceptable — user is idle anyway.

### Q3 — Better way?

| Alternative | Why rejected |
|---|---|
| **WaitableTimer with adjustable period** | More syscalls than `std::condition_variable::wait_for`; ties us to Win32. `MainThreadWorker` already uses CV-based wait that supports `SetTickInterval` reactively (via `notify_all` — see `MainThreadWorker.cpp:86`). Reuse it. |
| **Skip ticks entirely when idle** (instead of stretching) | Lose CJK polling completely. Pure-keyboard layout switch undetectable until user types. Stretching keeps CJK alive at degraded but bounded cadence. |
| **`EmptyWorkingSet` after N seconds idle** | Anti-pattern per Raymond Chen / Bruce Dawson (Random ASCII 2023) + verified in this thread's research turn. Refault penalty on first activity = stutter (Pillar Mượt violated). |
| **Smaller thread stack reserve** (`STACK_SIZE_PARAM_IS_A_RESERVATION 64KB`) | Saves only ~vài KB physical RAM (reserve = virtual AS). Doesn't address the 200 ms touch cadence which is the dominant cause. Could be combined with this plan in a follow-up but not load-bearing. |
| **Run OnTickPoll on main thread via SetTimer (revert to v2.1.24 model)** | Undoes Rule 12 doctrine + Wave 3 PR 3.6 three-race-fix. Hard reject. |
| **Adaptive ONLY when watchdog off** (rationale: watchdog already wakes 100 ms anyway) | Watchdog default = off; this would only special-case the minority. Plus, if user turns watchdog on, HeartbeatPublisher is its own 10-wake/s thread regardless. Adaptive should be unconditional. |

## 2. Design

### 2.1 Files (new + modified)

```
NEW   src/app/system/AdaptiveTick.h             # ~25 LOC pure C++, Linux-portable
NEW   tests/AdaptiveTickTest.cpp                # ~60 LOC GTest, Linux-portable
MOD   src/app/system/HookEngine.h               # +2 atomic fields, +1 setter, +1 marker
MOD   src/app/system/HookEngine.cpp             # +MarkActivity body, +OnTickPoll adaptive block, +SetTickRetuneFn body
MOD   tests/HookEngineAtomicTests.cpp           # +1 case asserting MarkActivity stores & adaptive load
MOD   src/app/main.cpp                          # +1 SetTickRetuneFn wiring before Start
MOD   src/app/main_lite.cpp                     # +1 SetTickRetuneFn wiring before Start
MOD   tests/MainThreadWorkerTests.cpp           # +1 case verifying SetTickInterval mid-wait wakes worker (regression guard for the existing notify_all behavior we depend on)
MOD   CMakeLists.txt                            # NONE — new files land under existing globs (verify)
```

### 2.2 `AdaptiveTick.h` (pure, Linux-portable)

```cpp
#pragma once
#include <chrono>
#include <cstdint>

namespace NextKey {

// Thresholds and tick periods are exposed as compile-time constants so that
// tests can compute the boundary cases without having to scrape them out of
// HookEngine. Anyone re-tuning the cadence updates here and the tests.
inline constexpr std::uint32_t kTickActiveMs       = 200;
inline constexpr std::uint32_t kTickIdleShortMs    = 1000;
inline constexpr std::uint32_t kTickIdleLongMs     = 5000;
inline constexpr std::uint64_t kIdleShortThreshMs  = 10'000;
inline constexpr std::uint64_t kIdleLongThreshMs   = 60'000;

/// Pure function — given elapsed ms since the last MarkActivity(), returns the
/// tick interval the worker should run at. Used by HookEngine::OnTickPoll to
/// retune MainThreadWorker. Linux-portable, no Win32 dependency.
[[nodiscard]] constexpr std::chrono::milliseconds
ComputeTickInterval(std::uint64_t idleMs) noexcept {
    if (idleMs < kIdleShortThreshMs) {
        return std::chrono::milliseconds(kTickActiveMs);
    }
    if (idleMs < kIdleLongThreshMs) {
        return std::chrono::milliseconds(kTickIdleShortMs);
    }
    return std::chrono::milliseconds(kTickIdleLongMs);
}

}  // namespace NextKey
```

### 2.3 `HookEngine.h` additions

```cpp
// New fields (private members):
std::atomic<std::uint64_t> lastActivityTickMs_{0};
std::atomic<std::uint32_t> currentTickIntervalMs_{kTickActiveMs};
std::function<void(std::chrono::milliseconds)> tickRetuneFn_;  // wired from main.cpp

// New public methods:
void SetTickRetuneFn(std::function<void(std::chrono::milliseconds)> fn) noexcept;
void MarkActivity() noexcept;  // called by hook callback + WinEventProc
```

### 2.4 `HookEngine.cpp` additions

```cpp
// MarkActivity — Rule 11.2 compliant: 1 atomic store + gated wake of worker.
// Callable from hook thread, main thread (WinEventProc), worker thread.
//
// Gated wake design (2026-05-27 revision after thread review):
// - Hot path (already in active cadence): atomic store + atomic load + branch
//   miss = ~5 ns. Hits Rule #11.1 Tier-1 budget.
// - Cold path (idle → active transition): + workerSignalFn_() which takes the
//   worker mutex (uncontended, ~30-50 ns) + cv_.notify_all (~50-200 ns). Fires
//   AT MOST once per idle cycle.
// Without the gate, every keystroke would Signal → workHandler runs SyncConfig
// + DrainClassify = ~1-10 ms — wasted work since cadence is already 200 ms.
void HookEngine::MarkActivity() noexcept {
    lastActivityTickMs_.store(GetTickCount64(), std::memory_order_relaxed);
    if (currentTickIntervalMs_.load(std::memory_order_relaxed) != kTickActiveMs) {
        if (workerSignalFn_) workerSignalFn_();
    }
}

void HookEngine::SetTickRetuneFn(std::function<void(std::chrono::milliseconds)> fn) noexcept {
    tickRetuneFn_ = std::move(fn);
}

// Helper — called from BOTH OnTickPoll (worker tick path) AND workHandler
// (worker signal path after MarkActivity wake). DRY: same retune logic
// regardless of which entry point woke the worker.
void HookEngine::RetuneCadenceIfNeeded() noexcept {
    const std::uint64_t now    = GetTickCount64();
    const std::uint64_t lastAct = lastActivityTickMs_.load(std::memory_order_relaxed);
    const std::uint64_t idleMs  = (now > lastAct) ? (now - lastAct) : 0;
    const auto desired = ComputeTickInterval(idleMs);
    const auto desiredMs = static_cast<std::uint32_t>(desired.count());
    if (desiredMs != currentTickIntervalMs_.load(std::memory_order_relaxed)) {
        currentTickIntervalMs_.store(desiredMs, std::memory_order_relaxed);
        if (tickRetuneFn_) {
            tickRetuneFn_(desired);  // SetTickInterval — uncontended mutex + notify_all
        }
    }
}

// At the END of OnTickPoll (after existing body, INSIDE the try block):
//     RetuneCadenceIfNeeded();
//
// In main.cpp workHandler wiring (extend the existing lambda):
//     g_mainThreadWorker.SetWorkHandler([]() {
//         g_hookEngine.SyncConfigFromSharedState();
//         g_hookEngine.DrainClassifyOnWorker();
//         g_hookEngine.RetuneCadenceIfNeeded();   // NEW — handles MarkActivity wake
//     });
```

### 2.5 Hook callback insertion (`MarkActivity` call site)

There's exactly one place in the hook path that's hit by every real keystroke: the entry of `HookEngine::ProcessKeyDown` (or its top-level wrapper). The exact insertion line will be confirmed during implementation — must be **after** the dead-key / synth-event filter so synthetic re-injections don't keep the cadence pinned (a synthetic keypress doesn't count as user activity).

The other insertion is in `FocusOwner::WinEventProc` (or its main-thread bridge). Same atomic store — runs on main, not hook, but the cost is identical.

### 2.6 Main wiring (`main.cpp` + `main_lite.cpp`)

```cpp
// BEFORE g_mainThreadWorker.Start() — same ordering as the existing
// SetWorkerSignalFn wiring (see Wave 3 PR 3.6 comment in main.cpp:548).
g_hookEngine.SetTickRetuneFn([](std::chrono::milliseconds ms) noexcept {
    g_mainThreadWorker.SetTickInterval(ms);
});
```

## 3. Test-first plan

**Sequence (mandatory per PHILOSOPHY §3 "Sequence"): tests written + red BEFORE implementation.**

### 3.1 Linux-portable unit tests (`tests/AdaptiveTickTest.cpp`)

Six cases pinning `ComputeTickInterval` boundaries. All can run on Linux build (`./build-linux/tests/VKeyTests --gtest_filter="AdaptiveTickTest.*"`).

```cpp
TEST(AdaptiveTickTest, ZeroIdle_ReturnsActive) {
    EXPECT_EQ(ComputeTickInterval(0), std::chrono::milliseconds(200));
}
TEST(AdaptiveTickTest, JustBelowShortThreshold_ReturnsActive) {
    EXPECT_EQ(ComputeTickInterval(9'999), std::chrono::milliseconds(200));
}
TEST(AdaptiveTickTest, AtShortThreshold_ReturnsIdleShort) {
    EXPECT_EQ(ComputeTickInterval(10'000), std::chrono::milliseconds(1'000));
}
TEST(AdaptiveTickTest, JustBelowLongThreshold_ReturnsIdleShort) {
    EXPECT_EQ(ComputeTickInterval(59'999), std::chrono::milliseconds(1'000));
}
TEST(AdaptiveTickTest, AtLongThreshold_ReturnsIdleLong) {
    EXPECT_EQ(ComputeTickInterval(60'000), std::chrono::milliseconds(5'000));
}
TEST(AdaptiveTickTest, FarPastLongThreshold_StaysIdleLong) {
    EXPECT_EQ(ComputeTickInterval(86'400'000), std::chrono::milliseconds(5'000));
}
```

**Red expectation**: tests fail at compile until `AdaptiveTick.h` exists.

### 3.2 HookEngine atomic test (`tests/HookEngineAtomicTests.cpp` — Linux-portable surface only)

Add ONE case. Existing file pattern: tests against the atomic-load-store surface, no hook thread spin-up needed.

```cpp
TEST(HookEngineAtomicTests, MarkActivity_StoresMonotonicTimestamp) {
    HookEngine eng;
    eng.MarkActivity();
    // Implementation note: this test is Linux-portable because MarkActivity
    // calls GetTickCount64 which, on Linux build, we either shim or skip via
    // existing pattern. If shim infeasible, move this test to a Win32 #ifdef
    // section — does not block the plan.
    // ...assert atomic exposed via a friend accessor or new getter for tests
}
```

If exposing `lastActivityTickMs_` raw to test seems wrong, instead add a getter `uint64_t LastActivityTickMsForTest() const noexcept { return lastActivityTickMs_.load(std::memory_order_relaxed); }`. Test-only naming convention.

### 3.3 MainThreadWorker reactive-wake regression test (`tests/MainThreadWorkerTests.cpp`)

Pin the existing behavior the plan depends on: calling `SetTickInterval` from another thread while the worker is mid-wait wakes it (so a 5 s wait collapses to a 200 ms wait when activity resumes).

```cpp
TEST(MainThreadWorkerTests, SetTickInterval_MidWait_WakesWorker) {
    MainThreadWorker w;
    std::atomic<int> tickCount{0};
    w.SetTickHandler([&]() { tickCount.fetch_add(1, std::memory_order_relaxed); });
    w.SetTickInterval(std::chrono::milliseconds(5'000));
    w.Start();

    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    EXPECT_EQ(tickCount.load(), 0);  // 50 ms < 5 s → no tick yet

    w.SetTickInterval(std::chrono::milliseconds(50));  // should wake the wait_for
    std::this_thread::sleep_for(std::chrono::milliseconds(120));
    EXPECT_GE(tickCount.load(), 1);  // adaptive shortening must have fired

    w.Stop();
}
```

**Red expectation**: this test must pass on `main` already (the behavior exists per `MainThreadWorker.cpp:86` `notify_all`). If it fails on main, the plan's assumption breaks — STOP and revise. Running this test first is the assumption check.

### 3.4 End-to-end manual benchmark (regression criterion)

After implementation, run `tools\measure-ram.ps1` with all 4 binaries × 10 min each.

**Scenario A — pure idle** (same as pre-fix baseline benchmark):
- Launch → walk away 10 min. Pass criterion:
  - v3.0.0 Classic Private WS min ≤ 1.30 MB (vs current 1.64 MB)
  - v3.0.0 Sciter Private WS min ≤ 1.30 MB (improves or matches current 1.37 MB)
  - Thread count idle still 3
  - No new "GREW" verdict

**Scenario B — active typing then idle** (NEW — added 2026-05-27 after user-reported "1.7 → 2.2 MB during Vietnamese typing"):
- Launch baseline 30 s → type 200 Vietnamese words (5-10 min of active typing) into Notepad → walk away 5 min idle. Pass criterion:
  - Working Set / Task Manager Memory column drops back to within +0.2 MB of launch baseline (i.e., 5-min idle reclaims most of the typing-induced page warming)
  - Pre-fix baseline: Memory stays elevated (~2.2 MB) indefinitely
  - Post-fix expectation: Memory drops to ~1.5 MB after 60 s+ idle

**Scenario C — Settings dialog effect** (NEW — same date, after user-reported "đúng hình như có mở setting"):
- Launch baseline 30 s → open Settings dialog → close → walk away 5 min idle. Pass criterion:
  - Memory drops back to within +0.3 MB of launch baseline after 5-min idle
  - Confirms working-set trim does evict the Settings dialog pages (committed but no longer touched by MainThreadWorker because cadence is now backed off)
  - Note: Private Bytes / Commit Size will stay elevated permanently (C++ heap free-list semantics, expected) — this scenario only validates Working Set trim

**Fail-stop criterion** (any scenario):
- If Sciter trim WORSENS (verdict "FLAT" appears on Sciter where it previously trimmed), back the change out — adaptive interrupted Sciter's existing trim path.
- If Working Set in Scenarios B/C does NOT drop after 5 min idle, the adaptive backoff isn't activating — investigate (MarkActivity insertion site wrong? cadence not retuning?).

### 3.5 Chaos corpus regression (mandatory before merge)

Run `tools\run-chaos.ps1` on the modified build. Current baseline: 54/55 (chrome `1.3-escape-bs-aa-b` is the known environmental flake). New baseline must match — adaptive ticking must not affect typing semantics. If chaos drops below 54/55 (excluding the flake), STOP and investigate.

### 3.6 No-lag verification (mandatory pre-merge)

The adaptive backoff design introduces overhead on the hook hot path (MarkActivity) and a potential first-keystroke-after-idle delay (workerSignalFn_ → workHandler chain). Verify NO user-perceptible lag is introduced.

#### 4 sources of lag

| # | Source | Magnitude (theory) | Measurement |
|---|---|---|---|
| **L1** | `MarkActivity()` body on EVERY hook callback: atomic store + atomic load + branch | ~5-10 ns | Microbench gtest (Linux-portable) |
| **L2** | `workerSignalFn_()` call ONLY on idle→active transition: lock_guard + cv.notify_all | ~100-300 ns, once per idle cycle | Same microbench (with mocked signal counter) |
| **L3** | Worker mutex contention when hook calls Signal while worker is mid-workHandler | <10 µs (mutex held briefly by worker) | Chaos corpus 54/55 baseline must hold (existing concurrent test surface) |
| **L4** | First-keystroke-after-idle triggers full `SyncConfigFromSharedState + DrainClassifyOnWorker + RetuneCadence` chain (1-10 ms cold cache). Hook callback does NOT block on this (Signal returns immediately) but the TICK driving CJK detect waits one full new-cadence cycle (200 ms post-resume). | ~200 ms CJK detection lag for first key after idle. | Manual "idle 90 s + Win+Space + CJK switch" test |

#### Tests

**§3.6.1 — MarkActivity microbench** (`tests/AdaptiveTickBenchmark.cpp`, Linux-portable):

```cpp
TEST(AdaptiveTickBenchmark, MarkActivityHotPathBudget) {
    // Stub HookEngine with no workerSignalFn_ wired — measure raw atomic cost.
    HookEngineMock eng;
    const int kIters = 1'000'000;
    auto t0 = std::chrono::steady_clock::now();
    for (int i = 0; i < kIters; ++i) eng.MarkActivity();
    auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now() - t0).count() / kIters;
    EXPECT_LT(ns, 50);  // 50 ns budget (10× margin over expected ~5 ns)
}

TEST(AdaptiveTickBenchmark, MarkActivityGatedSignalFiresOnceOnTransition) {
    HookEngineMock eng;
    std::atomic<int> signalCount{0};
    eng.SetWorkerSignalFn([&] { signalCount.fetch_add(1, std::memory_order_relaxed); });
    eng.SetCurrentTickIntervalForTest(1000);  // simulate backoff state

    eng.MarkActivity();                       // transition → signal fires
    EXPECT_EQ(signalCount.load(), 1);

    eng.SetCurrentTickIntervalForTest(200);   // simulate post-retune
    for (int i = 0; i < 100; ++i) eng.MarkActivity();  // active state
    EXPECT_EQ(signalCount.load(), 1);         // no further signals
}
```

**§3.6.2 — Windows keystroke perf baseline** (`tools\benchmark_ime.ps1`):

```powershell
# Pre-fix: capture baseline
.\tools\benchmark_ime.ps1 -IME "VKey-prefix" -Method telex -Rounds 5

# Post-fix: same command, compare
.\tools\benchmark_ime.ps1 -IME "VKey-postfix" -Method telex -Rounds 5
.\tools\benchmark_ime.ps1 -Compare
```

Pass criterion:
- p95 keystroke latency: ≤ baseline + 1 ms (margin for ~100 ns overhead)
- p99 keystroke latency: ≤ baseline + 2 ms
- Worst case: ≤ baseline + 5 ms

**§3.6.3 — Manual "idle → first key" latency** (Windows):

Launch VKey → walk away 90 s → focus Notepad → press 'a' (Telex). Compare visual lag vs pre-fix. Should be imperceptible.

Objective measurement: extend `benchmark_ime.ps1` with optional `-PreSleep 90` parameter to insert 90 s sleep before first round. Compare round 1 vs round 5 latency:
- Pre-fix: round 1 ≈ round 5
- Post-fix expectation: round 1 should NOT spike > round 5 + 5 ms

**§3.6.4 — Manual CJK switch latency after idle** (Windows):

Launch → gõ vài chữ tiếng Việt → walk away ≥ 90 s → Win+Space switch to Chinese/Japanese/Korean IME → time elapsed until VKey switches to E mode (tray icon V→E).

Pass criterion: ≤ 500 ms perceived. Acceptable for human perception (humans don't notice < 1 s lag).

#### Fail-stop thresholds (rollback triggers)

| Test | Fail threshold |
|---|---|
| §3.6.1 MarkActivityHotPathBudget | > 50 ns/op |
| §3.6.1 GatedSignal counter | Signals more than once OR doesn't signal at all |
| Chaos corpus | < 54/55 (excluding chrome 1.3-escape-bs-aa-b flake) |
| §3.6.2 p95 | > baseline + 1 ms |
| §3.6.3 round 1 vs round 5 | round 1 > round 5 + 5 ms |
| §3.6.4 CJK switch after 90 s idle | > 1000 ms |

## 4. Implementation order

| Step | What | Status when done |
|---|---|---|
| 1 | Write `tests/AdaptiveTickTest.cpp` (6 cases). | Red — compile failure (no AdaptiveTick.h). |
| 2 | Write `tests/MainThreadWorkerTests.cpp` reactive-wake case. | **Should be green on `main`** — assumption check. If red, STOP. |
| 3 | Create `src/app/system/AdaptiveTick.h`. | AdaptiveTickTest green. |
| 4 | Add `lastActivityTickMs_`, `currentTickIntervalMs_`, `tickRetuneFn_`, `MarkActivity()`, `SetTickRetuneFn()` to HookEngine. | Compiles; no behavior change yet. |
| 5 | Add adaptive block at end of `OnTickPoll`. | Worker self-retunes on next tick when crossing threshold. |
| 6 | Wire `g_hookEngine.SetTickRetuneFn(...)` in `main.cpp` AND `main_lite.cpp` before `g_mainThreadWorker.Start()`. | End-to-end live; runtime should retune. |
| 7 | Insert `MarkActivity()` calls: (a) entry of hook ProcessKeyDown post-synth-filter, (b) WinEventProc focus-change bridge on main thread. | Activity resets cadence to 200 ms. |
| 8 | Add HookEngine atomic test case for MarkActivity store. | Green. |
| 9 | Run Linux test suite (`VKeyTests`) — must pass. | Green. |
| 10 | Build Windows VKey + VKeyLite. Run chaos corpus. | 54/55 baseline match. |
| 11 | Run `measure-ram.ps1` 10-min × 4 binaries. Compare to baseline CSVs. | Pass criterion §3.4 met. |
| 12 | Commit. PR. | Done. |

## 5. Acceptance criteria (Nyquist sampling)

Per project Nyquist convention (verify each design claim at least twice):

| Claim | Sample 1 | Sample 2 |
|---|---|---|
| `ComputeTickInterval` produces correct boundary mapping | Unit tests §3.1 | Code review reads the body and confirms |
| `MarkActivity` is Rule 11.2 compliant (atomic, no mutex/alloc/IO) | Code review | Audit script `tools/audit/check_hook_thread_no_mutex.sh` (verify CI passes after change) |
| Worker mid-wait wake on interval shortening works | Regression test §3.3 | Live benchmark §3.4 (Private WS visibly drops past 60 s idle then jumps back when activity simulated) |
| No CJK / focus regression | Manual: bật/tắt CJK IME on Win, observe ≤ 5 s lag after long idle | Chaos corpus 54/55 |
| Doctrine intact | Code review of `OnTickPoll` body — heavy work still on worker, hook callback still produce-only | Wave 3 PR 3.6's three races: re-run their tests (HookEngineAtomicTests `appProfileCache_` + `webView2PositiveCache_` cases) — must still pass |

## 6. Rollback plan

| Trigger | Action |
|---|---|
| Chaos corpus < 54/55 (excluding known flake) | `git revert` the PR. `MarkActivity` insertion likely interferes with synth-event flow. Investigate before re-attempt. |
| `measure-ram.ps1` shows Sciter regressed (FLAT verdict) | `git revert`. Adaptive cadence is interfering with Sciter's existing trim. Investigate. |
| CJK / focus user complaint within first week post-release | Hot-fix: lower `kIdleLongThreshMs` from 60 s to 30 s, and `kTickIdleLongMs` from 5 s to 2 s. Re-bench. |
| User reports keystroke lag (hot-path budget overrun) | `git revert MarkActivity` insertion in hook (keep rest of plan). Lose the keystroke-resets-cadence behavior but keep WinEvent reset. |

## 7. Open questions

1. **Exact insertion point for `MarkActivity` in hook path**: must be after synth-event filter (avoid synthetic re-injections resetting cadence). Confirm during Step 7 by reading `HookEngine::ProcessKeyDown` entry. **Stop condition**: if no clear post-filter insertion point exists, fold the filter check into MarkActivity (`MarkActivity(bool isRealUserKey)`) and call unconditionally from the LL callback.
2. **Should config reload (SharedState bump from Settings) reset cadence?** SharedState writer is the Settings subprocess; HookEngine observes via `pendingConfigReload_`. Argument FOR: user just touched a setting → activity → reset. Argument AGAINST: setting may be set programmatically (auto-update applying a config) → not real user activity. **Resolution**: NO, do not reset on config reload. Only keystroke + focus change. Document this decision in code comment.
3. **Test-only accessor or friend?** Default to a public `LastActivityTickMsForTest()` getter (Win32-only if needed). If anh prefers `friend class HookEngineTest;` pattern, switch in Step 8. Either works; getter is simpler.

## 8. Risk summary

| Risk | Likelihood | Mitigation |
|---|---|---|
| `MarkActivity` insertion site wrong → synth keys reset cadence → no idle savings | Medium | Open Q1 — verify during Step 7. Tested by Step 11 benchmark. |
| CV `wait_for` doesn't wake on interval-shorten (MainThreadWorker bug) | **Low** — already implemented per `MainThreadWorker.cpp:86`. | Step 2 assumption-check test catches this on `main` before any other change. |
| Adaptive interferes with `pendingConfigReload_` drain (5 s tick = 5 s reload lag) | Low | TOML reload is rare (only when Settings dialog saves). When user touches Settings, focus changes back to Settings dialog → MarkActivity → cadence → 200 ms before save fires. |
| Wave 3 PR 3.6 races re-emerge | Very low — plan does NOT touch worker ownership, only cadence | Re-run PR 3.6's test cases per §5 Nyquist. |

## 9. Out of scope (do NOT bundle)

- HookLifecycle thread stack shrink (`STACK_SIZE_PARAM_IS_A_RESERVATION`) — separate ~vài-KB cosmetic fix.
- `Perf::Histogram` enabled-default check optimization.
- Wave 2 RCU snapshot lifecycle review (whether snapshots get released sooner).
- HeartbeatPublisher 100 ms Sleep loop fix — only relevant if watchdog enabled.
- `SetProcessInformation(EcoQoS)` for minimized state.

Each is a possible follow-up after this plan ships clean and is measured to have or not have residual gap.

---

**Sign-off**: anh confirms commitment to this plan + acceptance criteria §3.4 before Step 1 starts.
