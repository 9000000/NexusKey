# Hook Self-Healer + Watchdog Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Refactor Anti-Dorion self-heal into `HookSelfHealer` module + ship `NexusKeyWatchdog.exe` process supervisor for v3.0.0.

**Architecture:** Phase 1 extracts `IHookSelfHealer` + `RawInputSelfHealer` from `HookEngine` (in-process, fixes 4 CODING_RULES violations). Phase 2 ships a separate `NexusKeyWatchdog.exe` that monitors `NexusKey.exe` via a named event heartbeat (30s pulse, 90s timeout) and respawns on crash via Task Scheduler at-logon registration.

**Tech Stack:** C++20, MSVC 2022, CMake, Google Test, Win32 (Raw Input, Hooks, Named Events, Task Scheduler PowerShell shell-out, IShellLink).

**Spec:** [`docs/plans/2026-05-08-hook-self-healer-watchdog-design.md`](2026-05-08-hook-self-healer-watchdog-design.md) (commit `8a7ae1c`)

**Sequence rule:** HookEngine refactor work — does not violate "HookEngine before TypingEngine" rule.

---

## File Structure

### Phase 1 (HookSelfHealer extraction)

| File | Status | Responsibility |
|---|---|---|
| `src/app/system/HookSelfHealer.h` | NEW | `IHookSelfHealer` interface + `RawInputSelfHealer` declaration |
| `src/app/system/HookSelfHealer.cpp` | NEW | RawInputSelfHealer impl (Raw Input window + reinstall trigger) |
| `tests/HookSelfHealerTest.cpp` | NEW | 3 unit tests; mock reinstall callback |
| `src/app/system/HookEngine.h` | MOD | Remove 5 self-heal fields + 3 method decls (lines 449-463); add `selfHealer_` unique_ptr; add `ReinstallKeyboardAndMouseHooks()` private method decl |
| `src/app/system/HookEngine.cpp` | MOD | Replace inline self-heal calls with `selfHealer_->RecordHookFire()`; remove `CreateRawInputMonitor()`/`DestroyRawInputMonitor()`/`RawInputWndProc()` (lines 3439-3561); extract `ReinstallKeyboardAndMouseHooks()` for healer callback; fix comment at line 3532 |
| `CMakeLists.txt` | MOD | Add `HookSelfHealer.{h,cpp}` to `NextKeyApp` (after line 158) and `NextKeyLite` (after line 242); add `tests/HookSelfHealerTest.cpp` to `NEXTKEY_TEST_SOURCES` (line ~445) |

### Phase 2 (Watchdog)

| File | Status | Responsibility |
|---|---|---|
| `src/app/system/HeartbeatPublisher.h` | NEW | `HeartbeatPublisher` class — owns named event + 30s thread |
| `src/app/system/HeartbeatPublisher.cpp` | NEW | Pulse loop, graceful-shutdown flag publishing |
| `src/watchdog/main.cpp` | NEW | Watchdog entry point — heartbeat wait + respawn |
| `src/watchdog/CMakeLists.txt` | NEW | Separate `NexusKeyWatchdog` target |
| `src/app/main.cpp` | MOD | Start `HeartbeatPublisher`; register watchdog task at first run; set graceful-shutdown flag on tray quit |
| `src/app/system/StartupHelper.h` | MOD | Add `WATCHDOG_TASK_NAME`, `CreateWatchdogScheduledTask()`, `RemoveWatchdogScheduledTask()`, `IsWatchdogTaskRegistered()` (paralleling existing helpers) |
| `CMakeLists.txt` | MOD | `add_subdirectory(src/watchdog)` (after `add_subdirectory(tools/NextKeyTestRunner)` line 489); add `HeartbeatPublisher.{h,cpp}` to NextKeyApp + NextKeyLite |
| `tests/HeartbeatPublisherTest.cpp` | NEW | Windows-only test — verify pulse + graceful flag |

---

## PHASE 1 — HookSelfHealer Extraction

### Task 1.1: Create IHookSelfHealer interface header

**Files:**
- Create: `src/app/system/HookSelfHealer.h`

- [ ] **Step 1: Write the header**

```cpp
// NexusKey - Hook Self-Healer (Windows-only)
// SPDX-License-Identifier: GPL-3.0-only
//
// Detects when an external app installs a higher-priority LL keyboard
// hook above NexusKey's hook and skips CallNextHookEx (Anti-Dorion).
// Strategy: dual-channel comparison — Raw Input always reaches us
// (kernel-level), so missing LL hook fires while Raw Input fires =
// hook hijacked. Reinstall hooks via injected callback to jump back
// to top of LIFO chain.
//
// Threading: all methods (except RecordHookFire) are called from the
// hook thread. RecordHookFire is called inline from LowLevelKeyboardProc
// — same thread as the WndProc, no atomic needed.

#pragma once

#ifdef _WIN32
#include <Windows.h>
#endif

#include <functional>
#include <memory>

namespace NextKey {

/// Reinstall callback — invoked by the healer when it detects hook death.
/// Implementation must call UnhookWindowsHookEx + SetWindowsHookExW for both
/// keyboard and mouse hooks (kept paired for chain symmetry). Returns true
/// on success; false signals the healer to keep cooldown unreset so future
/// retries are still possible.
using ReinstallHookFn = std::function<bool()>;

class IHookSelfHealer {
public:
    virtual ~IHookSelfHealer() = default;
    virtual bool Start() = 0;
    virtual void Stop() = 0;
    virtual void RecordHookFire() = 0;
};

#ifdef _WIN32

class RawInputSelfHealer : public IHookSelfHealer {
public:
    RawInputSelfHealer(HINSTANCE hInst, ReinstallHookFn reinstaller);
    ~RawInputSelfHealer() override;

    RawInputSelfHealer(const RawInputSelfHealer&) = delete;
    RawInputSelfHealer& operator=(const RawInputSelfHealer&) = delete;

    bool Start() override;
    void Stop() override;
    void RecordHookFire() override;

private:
    static LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);

    HINSTANCE hInst_ = nullptr;
    ReinstallHookFn reinstaller_;
    HWND hwnd_ = nullptr;
    DWORD lastLlHookTime_ = 0;
    uint8_t consecutiveRawMisses_ = 0;
    DWORD lastSelfHealTime_ = 0;

    static constexpr UINT_PTR SELF_HEAL_TIMER_ID = 42;
    static constexpr uint8_t SELF_HEAL_MISS_THRESHOLD = 3;
    static constexpr DWORD SELF_HEAL_COOLDOWN_MS = 10000;
    static constexpr DWORD SELF_HEAL_HOOK_FRESHNESS_MS = 200;

    static thread_local RawInputSelfHealer* tlsActive_;
};

#endif  // _WIN32

}  // namespace NextKey
```

- [ ] **Step 2: Commit**

```bash
git add src/app/system/HookSelfHealer.h
git commit -m "feat(hook): IHookSelfHealer interface + RawInputSelfHealer header

Phase 1 of v3.0.0 hook self-heal extraction. No behavior change — header
declares the interface that subsequent tasks implement and wire into
HookEngine."
```

---

### Task 1.2: Write the failing test for HookSelfHealer

**Files:**
- Create: `tests/HookSelfHealerTest.cpp`

- [ ] **Step 1: Write 3 tests**

```cpp
// NexusKey — HookSelfHealer unit tests (Linux-runnable via mock callback)
// SPDX-License-Identifier: GPL-3.0-only

#include <gtest/gtest.h>
#include "app/system/HookSelfHealer.h"

#ifdef _WIN32

#include <atomic>

namespace NextKey {

// Test fixture exposes private trigger paths via a friend or via a
// behavior-test approach. We test via observable side-effects: the
// reinstaller callback's invocation count.

class HookSelfHealerTest : public ::testing::Test {
protected:
    std::atomic<int> reinstallerCallCount_{0};
    bool reinstallerReturn_ = true;

    ReinstallHookFn MakeReinstaller() {
        return [this]() {
            reinstallerCallCount_.fetch_add(1, std::memory_order_relaxed);
            return reinstallerReturn_;
        };
    }
};

TEST_F(HookSelfHealerTest, ConstructAndDestructWithoutStart) {
    RawInputSelfHealer healer(GetModuleHandleW(nullptr), MakeReinstaller());
    // Destructor without Start should be safe.
    EXPECT_EQ(reinstallerCallCount_.load(), 0);
}

TEST_F(HookSelfHealerTest, RecordHookFireDoesNotCrashWithoutStart) {
    RawInputSelfHealer healer(GetModuleHandleW(nullptr), MakeReinstaller());
    healer.RecordHookFire();  // Must not crash even without Start.
    EXPECT_EQ(reinstallerCallCount_.load(), 0);
}

TEST_F(HookSelfHealerTest, StopWithoutStartIsIdempotent) {
    RawInputSelfHealer healer(GetModuleHandleW(nullptr), MakeReinstaller());
    healer.Stop();  // Idempotent — should not crash, should not invoke reinstaller.
    healer.Stop();
    EXPECT_EQ(reinstallerCallCount_.load(), 0);
}

}  // namespace NextKey

#endif  // _WIN32
```

- [ ] **Step 2: Add test to CMakeLists.txt**

Edit `CMakeLists.txt` — find the `NEXTKEY_TEST_SOURCES` block (around line 425-450) and add `tests/HookSelfHealerTest.cpp`. Locate the section where `MainThreadWorkerTests.cpp` is listed (cross-platform tests) vs Windows-only — add HookSelfHealer to the Windows-only section since it requires `<Windows.h>`:

```cmake
# Windows-only tests (require ConfigManager, ConfigEvent, Win32 APIs)
if(WIN32)
    list(APPEND NEXTKEY_TEST_SOURCES
        tests/ConfigManagerTest.cpp
        tests/ConfigEventTest.cpp
        tests/HookSelfHealerTest.cpp                # <-- NEW
        src/app/system/HookSelfHealer.cpp           # <-- NEW (impl needed for linkage; lands in Task 1.3)
        # ...
    )
endif()
```

- [ ] **Step 3: Run on Linux to verify it skips correctly**

```bash
cd /home/phatmt/code/NexusKey
cmake -B build-linux -G "Unix Makefiles" -DCMAKE_BUILD_TYPE=Debug
cmake --build build-linux --target NextKeyTests
./build-linux/tests/NextKeyTests --gtest_filter="HookSelfHealerTest.*"
```
Expected: `0 tests ran` (Linux skips Windows-only block) — confirms test infrastructure does not error on the new file.

- [ ] **Step 4: Commit (do NOT yet stage HookSelfHealer.cpp — comes in Task 1.3)**

```bash
git add tests/HookSelfHealerTest.cpp CMakeLists.txt
git commit -m "test(hook): HookSelfHealer test scaffold

3 lifecycle tests (construct, RecordHookFire-without-Start, Stop
idempotent). Windows-only — Linux build skips the block. Test will
link against HookSelfHealer.cpp added in Task 1.3."
```

---

### Task 1.3: Implement RawInputSelfHealer.cpp

**Files:**
- Create: `src/app/system/HookSelfHealer.cpp`

- [ ] **Step 1: Write the implementation**

This ports `HookEngine::CreateRawInputMonitor` / `DestroyRawInputMonitor` / `RawInputWndProc` (current `HookEngine.cpp:3439-3561`) into the standalone class, with the 4 CODING_RULES fixes baked in.

```cpp
// NexusKey - Hook Self-Healer Implementation (Windows-only)
// SPDX-License-Identifier: GPL-3.0-only

#include "HookSelfHealer.h"

#ifdef _WIN32

#include "core/Debug.h"

namespace NextKey {

thread_local RawInputSelfHealer* RawInputSelfHealer::tlsActive_ = nullptr;

RawInputSelfHealer::RawInputSelfHealer(HINSTANCE hInst, ReinstallHookFn reinstaller)
    : hInst_(hInst), reinstaller_(std::move(reinstaller)) {}

RawInputSelfHealer::~RawInputSelfHealer() {
    Stop();
}

bool RawInputSelfHealer::Start() {
    if (hwnd_) return true;  // Idempotent

    WNDCLASSEXW wc = { sizeof(wc) };
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInst_;
    wc.lpszClassName = L"NexusKey_HookSelfHealer";
    if (!RegisterClassExW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        return false;
    }

    // Hidden desktop-hierarchy window (NOT HWND_MESSAGE — RIDEV_INPUTSINK
    // requires desktop hierarchy to receive WM_INPUT in background).
    hwnd_ = CreateWindowExW(0, wc.lpszClassName, nullptr,
                             0, 0, 0, 0, 0,
                             nullptr, nullptr, hInst_, nullptr);
    if (!hwnd_) return false;

    // Bind TLS so WndProc can find this instance — matches the previous
    // s_instance pattern but per-thread (avoids cross-instance interference
    // if HookEngine ever multi-instantiates).
    tlsActive_ = this;

    RAWINPUTDEVICE rid = {};
    rid.usUsagePage = 0x01;  // Generic Desktop
    rid.usUsage     = 0x06;  // Keyboard
    rid.dwFlags     = RIDEV_INPUTSINK;
    rid.hwndTarget  = hwnd_;
    if (!RegisterRawInputDevices(&rid, 1, sizeof(rid))) {
        DestroyWindow(hwnd_);
        hwnd_ = nullptr;
        tlsActive_ = nullptr;
        return false;
    }
    return true;
}

void RawInputSelfHealer::Stop() {
    if (!hwnd_) return;

    KillTimer(hwnd_, SELF_HEAL_TIMER_ID);

    RAWINPUTDEVICE rid = {};
    rid.usUsagePage = 0x01;
    rid.usUsage     = 0x06;
    rid.dwFlags     = RIDEV_REMOVE;
    rid.hwndTarget  = nullptr;
    RegisterRawInputDevices(&rid, 1, sizeof(rid));

    DestroyWindow(hwnd_);
    hwnd_ = nullptr;
    tlsActive_ = nullptr;
    UnregisterClassW(L"NexusKey_HookSelfHealer", hInst_);
}

void RawInputSelfHealer::RecordHookFire() {
    // Same thread as WndProc — no lock, no atomic.
    lastLlHookTime_ = GetTickCount();
}

LRESULT CALLBACK RawInputSelfHealer::WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    // Rule 11.5 fix: top-level catch — mirrors LowLevelKeyboardProc pattern.
    try {
        RawInputSelfHealer* self = tlsActive_;

        if (msg == WM_TIMER && wParam == SELF_HEAL_TIMER_ID) {
            KillTimer(hwnd, SELF_HEAL_TIMER_ID);
            if (!self) return 0;

            HOOK_LOG(L"  SelfHeal: timer fired — invoking reinstaller");
            NEXTKEY_LOG(L"SelfHeal: invoking reinstaller (timer)");

            const bool ok = self->reinstaller_ ? self->reinstaller_() : false;

            if (ok) {
                self->consecutiveRawMisses_ = 0;
                self->lastSelfHealTime_ = GetTickCount();
                HOOK_LOG(L"  SelfHeal: reinstaller OK");
            } else {
                // Rule 3 fix: do NOT reset cooldown timestamp on failure.
                // This allows future retries instead of locking the
                // healer in an infinite-cooldown state.
                NEXTKEY_LOG(L"SelfHeal: reinstaller FAILED (err=%lu) — retry on next miss streak",
                            GetLastError());
            }
            return 0;
        }

        if (msg != WM_INPUT) return DefWindowProcW(hwnd, msg, wParam, lParam);
        if (!self) return DefWindowProcW(hwnd, msg, wParam, lParam);

        // Extract Raw Input (stack-only, no allocation).
        UINT size = 0;
        GetRawInputData(reinterpret_cast<HRAWINPUT>(lParam),
                        RID_INPUT, nullptr, &size, sizeof(RAWINPUTHEADER));
        if (size == 0 || size > sizeof(RAWINPUT) + 32) {
            return DefWindowProcW(hwnd, msg, wParam, lParam);
        }

        alignas(RAWINPUT) BYTE buf[sizeof(RAWINPUT) + 32];
        if (GetRawInputData(reinterpret_cast<HRAWINPUT>(lParam),
                            RID_INPUT, buf, &size, sizeof(RAWINPUTHEADER)) == UINT(-1)) {
            return DefWindowProcW(hwnd, msg, wParam, lParam);
        }

        auto* raw = reinterpret_cast<RAWINPUT*>(buf);

        // Filter: physical keyboard key-down only.
        if (raw->header.dwType != RIM_TYPEKEYBOARD) return DefWindowProcW(hwnd, msg, wParam, lParam);
        if (raw->header.hDevice == nullptr) return DefWindowProcW(hwnd, msg, wParam, lParam);
        if (raw->data.keyboard.Flags & RI_KEY_BREAK) return DefWindowProcW(hwnd, msg, wParam, lParam);

        // Comment fix (was "< 0.5μs total" — incorrect; GetRawInputData ×2
        // alone is ~5-10μs). Hot path realistically ~5-20μs, well within
        // the 1ms hook-callback budget (Rule 11.1).
        DWORD now = GetTickCount();
        DWORD elapsed = now - self->lastLlHookTime_;

        if (elapsed < SELF_HEAL_HOOK_FRESHNESS_MS) {
            self->consecutiveRawMisses_ = 0;
            return DefWindowProcW(hwnd, msg, wParam, lParam);
        }

        self->consecutiveRawMisses_++;
        HOOK_LOG(L"  SelfHeal: LL hook miss #%u (elapsed=%ums, vk=0x%02X)",
                 self->consecutiveRawMisses_, elapsed, raw->data.keyboard.VKey);

        if (self->consecutiveRawMisses_ >= SELF_HEAL_MISS_THRESHOLD) {
            DWORD sinceLast = now - self->lastSelfHealTime_;
            if (sinceLast < SELF_HEAL_COOLDOWN_MS) {
                HOOK_LOG(L"  SelfHeal: cooldown (%ums left)",
                         SELF_HEAL_COOLDOWN_MS - sinceLast);
                return DefWindowProcW(hwnd, msg, wParam, lParam);
            }

            // Random delay 10-50ms — avoid lockstep with hijacker reinstall.
            UINT delay = 10 + (GetTickCount() % 41);
            SetTimer(hwnd, SELF_HEAL_TIMER_ID, delay, nullptr);
            HOOK_LOG(L"  SelfHeal: HOOK DEAD — scheduled reinstaller in %ums", delay);
        }

        return DefWindowProcW(hwnd, msg, wParam, lParam);
    } catch (...) {
        // Rule 11.5: swallow + no rethrow. Hook-thread message pump must
        // not abort the process. State stays as-is until next message.
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
}

}  // namespace NextKey

#endif  // _WIN32
```

- [ ] **Step 2: Verify Linux build still compiles (impl is `#ifdef _WIN32` so should be empty TU)**

```bash
cmake --build build-linux --target NextKeyTests 2>&1 | tail -20
```
Expected: success. The 3 tests pass on Linux because the entire test block is `#ifdef _WIN32` — they evaluate to 0 ran, 0 failed.

- [ ] **Step 3: Commit**

```bash
git add src/app/system/HookSelfHealer.cpp
git commit -m "feat(hook): RawInputSelfHealer implementation

Port Anti-Dorion logic from HookEngine into standalone class. Bundle
4 CODING_RULES fixes:
- Rule 4: interface IHookSelfHealer + impl class (was 5 fields + 3
  methods member of HookEngine)
- Rule 9: constants UPPER_SNAKE (was Google-style k-prefix)
- Rule 3: reinstaller failure does NOT reset cooldown — allows retry
  (was infinite-cooldown lock on SetWindowsHookExW failure)
- Rule 11.5: top-level try/catch in WndProc (was missing — only LL
  hook callback had it)

Behavior identical to previous in-line implementation modulo the
Rule 3 fix above."
```

---

### Task 1.4: Wire HookEngine to use HookSelfHealer

**Files:**
- Modify: `src/app/system/HookEngine.h:449-463` — remove inline self-heal fields/methods, add `selfHealer_` member, add `ReinstallKeyboardAndMouseHooks()` decl
- Modify: `src/app/system/HookEngine.cpp` — multiple sites

- [ ] **Step 1: Update HookEngine.h — remove old self-heal fields, add new member**

Find the block at `HookEngine.h:449-463` (the self-heal members + method declarations). Replace with:

```cpp
    // Self-heal — extracted to HookSelfHealer module (v3.0.0).
    // Owned via interface for testability + future swap (e.g., out-of-process
    // supervisor variant).
    std::unique_ptr<IHookSelfHealer> selfHealer_;
```

Above the section's existing `RawInputWndProc` declaration removal, add the new private method declaration (place near other private hook lifecycle methods, e.g. near `HookThreadProc`):

```cpp
    // Reinstall both keyboard and mouse LL hooks. Used by HookSelfHealer
    // when it detects the LL hook was hijacked. Returns false if either
    // SetWindowsHookExW call fails (caller logs GetLastError).
    [[nodiscard]] bool ReinstallKeyboardAndMouseHooks();
```

At the top of the file, add include:
```cpp
#include "HookSelfHealer.h"
```

- [ ] **Step 2: Update HookEngine.cpp — replace inline self-heal heartbeat**

In `LowLevelKeyboardProc` around lines 668-672:

OLD:
```cpp
        // Self-heal heartbeat: record that LL hook is alive.
        // Same thread as RawInputWndProc — no lock, no atomic.
        if (self) {
            self->lastLlHookTime_ = GetTickCount();
        }
```

NEW:
```cpp
        // Self-heal heartbeat — delegated to HookSelfHealer (Rule 4 extraction).
        if (self && self->selfHealer_) {
            self->selfHealer_->RecordHookFire();
        }
```

- [ ] **Step 3: Update HookEngine.cpp — wire HookSelfHealer Start/Stop**

In `HookThreadProc` around lines 311-313 where `CreateRawInputMonitor()` is called, replace with:

```cpp
    // Start Raw Input self-healer (best-effort — hook still works without it).
    selfHealer_ = std::make_unique<RawInputSelfHealer>(
        cachedHInstance_,
        [this]() { return ReinstallKeyboardAndMouseHooks(); });
    if (!selfHealer_->Start()) {
        HOOK_LOG(L"HookThreadProc: HookSelfHealer Start FAILED (self-healing disabled)");
        selfHealer_.reset();
    }
```

In the corresponding teardown (find paired `DestroyRawInputMonitor()` call), replace with:

```cpp
    if (selfHealer_) {
        selfHealer_->Stop();
        selfHealer_.reset();
    }
```

- [ ] **Step 4: Update HookEngine.cpp — extract ReinstallKeyboardAndMouseHooks()**

Add this new private method (place after the hook teardown methods, around the existing reinstall-on-WM_TIMER block at lines 322-332 was). The method consolidates the unhook+reinstall pattern with proper error checking (Rule 3 fix):

```cpp
bool HookEngine::ReinstallKeyboardAndMouseHooks() {
    if (keyboardHook_) {
        UnhookWindowsHookEx(keyboardHook_);
        keyboardHook_ = SetWindowsHookExW(
            WH_KEYBOARD_LL, LowLevelKeyboardProc, cachedHInstance_, 0);
        if (!keyboardHook_) {
            HOOK_LOG(L"  ReinstallKeyboardAndMouseHooks: keyboard hook FAILED err=%lu",
                     GetLastError());
            return false;
        }
    }
    if (mouseHook_) {
        UnhookWindowsHookEx(mouseHook_);
        mouseHook_ = SetWindowsHookExW(
            WH_MOUSE_LL, LowLevelMouseProc, cachedHInstance_, 0);
        if (!mouseHook_) {
            HOOK_LOG(L"  ReinstallKeyboardAndMouseHooks: mouse hook FAILED err=%lu",
                     GetLastError());
            return false;
        }
    }
    HOOK_LOG(L"  ReinstallKeyboardAndMouseHooks: both hooks reinstalled OK");
    return true;
}
```

- [ ] **Step 5: Update HookEngine.cpp — remove old method bodies (lines 3439-3561)**

Delete the entire old block:
- `bool HookEngine::CreateRawInputMonitor()` (was lines ~3439-3464)
- `void HookEngine::DestroyRawInputMonitor()` (was lines ~3466-3479)
- `LRESULT CALLBACK HookEngine::RawInputWndProc(...)` (was lines ~3481-3561)

These methods are now superseded by `RawInputSelfHealer` impl. Verify the closing `}  // namespace NextKey` at line ~3563 is preserved.

- [ ] **Step 6: Update HookEngine.cpp — also delete fields from .h that are now in HookSelfHealer**

Re-verify these fields were removed in Step 1 (they should no longer exist in HookEngine.h):
- `HWND rawInputHwnd_`
- `DWORD lastLlHookTime_`
- `uint8_t consecutiveRawMisses_`
- `DWORD lastSelfHealTime_`
- `static constexpr UINT_PTR kSelfHealTimerId`
- `static constexpr uint8_t kSelfHealMissThreshold`
- `static constexpr DWORD kSelfHealCooldownMs`
- `static constexpr DWORD kSelfHealHookFreshnessMs`
- `static LRESULT CALLBACK RawInputWndProc(HWND, UINT, WPARAM, LPARAM);`
- `bool CreateRawInputMonitor();`
- `void DestroyRawInputMonitor();`

- [ ] **Step 7: Add HookSelfHealer.cpp to CMakeLists.txt for production targets**

In `CMakeLists.txt`, find `NextKeyApp` source list around line 158 and insert after `HookEngine.cpp`:

```cmake
        src/app/system/HookSelfHealer.h
        src/app/system/HookSelfHealer.cpp
```

Find `NextKeyLite` source list around line 240 and insert after `HookEngine.cpp`:

```cmake
        src/app/system/HookSelfHealer.h
        src/app/system/HookSelfHealer.cpp
```

- [ ] **Step 8: Linux build sanity check**

```bash
cmake --build build-linux --target NextKeyTests 2>&1 | tail -10
./build-linux/tests/NextKeyTests --gtest_filter="HookSelfHealerTest.*"
```
Expected: build OK. HookSelfHealerTest.* will skip on Linux (Windows-only block) — confirms cross-platform stability. Total test count remains the existing 1555+.

- [ ] **Step 9: Commit**

```bash
git add src/app/system/HookEngine.h src/app/system/HookEngine.cpp CMakeLists.txt
git commit -m "refactor(hook): wire HookEngine to use HookSelfHealer

Replace inline Raw Input self-heal with injected IHookSelfHealer.
HookEngine now owns a unique_ptr<IHookSelfHealer>, instantiated as
RawInputSelfHealer on hook thread startup. Reinstall callback
extracted to ReinstallKeyboardAndMouseHooks() with proper error
checking (returns false on SetWindowsHookExW failure → healer keeps
cooldown unreset, allowing retry).

Removes ~125 LOC from HookEngine.cpp (3439-3561), 11 fields/methods
from HookEngine.h (lines 449-463). Behavior preserved modulo Rule 3
fix (no infinite-cooldown lock on reinstall failure).

Phase 1 of v3.0.0 watchdog work. Phase 2 (out-of-process supervisor)
ships separately."
```

---

### Task 1.5: Phase 1 verification gate (Windows manual smoke)

**Files:** None (verification only)

- [ ] **Step 1: Build Windows MSVC**

From WSL:
```bash
powershell.exe -Command "cd '\\wsl.localhost\Ubuntu-24.04\home\phatmt\code\NexusKey\build'; cmake --build . --target NexusKey --config Debug 2>&1" | tail -30
```
Expected: build success, no LNK2019 errors.

- [ ] **Step 2: Manual smoke — Anti-Dorion still works**

1. Install Dorion (Discord webview2 client) per Issue #99 reproduction setup
2. Launch NexusKey + Dorion
3. Try typing Vietnamese text in Dorion message box
4. Expected: Vietnamese text composes correctly. If hijack triggers, log should show:
   ```
   SelfHeal: LL hook miss #1, #2, #3
   SelfHeal: HOOK DEAD — scheduled reinstaller in NNms
   SelfHeal: timer fired — invoking reinstaller
   ReinstallKeyboardAndMouseHooks: both hooks reinstalled OK
   ```

- [ ] **Step 3: Manual smoke — normal-app regression**

Open Notepad, type Vietnamese text. Expected: composes correctly, no spurious self-heal triggers in log.

- [ ] **Step 4: Decision gate**

If both smoke tests pass → Phase 1 complete, proceed to Phase 2.
If Anti-Dorion broken → diff against pre-Task-1.4 HookEngine, check `RecordHookFire` is called every keydown.

---

## PHASE 2 — NexusKeyWatchdog.exe

### Task 2.1: HeartbeatPublisher header + impl + test

**Files:**
- Create: `src/app/system/HeartbeatPublisher.h`
- Create: `src/app/system/HeartbeatPublisher.cpp`
- Create: `tests/HeartbeatPublisherTest.cpp`

- [ ] **Step 1: Write HeartbeatPublisher.h**

```cpp
// NexusKey - Heartbeat Publisher (Windows-only)
// SPDX-License-Identifier: GPL-3.0-only
//
// Publishes a 30s heartbeat to a named event so NexusKeyWatchdog.exe
// can detect process liveness. Also publishes a graceful-shutdown flag
// (named event in signaled state) so the watchdog distinguishes user-
// initiated quit from crash.
//
// Event names (Local\ session-scoped, kernel objects):
//   Local\NexusKeyHeartbeat         — pulsed every 30s
//   Local\NexusKeyGracefulShutdown  — signaled by SignalGracefulShutdown
//                                     before NexusKey exits via tray quit

#pragma once

#ifdef _WIN32

#include <Windows.h>
#include <atomic>
#include <thread>

namespace NextKey {

inline constexpr const wchar_t* HEARTBEAT_EVENT_NAME = L"Local\\NexusKeyHeartbeat";
inline constexpr const wchar_t* GRACEFUL_SHUTDOWN_EVENT_NAME = L"Local\\NexusKeyGracefulShutdown";
inline constexpr DWORD HEARTBEAT_INTERVAL_MS = 30'000;

class HeartbeatPublisher {
public:
    HeartbeatPublisher() = default;
    ~HeartbeatPublisher();

    HeartbeatPublisher(const HeartbeatPublisher&) = delete;
    HeartbeatPublisher& operator=(const HeartbeatPublisher&) = delete;

    /// Open the named events and spawn the pulse thread. Returns false
    /// if event creation fails (caller treats as best-effort — NexusKey
    /// still functions, just no auto-respawn).
    [[nodiscard]] bool Start();

    /// Stop the pulse thread and close handles. Idempotent.
    void Stop();

    /// Set the graceful-shutdown flag — call before tray-quit exit so
    /// the watchdog does NOT respawn NexusKey.
    void SignalGracefulShutdown();

private:
    void Run() noexcept;

    std::thread thread_;
    std::atomic<bool> stopRequested_{false};
    HANDLE heartbeatEvent_ = nullptr;
    HANDLE gracefulShutdownEvent_ = nullptr;
};

}  // namespace NextKey

#endif  // _WIN32
```

- [ ] **Step 2: Write HeartbeatPublisher.cpp**

```cpp
// NexusKey - Heartbeat Publisher Implementation (Windows-only)
// SPDX-License-Identifier: GPL-3.0-only

#include "HeartbeatPublisher.h"

#ifdef _WIN32

#include "core/Debug.h"

namespace NextKey {

HeartbeatPublisher::~HeartbeatPublisher() {
    Stop();
}

bool HeartbeatPublisher::Start() {
    if (heartbeatEvent_) return true;  // Idempotent

    // Manual-reset event — we pulse it via SetEvent + ResetEvent.
    heartbeatEvent_ = CreateEventW(nullptr, TRUE, FALSE, HEARTBEAT_EVENT_NAME);
    if (!heartbeatEvent_) {
        NEXTKEY_LOG(L"HeartbeatPublisher: CreateEvent heartbeat FAILED err=%lu",
                    GetLastError());
        return false;
    }

    gracefulShutdownEvent_ = CreateEventW(nullptr, TRUE, FALSE, GRACEFUL_SHUTDOWN_EVENT_NAME);
    if (!gracefulShutdownEvent_) {
        NEXTKEY_LOG(L"HeartbeatPublisher: CreateEvent graceful FAILED err=%lu",
                    GetLastError());
        CloseHandle(heartbeatEvent_);
        heartbeatEvent_ = nullptr;
        return false;
    }

    stopRequested_.store(false, std::memory_order_relaxed);
    thread_ = std::thread([this]() { Run(); });
    return true;
}

void HeartbeatPublisher::Stop() {
    if (!heartbeatEvent_ && !gracefulShutdownEvent_) return;

    stopRequested_.store(true, std::memory_order_release);
    if (thread_.joinable()) thread_.join();

    if (heartbeatEvent_) {
        CloseHandle(heartbeatEvent_);
        heartbeatEvent_ = nullptr;
    }
    if (gracefulShutdownEvent_) {
        CloseHandle(gracefulShutdownEvent_);
        gracefulShutdownEvent_ = nullptr;
    }
}

void HeartbeatPublisher::SignalGracefulShutdown() {
    if (gracefulShutdownEvent_) {
        SetEvent(gracefulShutdownEvent_);
    }
}

void HeartbeatPublisher::Run() noexcept {
    // Pulse loop: SetEvent + ResetEvent + sleep. We use Sleep (not
    // condition_variable) because the cost of tearing down on shutdown
    // is at most one HEARTBEAT_INTERVAL_MS wait — acceptable for a 30s
    // interval. Stop() join blocks for ≤30s in worst case.
    while (!stopRequested_.load(std::memory_order_acquire)) {
        if (heartbeatEvent_) {
            SetEvent(heartbeatEvent_);
            ResetEvent(heartbeatEvent_);  // Pulse — watchdog Wait sees signaled then auto-resets-effective
        }
        // Granular sleep so Stop() returns within ~100ms instead of 30s.
        for (DWORD waited = 0; waited < HEARTBEAT_INTERVAL_MS; waited += 100) {
            if (stopRequested_.load(std::memory_order_acquire)) return;
            Sleep(100);
        }
    }
}

}  // namespace NextKey

#endif  // _WIN32
```

- [ ] **Step 3: Write HeartbeatPublisherTest.cpp**

```cpp
// NexusKey — HeartbeatPublisher unit tests (Windows-only)
// SPDX-License-Identifier: GPL-3.0-only

#include <gtest/gtest.h>
#include "app/system/HeartbeatPublisher.h"

#ifdef _WIN32

namespace NextKey {

TEST(HeartbeatPublisherTest, StartCreatesNamedEvents) {
    HeartbeatPublisher pub;
    ASSERT_TRUE(pub.Start());

    // Verify external observer can open the events.
    HANDLE hb = OpenEventW(SYNCHRONIZE, FALSE, HEARTBEAT_EVENT_NAME);
    HANDLE gs = OpenEventW(SYNCHRONIZE, FALSE, GRACEFUL_SHUTDOWN_EVENT_NAME);
    EXPECT_NE(hb, nullptr);
    EXPECT_NE(gs, nullptr);
    if (hb) CloseHandle(hb);
    if (gs) CloseHandle(gs);

    pub.Stop();
}

TEST(HeartbeatPublisherTest, GracefulShutdownFlagSignalable) {
    HeartbeatPublisher pub;
    ASSERT_TRUE(pub.Start());

    HANDLE gs = OpenEventW(SYNCHRONIZE, FALSE, GRACEFUL_SHUTDOWN_EVENT_NAME);
    ASSERT_NE(gs, nullptr);

    // Initially not signaled.
    EXPECT_EQ(WaitForSingleObject(gs, 0), WAIT_TIMEOUT);

    pub.SignalGracefulShutdown();

    // Now signaled.
    EXPECT_EQ(WaitForSingleObject(gs, 100), WAIT_OBJECT_0);

    CloseHandle(gs);
    pub.Stop();
}

TEST(HeartbeatPublisherTest, StopIsIdempotent) {
    HeartbeatPublisher pub;
    ASSERT_TRUE(pub.Start());
    pub.Stop();
    pub.Stop();  // No crash.
}

}  // namespace NextKey

#endif  // _WIN32
```

- [ ] **Step 4: Add to CMakeLists.txt**

In `NextKeyApp` sources (around line 158), insert after `HookSelfHealer.cpp`:
```cmake
        src/app/system/HeartbeatPublisher.h
        src/app/system/HeartbeatPublisher.cpp
```

Same for `NextKeyLite` (around line 242).

In Windows-only test block (around line 452):
```cmake
        tests/HeartbeatPublisherTest.cpp
        src/app/system/HeartbeatPublisher.cpp
```

- [ ] **Step 5: Linux build sanity check**

```bash
cmake --build build-linux --target NextKeyTests 2>&1 | tail -5
```
Expected: success. HeartbeatPublisher.cpp `#ifdef _WIN32` empty TU on Linux.

- [ ] **Step 6: Commit**

```bash
git add src/app/system/HeartbeatPublisher.h \
        src/app/system/HeartbeatPublisher.cpp \
        tests/HeartbeatPublisherTest.cpp \
        CMakeLists.txt
git commit -m "feat(watchdog): HeartbeatPublisher — 30s pulse + graceful shutdown

Phase 2 / 1: NexusKey-side heartbeat. Owns 2 named events:
- Local\\NexusKeyHeartbeat (pulsed every 30s on dedicated thread)
- Local\\NexusKeyGracefulShutdown (set by tray-quit before exit)

Watchdog (next task) opens both events and uses heartbeat liveness
+ graceful flag absence to decide respawn."
```

---

### Task 2.2: Wire HeartbeatPublisher into main.cpp

**Files:**
- Modify: `src/app/main.cpp`

- [ ] **Step 1: Locate startup sequence**

Open `src/app/main.cpp`. Find the main app initialization (after `g_sharedState` setup, near where `MainThreadWorker` or `HookEngine` is started — exact line varies).

- [ ] **Step 2: Add HeartbeatPublisher start near app init**

Add at the top of main.cpp:
```cpp
#include "system/HeartbeatPublisher.h"
```

Near the global state declarations:
```cpp
static NextKey::HeartbeatPublisher g_heartbeat;
```

After successful HookEngine startup but before the message loop begins:
```cpp
if (!g_heartbeat.Start()) {
    NEXTKEY_LOG(L"HeartbeatPublisher start failed — watchdog auto-respawn disabled");
    // Continue: not fatal; NexusKey still functions without watchdog.
}
```

- [ ] **Step 3: Wire graceful shutdown on tray-quit**

Find the tray-quit handler (search for `WM_DESTROY` or `OnTrayQuit` or similar in main.cpp). Add **before** the actual exit/PostQuitMessage:
```cpp
g_heartbeat.SignalGracefulShutdown();
```

Find any other clean-exit paths (e.g., update-installer-handover) and add the same call there.

- [ ] **Step 4: Wire Stop() at app cleanup**

After the message loop exits, before `return 0`:
```cpp
g_heartbeat.Stop();
```

- [ ] **Step 5: Commit**

```bash
git add src/app/main.cpp
git commit -m "feat(watchdog): wire HeartbeatPublisher in main.cpp

Start heartbeat after HookEngine init, signal graceful shutdown on
tray-quit and update-installer paths, Stop on app exit. Best-effort:
heartbeat failure is logged but non-fatal — watchdog auto-respawn
simply won't engage in that case."
```

---

### Task 2.3: Watchdog exe scaffold

**Files:**
- Create: `src/watchdog/main.cpp`
- Create: `src/watchdog/CMakeLists.txt`
- Modify: top-level `CMakeLists.txt`

- [ ] **Step 1: Write src/watchdog/main.cpp**

```cpp
// NexusKey Watchdog - Process Supervisor
// SPDX-License-Identifier: GPL-3.0-only
//
// Monitors NexusKey.exe via Local\NexusKeyHeartbeat (30s pulse, 90s
// timeout = 3 missed pulses). On crash detect (heartbeat stale +
// graceful flag clear + process not in process list), respawns
// NexusKey.exe via CreateProcessW.
//
// Launched at-logon by Task Scheduler under user account (NOT SYSTEM)
// to keep AV happy and to be able to launch user-session UI.

#include <Windows.h>
#include <Psapi.h>
#include <TlHelp32.h>
#include <string>

namespace {

constexpr const wchar_t* HEARTBEAT_EVENT_NAME = L"Local\\NexusKeyHeartbeat";
constexpr const wchar_t* GRACEFUL_SHUTDOWN_EVENT_NAME = L"Local\\NexusKeyGracefulShutdown";
constexpr DWORD HEARTBEAT_TIMEOUT_MS = 90'000;  // 3× publisher interval
constexpr DWORD POST_RESPAWN_GRACE_MS = 5'000;  // Wait this long after respawn
constexpr DWORD POST_INIT_GRACE_MS = 30'000;    // Initial grace for NexusKey to start

// Logging helper — append to %LOCALAPPDATA%\NexusKey\watchdog.log.
// Best-effort: silent failure if file unavailable.
void LogLine(const wchar_t* fmt, ...) {
    wchar_t path[MAX_PATH];
    DWORD len = GetEnvironmentVariableW(L"LOCALAPPDATA", path, MAX_PATH);
    if (len == 0 || len >= MAX_PATH) return;
    wcscat_s(path, MAX_PATH, L"\\NexusKey\\watchdog.log");

    HANDLE hFile = CreateFileW(path, FILE_APPEND_DATA, FILE_SHARE_READ,
                               nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hFile == INVALID_HANDLE_VALUE) return;
    SetFilePointer(hFile, 0, nullptr, FILE_END);

    SYSTEMTIME st;
    GetLocalTime(&st);
    wchar_t buf[1024];
    int n = swprintf_s(buf, 1024, L"[%04u-%02u-%02u %02u:%02u:%02u] ",
                       st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);

    va_list args;
    va_start(args, fmt);
    n += vswprintf_s(buf + n, 1024 - n, fmt, args);
    va_end(args);

    if (n < 1023) { buf[n] = L'\n'; buf[n + 1] = L'\0'; n++; }

    DWORD written;
    WriteFile(hFile, buf, n * sizeof(wchar_t), &written, nullptr);
    CloseHandle(hFile);
}

bool IsProcessAlive(const wchar_t* exeName) {
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return false;

    PROCESSENTRY32W pe = { sizeof(pe) };
    bool found = false;
    if (Process32FirstW(snap, &pe)) {
        do {
            if (_wcsicmp(pe.szExeFile, exeName) == 0) { found = true; break; }
        } while (Process32NextW(snap, &pe));
    }
    CloseHandle(snap);
    return found;
}

std::wstring GetNexusKeyExePath() {
    // Watchdog and NexusKey live in the same install dir.
    wchar_t self[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, self, MAX_PATH);
    std::wstring path(self);
    auto pos = path.find_last_of(L"\\/");
    if (pos == std::wstring::npos) return {};
    return path.substr(0, pos) + L"\\NexusKey.exe";
}

bool RespawnNexusKey() {
    std::wstring exePath = GetNexusKeyExePath();
    if (exePath.empty()) {
        LogLine(L"RespawnNexusKey: cannot resolve NexusKey.exe path");
        return false;
    }

    STARTUPINFOW si = { sizeof(si) };
    PROCESS_INFORMATION pi = {};
    std::wstring cmd = L"\"" + exePath + L"\"";
    if (!CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE,
                        0, nullptr, nullptr, &si, &pi)) {
        LogLine(L"RespawnNexusKey: CreateProcess FAILED err=%lu", GetLastError());
        return false;
    }
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    LogLine(L"RespawnNexusKey: spawned PID=%lu", pi.dwProcessId);
    return true;
}

}  // namespace

int APIENTRY wWinMain(HINSTANCE, HINSTANCE, LPWSTR, int) {
    LogLine(L"Watchdog starting (pid=%lu)", GetCurrentProcessId());

    // Initial grace — NexusKey may not be up yet at logon.
    Sleep(POST_INIT_GRACE_MS);

    while (true) {
        // Open events fresh each iteration — handles publisher restart
        // (NexusKey crashed and respawned) by reattaching to new instance.
        HANDLE heartbeat = OpenEventW(SYNCHRONIZE, FALSE, HEARTBEAT_EVENT_NAME);
        HANDLE graceful = OpenEventW(SYNCHRONIZE, FALSE, GRACEFUL_SHUTDOWN_EVENT_NAME);

        if (!heartbeat || !graceful) {
            // Events not yet created → NexusKey not running.
            if (heartbeat) CloseHandle(heartbeat);
            if (graceful) CloseHandle(graceful);

            if (!IsProcessAlive(L"NexusKey.exe")) {
                LogLine(L"Heartbeat events absent + process not running → respawn");
                RespawnNexusKey();
                Sleep(POST_RESPAWN_GRACE_MS);
            } else {
                Sleep(5000);  // Process exists but events not yet up — be patient
            }
            continue;
        }

        DWORD waitResult = WaitForSingleObject(heartbeat, HEARTBEAT_TIMEOUT_MS);
        CloseHandle(heartbeat);

        if (waitResult == WAIT_OBJECT_0) {
            // Pulse received — alive.
            CloseHandle(graceful);
            continue;
        }

        // Heartbeat stale — check graceful flag.
        DWORD gracefulState = WaitForSingleObject(graceful, 0);
        CloseHandle(graceful);

        if (gracefulState == WAIT_OBJECT_0) {
            LogLine(L"Heartbeat stale + graceful flag set → user quit, watchdog exiting");
            return 0;
        }

        // Stale + no graceful — but is process actually dead?
        if (IsProcessAlive(L"NexusKey.exe")) {
            LogLine(L"Heartbeat stale but process alive (UI hung?) — NOT respawning");
            Sleep(POST_RESPAWN_GRACE_MS);
            continue;
        }

        LogLine(L"Heartbeat stale + graceful clear + process dead → CRASH detected");
        RespawnNexusKey();
        Sleep(POST_RESPAWN_GRACE_MS);
    }
}
```

- [ ] **Step 2: Write src/watchdog/CMakeLists.txt**

```cmake
# NexusKey Watchdog — process supervisor for crash auto-respawn.
# Built only on Windows.

if(NOT WIN32)
    return()
endif()

add_executable(NexusKeyWatchdog WIN32
    main.cpp
)

target_compile_definitions(NexusKeyWatchdog PRIVATE
    UNICODE _UNICODE
)

target_link_libraries(NexusKeyWatchdog PRIVATE
    psapi.lib
)

if(MSVC)
    target_link_options(NexusKeyWatchdog PRIVATE /SUBSYSTEM:WINDOWS,6.01 /RELEASE)
endif()

set_target_properties(NexusKeyWatchdog PROPERTIES
    OUTPUT_NAME "NexusKeyWatchdog"
)
```

- [ ] **Step 3: Wire into top-level CMakeLists.txt**

After line 489 (`add_subdirectory(tools/NextKeyTestRunner)`), add:
```cmake
add_subdirectory(src/watchdog)
```

- [ ] **Step 4: Linux skip verification**

```bash
cmake --build build-linux --target NextKeyTests 2>&1 | tail -5
```
Expected: success. `src/watchdog/CMakeLists.txt` `return()`s on non-WIN32, so target is not added on Linux.

- [ ] **Step 5: Commit**

```bash
git add src/watchdog/main.cpp src/watchdog/CMakeLists.txt CMakeLists.txt
git commit -m "feat(watchdog): NexusKeyWatchdog.exe scaffold

Standalone process supervisor: opens Local\\NexusKeyHeartbeat, waits
90s for pulse, respawns NexusKey.exe on crash detect (stale + graceful
flag clear + process not in list).

Three guard conditions before respawn:
1. Heartbeat WAIT_TIMEOUT
2. NexusKeyGracefulShutdown event NOT signaled (rules out user quit)
3. CreateToolhelp32Snapshot shows NexusKey.exe NOT running (rules out
   UI-hung-but-process-alive case)

Logs to %LOCALAPPDATA%\\NexusKey\\watchdog.log."
```

---

### Task 2.4: StartupHelper — Watchdog task helpers

**Files:**
- Modify: `src/app/system/StartupHelper.h`

- [ ] **Step 1: Add watchdog-specific helpers near existing `STARTUP_TASK_NAME`**

After line 34 (`inline constexpr const wchar_t* STARTUP_TASK_NAME`):
```cpp
inline constexpr const wchar_t* WATCHDOG_TASK_NAME = L"\\NexusKey\\Watchdog";
```

- [ ] **Step 2: Add IsWatchdogTaskRegistered helper**

Add after the existing `IsScheduledTaskRegistered()` definition (around line 309):

```cpp
[[nodiscard]] inline bool IsWatchdogTaskRegistered() noexcept {
    std::wstring cmdLine = L"schtasks.exe /query /tn \"" +
                            std::wstring(WATCHDOG_TASK_NAME) + L"\"";
    STARTUPINFOW si = { sizeof(si) };
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    PROCESS_INFORMATION pi = {};
    if (!CreateProcessW(nullptr, cmdLine.data(), nullptr, nullptr, FALSE,
                        CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
        return false;
    }
    WaitForSingleObject(pi.hProcess, 5000);
    DWORD exitCode = 1;
    GetExitCodeProcess(pi.hProcess, &exitCode);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return exitCode == 0;
}
```

- [ ] **Step 3: Add CreateWatchdogScheduledTask helper**

Add after `CreateScheduledTaskElevated()` (around line 183):

```cpp
[[nodiscard]] inline bool CreateWatchdogScheduledTask() noexcept {
    // Build path to NexusKeyWatchdog.exe — same dir as current EXE.
    wchar_t exePath[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, exePath, MAX_PATH);
    std::wstring exeStr(exePath);
    std::wstring dirStr = exeStr.substr(0, exeStr.find_last_of(L"\\/"));
    std::wstring watchdogPath = dirStr + L"\\NexusKeyWatchdog.exe";

    wchar_t username[256] = {};
    DWORD usernameSize = 256;
    GetUserNameW(username, &usernameSize);

    // PowerShell registers the task. Watchdog runs at LIMITED RunLevel
    // (NOT Highest) — keeps AV calm, no UAC needed at logon.
    std::wstring ps1Args = L"-NoProfile -WindowStyle Hidden -Command \"";
    ps1Args += L"$A = New-ScheduledTaskAction -Execute '\"" + watchdogPath + L"\"' -WorkingDirectory '" + dirStr + L"'; ";
    ps1Args += L"$T = New-ScheduledTaskTrigger -AtLogOn; ";
    ps1Args += L"$T.Delay = 'PT10S'; ";  // 10s after logon — let NexusKey come up first
    ps1Args += L"$S = New-ScheduledTaskSettingsSet -AllowStartIfOnBatteries -DontStopIfGoingOnBatteries -ExecutionTimeLimit 0 -RestartCount 3 -RestartInterval (New-TimeSpan -Minutes 1); ";
    ps1Args += L"$P = New-ScheduledTaskPrincipal -UserId '" + EscapePowerShellSingleQuote(username) + L"' -LogonType Interactive -RunLevel Limited; ";
    ps1Args += L"Register-ScheduledTask -TaskName '" + std::wstring(WATCHDOG_TASK_NAME) + L"' -Action $A -Trigger $T -Settings $S -Principal $P -Force\"";

    SHELLEXECUTEINFOW sei = { sizeof(sei) };
    sei.lpVerb = L"runas";  // UAC required to write \NexusKey\ Task Scheduler folder
    sei.lpFile = L"powershell.exe";
    sei.lpParameters = ps1Args.c_str();
    sei.nShow = SW_HIDE;
    sei.fMask = SEE_MASK_NOCLOSEPROCESS;

    if (!ShellExecuteExW(&sei)) return false;
    if (sei.hProcess) {
        WaitForSingleObject(sei.hProcess, 10000);
        DWORD exitCode = 1;
        GetExitCodeProcess(sei.hProcess, &exitCode);
        CloseHandle(sei.hProcess);
        return exitCode == 0;
    }
    return true;
}

inline void RemoveWatchdogScheduledTask() noexcept {
    if (!IsWatchdogTaskRegistered()) return;
    std::wstring args = L"/delete /tn \"" + std::wstring(WATCHDOG_TASK_NAME) + L"\" /f";
    (void)RunSchtasksElevated(args.c_str());
}
```

- [ ] **Step 4: Commit**

```bash
git add src/app/system/StartupHelper.h
git commit -m "feat(watchdog): Task Scheduler helpers for watchdog

Three new functions paralleling existing startup helpers:
- IsWatchdogTaskRegistered (non-elevated query)
- CreateWatchdogScheduledTask (UAC, registers \\NexusKey\\Watchdog
  at-logon with 10s delay, RunLevel Limited, RestartCount=3)
- RemoveWatchdogScheduledTask (UAC delete)

Task path \\NexusKey\\Watchdog (user-root, visible in Task Scheduler
MMC for user debug). RunLevel Limited matches NexusKey itself —
keeps AV from flagging elevated background process."
```

---

### Task 2.5: Wire watchdog task registration into main.cpp

**Files:**
- Modify: `src/app/main.cpp`

- [ ] **Step 1: Add one-shot watchdog task registration on first run**

Find the app-init block where `EnsureStartupRegistration` is called (or the equivalent first-run setup). Add after it:

```cpp
// Register watchdog task at first run (UAC prompt once). Idempotent —
// CreateWatchdogScheduledTask uses Register-ScheduledTask -Force.
if (!IsWatchdogTaskRegistered()) {
    NEXTKEY_LOG(L"Watchdog task not registered — prompting for UAC");
    if (!CreateWatchdogScheduledTask()) {
        NEXTKEY_LOG(L"Watchdog task registration FAILED — auto-respawn unavailable");
        // Non-fatal: app still functions.
    }
}
```

- [ ] **Step 2: Optional — wire uninstall path**

If NexusKey has an uninstall hook (find via `RemoveScheduledTask` callsites), pair with `RemoveWatchdogScheduledTask()`.

- [ ] **Step 3: Commit**

```bash
git add src/app/main.cpp
git commit -m "feat(watchdog): register watchdog task on first run

One-shot UAC prompt at first launch when WATCHDOG_TASK_NAME is not
yet registered. Idempotent for subsequent runs (no UAC). If the user
denies UAC, auto-respawn is unavailable but app still functions
normally."
```

---

### Task 2.6: Phase 2 manual smoke + AV scan gate

**Files:** None (verification only)

- [ ] **Step 1: Build all Windows targets**

From WSL:
```bash
powershell.exe -Command "cd '\\wsl.localhost\Ubuntu-24.04\home\phatmt\code\NexusKey\build'; cmake --build . --target NexusKey --config Release 2>&1" | tail -20
powershell.exe -Command "cd '\\wsl.localhost\Ubuntu-24.04\home\phatmt\code\NexusKey\build'; cmake --build . --target NexusKeyWatchdog --config Release 2>&1" | tail -20
```
Expected: both build success.

- [ ] **Step 2: Heartbeat smoke test**

1. Launch NexusKey
2. Verify heartbeat events exist:
   ```powershell
   # PowerShell — check object existence via Sysinternals winobj or:
   handle.exe -a NexusKeyHeartbeat
   ```
   Expected: NexusKey process owns 2 named events.
3. Kill NexusKey via Task Manager (NOT tray quit — simulate crash):
   ```cmd
   taskkill /F /IM NexusKey.exe
   ```
4. Wait 90-120 seconds.
5. Expected: NexusKey.exe process appears again in Task Manager. `%LOCALAPPDATA%\NexusKey\watchdog.log` shows:
   ```
   Heartbeat stale + graceful clear + process dead → CRASH detected
   RespawnNexusKey: spawned PID=NNNN
   ```

- [ ] **Step 3: Graceful shutdown smoke test**

1. Launch NexusKey
2. Quit via tray menu (not taskkill)
3. Watchdog log should show:
   ```
   Heartbeat stale + graceful flag set → user quit, watchdog exiting
   ```
4. NexusKey should NOT be respawned. Verify `Get-Process NexusKey` returns nothing.

- [ ] **Step 4: Sleep/wake cycle test**

1. NexusKey + watchdog running.
2. `rundll32 powrprof.dll,SetSuspendState 0,1,0`
3. Wake machine.
4. Both processes still in Task Manager. NexusKey heartbeat resumes (verify by typing Vietnamese in any app).

- [ ] **Step 5: AV scan — Windows Defender**

```powershell
# Scan the build output dir
Start-MpScan -ScanType CustomScan -ScanPath "C:\path\to\build\Release"
```
Expected: zero detections. If flagged, capture detection name and revisit signing.

- [ ] **Step 6: AV scan — Bitdefender**

Install Bitdefender free trial, full-scan the install dir. Expected: zero detections. If flagged, file false-positive report (template in `docs/superpowers/plans/2026-04-12-av-false-positive-reduction.md` history).

- [ ] **Step 7: Decision gate**

If all 4 smoke tests + 2 AV scans pass → Phase 2 complete, v3.0.0 watchdog work ready for release bundling.

If AV flags → STOP shipping. Investigate signing certificate state, code-signing pipeline, and binary metadata before retry.

If respawn does NOT happen → check process discovery via `IsProcessAlive`, verify watchdog task is actually running (`Get-ScheduledTask -TaskName Watchdog -TaskPath \NexusKey\`), and inspect watchdog.log for the decision sequence.

---

## Out of Scope (deferred to separate work)

- Mode 1 long-uptime drift — needs telemetry; anh quyết định không invest 2026-05-08
- Quick Convert HOA→Title Case bug — separate brainstorm
- SPSC ring buffer + side-channel context — premise stale, revisit on TSF Phase 2
- H8 Sprint 1 deferred items (`WaitOnAddress`, ETW)

## Self-Review Notes

- **Spec coverage:** Phase 1 §3 (HookSelfHealer extraction + 4 fixes) → Tasks 1.1-1.4. Phase 2 §4 (NexusKeyWatchdog, heartbeat 30s/90s, Task Scheduler at-logon, AV scan) → Tasks 2.1-2.6. Acceptance criteria from spec §6 mapped to manual gates in Tasks 1.5 and 2.6.
- **Type consistency:** `IHookSelfHealer` / `RawInputSelfHealer` / `ReinstallHookFn` used consistently across header, impl, test, and HookEngine wiring. `HEARTBEAT_EVENT_NAME` / `GRACEFUL_SHUTDOWN_EVENT_NAME` / `WATCHDOG_TASK_NAME` constants share consistent naming with existing repo style.
- **Placeholder scan:** No "TBD"/"TODO" steps — every task has complete code blocks. Manual test steps in 1.5 and 2.6 specify exact commands/expected output.
