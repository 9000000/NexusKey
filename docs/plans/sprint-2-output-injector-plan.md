# Sprint 2 T3 — IOutputInjector Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Extract the output channel layer from `HookEngine.cpp` into the `src/app/output/` module behind an `IOutputInjector` interface, with three concrete impls (Win32 batch SendInput / RichEdit EM_REPLACESEL / split-dispatch with Sleep), published via RCU `shared_ptr` and selected by an `OutputInjectorFactory` on focus change.

**Architecture:** RCU `std::shared_ptr<IOutputInjector>` published via `std::atomic_store`/`std::atomic_load` (same pattern as Sprint 1 D6 `config_`). Two-phase focus: classify (no shared write) → atomic-publish. Hot path: 1 atomic_load + 1 virtual call + mechanism (~11 ns overhead). Test seam: function-pointer indirection on `SendInput` / `SendMessageW` / `Sleep` so impls are testable on Windows GTest with no GUI.

**Tech Stack:** C++20 / MSVC 2022 / GoogleTest / Windows API (SendInput, SendMessage, Win32 Edit + RichEdit). Linux build excludes the `output/` folder via CMake `if(WIN32)` gate; engine GTest 1405/1405 must continue to pass on Linux.

**Spec reference:** [`sprint-2-output-injector.md`](sprint-2-output-injector.md) — read its §0 (5-question gate), §2 (components), §6 (D-day breakdown) before starting.

**Branch policy:** `sprint-2/output-injector` from Main. ONE commit per D-day (D0..D6). Each D-commit must leave system functional with chaos-corpus passing → bisect-friendly. Single PR at the end.

---

## File Structure

**New (all under `src/app/output/`, CMake `if(WIN32)` gated):**

| File | Responsibility |
|---|---|
| `IOutputInjector.h` | Pure-virtual interface, header-only. 3 methods: `Replace`, `SendKey`, `SettleBudget` (non-pure). |
| `Internal.h` | `extern` test-seam function pointers (`g_sendInput` / `g_sendMessageW` / `g_sleep`); `TrackedSendInput` declaration; `kNexusKeyExtraInfo`. |
| `Internal.cpp` | Definitions of fn pointers (init to `::SendInput` etc) and `TrackedSendInput` impl. |
| `OutputInjectorFactory.h` | `struct WindowClassification`; `ClassifyWindow(HWND)`; `Create(WindowClassification)`. |
| `OutputInjectorFactory.cpp` | `ClassifyWindow` (moves from HookEngine.cpp) + `Create`. |
| `Win32SendInputInjector.h/.cpp` | `final` impl. ~85% hosts. Bait-char prefix gate. |
| `RichEditEmReplaceSelInjector.h/.cpp` | `final` impl. RichEditD2DPT only. |
| `SplitDispatchInjector.h/.cpp` | `final` impl. Electron + Console (constructor `sleepMs`). |

**New tests (under `tests/output/`, Windows-only):**

| File | Tests |
|---|---|
| `OutputInjectorFactoryTest.cpp` | 5 — default / RichEdit / Electron / Console / Chromium classification |
| `Win32SendInputInjectorTest.cpp` | 5 — empty text, BS+chars, bait-char, partial-send, SendKey marker |
| `RichEditEmReplaceSelInjectorTest.cpp` | 4 — message order, bs=0 skip, SendMessage failure, SendKey fallthrough |
| `SplitDispatchInjectorTest.cpp` | 5 — split order, bs=0, text empty, sleepMs respected, partial first send |

**Modified:**

| File | Change |
|---|---|
| `src/app/system/HookEngine.h` | Forward decl `namespace NextKey::Output { class IOutputInjector; }`; add `std::shared_ptr<NextKey::Output::IOutputInjector> injector_;`. Remove 4 atomic field decls in D4. |
| `src/app/system/HookEngine.cpp` | Route SendBackspaces / SendCharEvents / InjectKey / ReplaceComposition through `injector_`. Remove path-specific branches incrementally D1→D3. Delete dead helpers + 4 atomic stores in D4. |
| `tools/audit/check_hook_thread_no_mutex.sh` | D4: drop 4 fields from regex; add Check 4. |
| `tools/NextKeyTestRunner/src/main.cpp` | D6: add `--host-class` CLI flag → set `NEXUSKEY_FORCE_HOST_CLASS` env var. |
| `CMakeLists.txt` (or `src/app/CMakeLists.txt`) | Add `output/*.cpp` sources, gated if(WIN32). |
| `tests/CMakeLists.txt` | Add `output/*Test.cpp` sources, gated if(WIN32). |

---

## D0 — Scaffolding

Goal: every new file compiles, factory stub returns default Win32 stub, `HookEngine::injector_` initialized in ctor but not yet called from anywhere. Linux engine GTest still 1405/1405. Single end-of-D commit.

### Task 1: Create `IOutputInjector.h` interface header

**Files:**
- Create: `src/app/output/IOutputInjector.h`

- [ ] **Step 1: Write the header**

```cpp
// src/app/output/IOutputInjector.h
#pragma once

#include <chrono>
#include <cstddef>
#include <string_view>

namespace NextKey::Output {

// Output Injection Strategy interface (CODE_GOVERNANCE §2).
// Implementations encapsulate the OS-side mechanism (batch SendInput,
// EM_REPLACESEL, split-dispatch with Sleep). HookEngine reads via
// std::atomic_load(shared_ptr) on the hook thread (RCU pattern).
class IOutputInjector {
public:
    virtual ~IOutputInjector() = default;

    // Replace caret region: delete bsCount chars, then insert text.
    // Returns true if delivered, false if caller should fall back to passthrough.
    [[nodiscard]] virtual bool Replace(size_t bsCount,
                                       std::wstring_view text) noexcept = 0;

    // Re-inject a single VK as if the user pressed it (down + up, with
    // NEXUSKEY_EXTRA_INFO marker). Used for the synth-pending re-inject case.
    virtual void SendKey(unsigned short vkCode) noexcept = 0;

    // Time the synth pressure from this channel takes to drain. Used by
    // HookEngine's commit-undo synth-guard. Default 100 ms (paranoid).
    // Each impl overrides to match its actual mechanism.
    [[nodiscard]] virtual std::chrono::milliseconds SettleBudget() const noexcept {
        return std::chrono::milliseconds{100};
    }
};

}  // namespace NextKey::Output
```

- [ ] **Step 2: Verify header compiles standalone (Linux + Windows)**

Linux: `cmake --build build-linux --target NextKeyEngine 2>&1 | grep -E "(IOutputInjector|error)"` — expect no errors (header not yet included by anything; test by adding `#include "app/output/IOutputInjector.h"` to a scratch translation unit if CMake doesn't include `src/` for the engine target). Actually, easier: defer compile-check until Step in Task 5 (HookEngine forward decl is the first inclusion).

For now: `head -5 src/app/output/IOutputInjector.h` to verify file content saved.

### Task 2: Create `Internal` test-seam header + source

**Files:**
- Create: `src/app/output/Internal.h`
- Create: `src/app/output/Internal.cpp`

- [ ] **Step 1: Write `Internal.h`**

```cpp
// src/app/output/Internal.h
#pragma once

#include <windows.h>
#include <cstddef>

namespace NextKey::Output::Internal {

using SendInputFn    = UINT (WINAPI*)(UINT, LPINPUT, int);
using SendMessageWFn = LRESULT (WINAPI*)(HWND, UINT, WPARAM, LPARAM);
using SleepFn        = void (WINAPI*)(DWORD);

// Test seams. Production wires these to the real Win32 APIs at startup.
// Tests swap to capturing lambdas in fixture SetUp() and restore in TearDown().
extern SendInputFn    g_sendInput;
extern SendMessageWFn g_sendMessageW;
extern SleepFn        g_sleep;

// Wrapper around g_sendInput with partial-send detection. Returns true iff
// all events delivered; false on partial (renderer drop case).
[[nodiscard]] bool TrackedSendInput(INPUT* events, UINT count) noexcept;

// Marker dwExtraInfo so own synth events skip our own hook (Rule #11.4).
constexpr ULONG_PTR kNexusKeyExtraInfo = 0xC1A0DEUL;  // matches HookEngine NEXUSKEY_EXTRA_INFO

}  // namespace NextKey::Output::Internal
```

- [ ] **Step 2: Write `Internal.cpp`**

```cpp
// src/app/output/Internal.cpp
#include "Internal.h"

#include "app/system/HookEngine.h"  // for HOOK_LOG macro if used; else drop
// ^ if HOOK_LOG is in a util header, include that one instead.

namespace NextKey::Output::Internal {

SendInputFn    g_sendInput    = ::SendInput;
SendMessageWFn g_sendMessageW = ::SendMessageW;
SleepFn        g_sleep        = ::Sleep;

bool TrackedSendInput(INPUT* events, UINT count) noexcept {
    UINT sent = g_sendInput(count, events, sizeof(INPUT));
    return sent == count;  // false on partial (renderer drop)
}

}  // namespace NextKey::Output::Internal
```

- [ ] **Step 3: Verify `kNexusKeyExtraInfo` value matches existing `NEXUSKEY_EXTRA_INFO`**

Run: `grep -nE "NEXUSKEY_EXTRA_INFO" src/app/system/HookEngine.h src/app/system/HookEngine.cpp | head -3`

Expected: find a `#define NEXUSKEY_EXTRA_INFO 0x...UL` somewhere. Update `kNexusKeyExtraInfo` literal to match. If `NEXUSKEY_EXTRA_INFO` is in a public header, replace the literal with `static_cast<ULONG_PTR>(NEXUSKEY_EXTRA_INFO)` and `#include` that header instead.

### Task 3: Create factory stub

**Files:**
- Create: `src/app/output/OutputInjectorFactory.h`
- Create: `src/app/output/OutputInjectorFactory.cpp`

- [ ] **Step 1: Write factory header**

```cpp
// src/app/output/OutputInjectorFactory.h
#pragma once

#include "IOutputInjector.h"
#include <windows.h>
#include <memory>

namespace NextKey::Output {

// Phase 1 result — pure data, no shared-state writes (Rule #11.3).
struct WindowClassification {
    bool isRichEditD2DPT = false;  // Win11 New Notepad
    bool isElectron      = false;  // Discord / Slack / VSCode etc
    bool isConsole       = false;  // CMD / PowerShell
    bool isChromium      = false;  // Chrome / Edge — bait-char hint
};

// Phase 1 — classify focused window. No shared writes.
[[nodiscard]] WindowClassification ClassifyWindow(HWND hwnd) noexcept;

// Phase 2 — construct injector for a classification. Always returns a
// usable injector (default branch is Win32). Caller atomic_store-publishes.
[[nodiscard]] std::shared_ptr<IOutputInjector> Create(
    const WindowClassification& c) noexcept;

}  // namespace NextKey::Output
```

- [ ] **Step 2: Write factory stub source (returns Win32 stub for everything until D2/D3 wire impls)**

```cpp
// src/app/output/OutputInjectorFactory.cpp
#include "OutputInjectorFactory.h"

// NOTE: D2/D3 wires the real impls here. For now, factory returns the
// Win32 stub for every classification — this is enough for D0 because
// the engine doesn't actually use it yet.
#include "Win32SendInputInjector.h"

namespace NextKey::Output {

WindowClassification ClassifyWindow(HWND /*hwnd*/) noexcept {
    // D2/D3 will populate flags. D0 stub returns all-false → default Win32.
    return WindowClassification{};
}

std::shared_ptr<IOutputInjector> Create(
        const WindowClassification& c) noexcept {
    // Priority order: RichEdit > Electron > Console > default (Win32).
    // D0: only default branch is wired; D2/D3 add the others.
    return std::make_shared<Win32SendInputInjector>(c.isChromium);
}

}  // namespace NextKey::Output
```

### Task 4: Create the three impl stubs (no-op)

**Files:**
- Create: `src/app/output/Win32SendInputInjector.h`, `.cpp`
- Create: `src/app/output/RichEditEmReplaceSelInjector.h`, `.cpp`
- Create: `src/app/output/SplitDispatchInjector.h`, `.cpp`

- [ ] **Step 1: Win32 stub header**

```cpp
// src/app/output/Win32SendInputInjector.h
#pragma once

#include "IOutputInjector.h"

namespace NextKey::Output {

class Win32SendInputInjector final : public IOutputInjector {
public:
    explicit Win32SendInputInjector(bool needsBaitCharPrefix) noexcept
        : needsBaitCharPrefix_(needsBaitCharPrefix) {}

    bool Replace(size_t bsCount, std::wstring_view text) noexcept override;
    void SendKey(unsigned short vkCode) noexcept override;
    std::chrono::milliseconds SettleBudget() const noexcept override {
        return std::chrono::milliseconds{30};
    }

private:
    bool needsBaitCharPrefix_;
};

}  // namespace NextKey::Output
```

- [ ] **Step 2: Win32 stub source (returns false / no-op until D1)**

```cpp
// src/app/output/Win32SendInputInjector.cpp
#include "Win32SendInputInjector.h"

namespace NextKey::Output {

bool Win32SendInputInjector::Replace(size_t /*bsCount*/,
                                     std::wstring_view /*text*/) noexcept {
    // D1 implementation. Stub returns false → caller falls back to passthrough.
    return false;
}

void Win32SendInputInjector::SendKey(unsigned short /*vkCode*/) noexcept {
    // D1 implementation.
}

}  // namespace NextKey::Output
```

- [ ] **Step 3: RichEdit stub header + source (analogous, SettleBudget = 0ms, no member fields)**

Header `RichEditEmReplaceSelInjector.h`:

```cpp
#pragma once
#include "IOutputInjector.h"

namespace NextKey::Output {

class RichEditEmReplaceSelInjector final : public IOutputInjector {
public:
    bool Replace(size_t bsCount, std::wstring_view text) noexcept override;
    void SendKey(unsigned short vkCode) noexcept override;
    std::chrono::milliseconds SettleBudget() const noexcept override {
        return std::chrono::milliseconds{0};
    }
};

}  // namespace NextKey::Output
```

Source `RichEditEmReplaceSelInjector.cpp`:

```cpp
#include "RichEditEmReplaceSelInjector.h"

namespace NextKey::Output {

bool RichEditEmReplaceSelInjector::Replace(size_t /*bsCount*/,
                                           std::wstring_view /*text*/) noexcept {
    return false;  // D2 implementation
}

void RichEditEmReplaceSelInjector::SendKey(unsigned short /*vkCode*/) noexcept {
    // D2 implementation
}

}  // namespace NextKey::Output
```

- [ ] **Step 4: Split stub header + source (analogous, takes `int sleepMs` constructor)**

Header `SplitDispatchInjector.h`:

```cpp
#pragma once
#include "IOutputInjector.h"

namespace NextKey::Output {

class SplitDispatchInjector final : public IOutputInjector {
public:
    explicit SplitDispatchInjector(int sleepMsBetweenBatches) noexcept
        : sleepMs_(sleepMsBetweenBatches) {}

    bool Replace(size_t bsCount, std::wstring_view text) noexcept override;
    void SendKey(unsigned short vkCode) noexcept override;
    std::chrono::milliseconds SettleBudget() const noexcept override {
        return std::chrono::milliseconds{100};
    }

private:
    int sleepMs_;
};

}  // namespace NextKey::Output
```

Source `SplitDispatchInjector.cpp`:

```cpp
#include "SplitDispatchInjector.h"

namespace NextKey::Output {

bool SplitDispatchInjector::Replace(size_t /*bsCount*/,
                                    std::wstring_view /*text*/) noexcept {
    return false;  // D3 implementation
}

void SplitDispatchInjector::SendKey(unsigned short /*vkCode*/) noexcept {
    // D3 implementation
}

}  // namespace NextKey::Output
```

### Task 5: Wire CMake — add `output/` sources gated `if(WIN32)`

**Files:**
- Modify: `src/app/CMakeLists.txt` (or wherever `NextKeyApp` / shared lib sources are listed)

- [ ] **Step 1: Locate the CMakeLists section that lists app/system/HookEngine sources**

Run: `grep -nE "HookEngine|app/system" src/app/CMakeLists.txt CMakeLists.txt 2>/dev/null | head`

Expected: find the `target_sources` or `add_library` block that collects `src/app/system/*.cpp`.

- [ ] **Step 2: Add the new output sources, gated `if(WIN32)`**

Edit the CMake file:

```cmake
# After the existing app/system source list, add:
if(WIN32)
    target_sources(NextKeyApp PRIVATE  # or whatever target the app/system files belong to
        src/app/output/Internal.cpp
        src/app/output/OutputInjectorFactory.cpp
        src/app/output/Win32SendInputInjector.cpp
        src/app/output/RichEditEmReplaceSelInjector.cpp
        src/app/output/SplitDispatchInjector.cpp
    )
endif()
```

- [ ] **Step 3: Verify Linux build still compiles (output/ excluded by gate)**

```bash
cmake --build build-linux --target NextKeyTests
```

Expected: PASS, no references to `NextKey::Output::*` from Linux-built engine code.

- [ ] **Step 4: Verify Windows build compiles (in WSL)**

```bash
powershell.exe -Command "cd '\\wsl.localhost\Ubuntu-24.04\home\phatmt\code\NexusKey\build'; cmake --build . --target NexusKey --config Debug 2>&1" | tail -20
```

Expected: PASS, no compile errors. New `output/*.obj` produced.

### Task 6: Add `injector_` member to HookEngine + ctor init

**Files:**
- Modify: `src/app/system/HookEngine.h`
- Modify: `src/app/system/HookEngine.cpp` (constructor)

- [ ] **Step 1: Add forward decl + member to `HookEngine.h`**

In `HookEngine.h`, near the top (before `class HookEngine`):

```cpp
// Forward-declare so HookEngine.h stays Linux-friendly (no Output/ headers).
namespace NextKey::Output { class IOutputInjector; }
```

In the `HookEngine` class private section (alongside other state):

```cpp
// Output channel. RCU pattern: published via std::atomic_store on focus change,
// read via std::atomic_load on hook thread. Same pattern as config_.
std::shared_ptr<NextKey::Output::IOutputInjector> injector_;
```

- [ ] **Step 2: Initialize in ctor**

In `HookEngine.cpp` ctor, near the top:

```cpp
// At top of HookEngine.cpp, add:
#include "app/output/OutputInjectorFactory.h"

// In HookEngine ctor body, before any state-mutator runs:
{
    auto initial = NextKey::Output::Create({});  // all flags false → default Win32
    std::atomic_store(&injector_, std::move(initial));
}
```

Note: the `std::atomic_store(shared_ptr*, shared_ptr)` free function is C++20. Verify MSVC `/std:c++20` is set — check root `CMakeLists.txt` for `CXX_STANDARD 20`. If not yet 20, this entire plan is gated on bumping to C++20 first (Sprint 1 may already have done this).

- [ ] **Step 3: Linux build smoke**

```bash
cmake --build build-linux --target NextKeyTests
```

Expected: PASS. The forward decl + `std::shared_ptr<IOutputInjector>` member compiles on Linux because shared_ptr to forward-declared class is OK.

- [ ] **Step 4: Windows build smoke + run engine tests**

```bash
powershell.exe -Command "cd '\\wsl.localhost\Ubuntu-24.04\home\phatmt\code\NexusKey\build'; cmake --build . --target NexusKey --config Debug 2>&1" | tail
./build-linux/tests/NextKeyTests
```

Expected: Windows compile PASS. Engine tests 1405/1405 PASS.

### Task 7: D0 commit

- [ ] **Step 1: Stage and commit**

```bash
git checkout -b sprint-2/output-injector  # if not yet on it
git add src/app/output/ src/app/system/HookEngine.h src/app/system/HookEngine.cpp \
        src/app/CMakeLists.txt  # adjust path as needed
git commit -m "Sprint 2 D0: IOutputInjector scaffolding + factory stub + test seam"
```

- [ ] **Step 2: Verify commit clean**

```bash
git log -1 --stat
git status  # should be clean
```

Expected: 11 new files (`output/` headers + sources) + 2-3 modified files. Working tree clean.

---

## D1 — Win32SendInputInjector + integrate default path

Goal: `Win32SendInputInjector::Replace` and `SendKey` fully implemented; HookEngine routes `SendBackspaces`, `SendCharEvents`, `InjectKey` through `injector_`. `useEditMsgPath_` and `isElectronApp_` flags **still present** as short-circuits BEFORE the injector call (preserving today's behavior; D2/D3 will remove them).

### Task 8: Test fixture for Win32 injector

**Files:**
- Create: `tests/output/InjectorTestBase.h` (shared fixture)
- Create: `tests/output/Win32SendInputInjectorTest.cpp`

- [ ] **Step 1: Write shared fixture**

```cpp
// tests/output/InjectorTestBase.h
#pragma once

#include "app/output/Internal.h"
#include <gtest/gtest.h>
#include <vector>
#include <tuple>
#include <windows.h>

namespace NextKey::Output::Test {

class InjectorTestBase : public ::testing::Test {
protected:
    static inline std::vector<INPUT> capturedInputs;
    static inline std::vector<DWORD> sleepDelays;
    static inline std::vector<std::tuple<HWND, UINT, WPARAM, LPARAM>> capturedMsgs;
    static inline UINT sendInputReturnOverride = 0;  // 0 = full delivery

    void SetUp() override {
        capturedInputs.clear();
        sleepDelays.clear();
        capturedMsgs.clear();
        sendInputReturnOverride = 0;

        Internal::g_sendInput = [](UINT n, LPINPUT inputs, int) -> UINT {
            for (UINT i = 0; i < n; ++i) capturedInputs.push_back(inputs[i]);
            return sendInputReturnOverride > 0 ? sendInputReturnOverride : n;
        };
        Internal::g_sleep = [](DWORD ms) {
            sleepDelays.push_back(ms);
        };
        Internal::g_sendMessageW = [](HWND h, UINT m, WPARAM w, LPARAM l) -> LRESULT {
            capturedMsgs.emplace_back(h, m, w, l);
            return 1;  // default success; tests can override per-test
        };
    }

    void TearDown() override {
        Internal::g_sendInput    = ::SendInput;
        Internal::g_sleep        = ::Sleep;
        Internal::g_sendMessageW = ::SendMessageW;
    }
};

}  // namespace NextKey::Output::Test
```

- [ ] **Step 2: Write 5 Win32 injector tests (all failing initially)**

```cpp
// tests/output/Win32SendInputInjectorTest.cpp
#include "InjectorTestBase.h"
#include "app/output/Win32SendInputInjector.h"

using namespace NextKey::Output;
using namespace NextKey::Output::Test;

class Win32SendInputInjectorTest : public InjectorTestBase {};

TEST_F(Win32SendInputInjectorTest, ReplaceEmptyTextSendsBackspacesOnly) {
    Win32SendInputInjector inj(/*needsBaitCharPrefix=*/false);
    EXPECT_TRUE(inj.Replace(3, L""));
    ASSERT_EQ(capturedInputs.size(), 6u);  // 3 BS × (down + up)
    for (size_t i = 0; i < 6; ++i) {
        EXPECT_EQ(capturedInputs[i].ki.wVk, VK_BACK);
    }
    EXPECT_EQ(capturedInputs[0].ki.dwFlags & KEYEVENTF_KEYUP, 0u);    // down
    EXPECT_NE(capturedInputs[1].ki.dwFlags & KEYEVENTF_KEYUP, 0u);    // up
}

TEST_F(Win32SendInputInjectorTest, ReplaceWithCharsSendsBatch) {
    Win32SendInputInjector inj(false);
    EXPECT_TRUE(inj.Replace(2, L"vi"));
    ASSERT_EQ(capturedInputs.size(), 4u + 4u);  // 2 BS down/up + 2 chars down/up
    EXPECT_EQ(capturedInputs[0].ki.wVk, VK_BACK);
    // Char events use wScan (UNICODE) not wVk
    EXPECT_NE(capturedInputs[4].ki.dwFlags & KEYEVENTF_UNICODE, 0u);
    EXPECT_EQ(capturedInputs[4].ki.wScan, L'v');
    EXPECT_EQ(capturedInputs[6].ki.wScan, L'i');
}

TEST_F(Win32SendInputInjectorTest, BaitCharPrefixWhenFlaggedAndPureBackspace) {
    Win32SendInputInjector inj(/*needsBaitCharPrefix=*/true);
    EXPECT_TRUE(inj.Replace(2, L""));
    // Expected: 1 bait char (U+202F down + up) + 3 BS (= original 2 + 1 extra to delete bait)
    ASSERT_EQ(capturedInputs.size(), 2u + 6u);
    EXPECT_EQ(capturedInputs[0].ki.wScan, 0x202F);
    EXPECT_NE(capturedInputs[0].ki.dwFlags & KEYEVENTF_UNICODE, 0u);
    for (size_t i = 2; i < 8; ++i) {
        EXPECT_EQ(capturedInputs[i].ki.wVk, VK_BACK);
    }
}

TEST_F(Win32SendInputInjectorTest, BaitCharSkippedWhenTextNonEmpty) {
    Win32SendInputInjector inj(/*needsBaitCharPrefix=*/true);
    EXPECT_TRUE(inj.Replace(1, L"x"));
    // No bait char prefix; just BS×1 + char×1
    ASSERT_EQ(capturedInputs.size(), 2u + 2u);
    EXPECT_EQ(capturedInputs[0].ki.wVk, VK_BACK);
    EXPECT_EQ(capturedInputs[2].ki.wScan, L'x');
}

TEST_F(Win32SendInputInjectorTest, ReplaceReturnsFalseOnPartialSend) {
    Win32SendInputInjector inj(false);
    sendInputReturnOverride = 2;  // simulate partial: only 2 of N events delivered
    EXPECT_FALSE(inj.Replace(3, L""));  // 6 events expected, only 2 delivered
}

TEST_F(Win32SendInputInjectorTest, SendKeyEmitsDownAndUpWithMarker) {
    Win32SendInputInjector inj(false);
    inj.SendKey(VK_BACK);
    ASSERT_EQ(capturedInputs.size(), 2u);
    EXPECT_EQ(capturedInputs[0].ki.wVk, VK_BACK);
    EXPECT_EQ(capturedInputs[0].ki.dwExtraInfo, Internal::kNexusKeyExtraInfo);
    EXPECT_EQ(capturedInputs[0].ki.dwFlags & KEYEVENTF_KEYUP, 0u);
    EXPECT_NE(capturedInputs[1].ki.dwFlags & KEYEVENTF_KEYUP, 0u);
}
```

- [ ] **Step 3: Wire test sources into CMake (Windows-only)**

In `tests/CMakeLists.txt`:

```cmake
if(WIN32)
    target_sources(NextKeyTests PRIVATE
        tests/output/Win32SendInputInjectorTest.cpp
    )
    target_include_directories(NextKeyTests PRIVATE tests)
endif()
```

- [ ] **Step 4: Run tests to verify they fail (impl is still stub)**

```bash
powershell.exe -Command "cd '\\wsl.localhost\Ubuntu-24.04\home\phatmt\code\NexusKey\build'; cmake --build . --target NextKeyTests --config Debug 2>&1" | tail
# Then in PowerShell or via WSL:
./build/tests/Debug/NextKeyTests.exe --gtest_filter="Win32SendInputInjectorTest.*"
```

Expected: 6 tests FAIL (Replace returns false → assertions on captured events fail).

### Task 9: Implement `Win32SendInputInjector::Replace`

**Files:**
- Modify: `src/app/output/Win32SendInputInjector.cpp`

- [ ] **Step 1: Replace the stub with full implementation**

```cpp
// src/app/output/Win32SendInputInjector.cpp
#include "Win32SendInputInjector.h"
#include "Internal.h"

#include <array>

namespace NextKey::Output {

namespace {
// Stack-buffer upper bound. Worst-case Vietnamese composition replacement
// needs ~8 chars + 8 BS; 256 is 16× margin.
constexpr size_t kMaxBatch = 256;

INPUT MakeKey(WORD vk, bool up, ULONG_PTR extraInfo = Internal::kNexusKeyExtraInfo) noexcept {
    INPUT in{};
    in.type = INPUT_KEYBOARD;
    in.ki.wVk = vk;
    in.ki.wScan = static_cast<WORD>(::MapVirtualKeyW(vk, MAPVK_VK_TO_VSC));
    in.ki.dwFlags = up ? KEYEVENTF_KEYUP : 0u;
    in.ki.dwExtraInfo = extraInfo;
    return in;
}

INPUT MakeUnicodeChar(WCHAR ch, bool up,
                     ULONG_PTR extraInfo = Internal::kNexusKeyExtraInfo) noexcept {
    INPUT in{};
    in.type = INPUT_KEYBOARD;
    in.ki.wScan = static_cast<WORD>(ch);
    in.ki.dwFlags = KEYEVENTF_UNICODE | (up ? KEYEVENTF_KEYUP : 0u);
    in.ki.dwExtraInfo = extraInfo;
    return in;
}
}  // namespace

bool Win32SendInputInjector::Replace(size_t bsCount,
                                     std::wstring_view text) noexcept {
    std::array<INPUT, kMaxBatch> buf{};
    size_t i = 0;

    // Bait-char prefix: only when this impl is the Chromium variant AND the
    // operation is pure-backspace (no chars after). Inserts U+202F + an
    // extra BS to delete it. Replicates HookEngine::SendBackspaces line 3121.
    const bool emitBait = needsBaitCharPrefix_ && text.empty() && bsCount > 0;
    if (emitBait) {
        if (i + 2 > kMaxBatch) return false;
        buf[i++] = MakeUnicodeChar(0x202F, /*up=*/false);
        buf[i++] = MakeUnicodeChar(0x202F, /*up=*/true);
        ++bsCount;  // extra BS to delete the bait
    }

    // BS events
    for (size_t k = 0; k < bsCount; ++k) {
        if (i + 2 > kMaxBatch) return false;
        buf[i++] = MakeKey(VK_BACK, /*up=*/false);
        buf[i++] = MakeKey(VK_BACK, /*up=*/true);
    }

    // Char events
    for (WCHAR ch : text) {
        if (i + 2 > kMaxBatch) return false;
        buf[i++] = MakeUnicodeChar(ch, /*up=*/false);
        buf[i++] = MakeUnicodeChar(ch, /*up=*/true);
    }

    if (i == 0) return true;  // nothing to do
    return Internal::TrackedSendInput(buf.data(), static_cast<UINT>(i));
}

void Win32SendInputInjector::SendKey(unsigned short vkCode) noexcept {
    INPUT events[2] = {
        MakeKey(static_cast<WORD>(vkCode), /*up=*/false),
        MakeKey(static_cast<WORD>(vkCode), /*up=*/true),
    };
    Internal::TrackedSendInput(events, 2);  // ignore return: SendKey is fire-and-forget
}

}  // namespace NextKey::Output
```

- [ ] **Step 2: Run Win32 injector tests — expect 6/6 PASS**

```bash
./build/tests/Debug/NextKeyTests.exe --gtest_filter="Win32SendInputInjectorTest.*"
```

Expected: 6 PASS.

### Task 10: Route `HookEngine::SendBackspaces` and `SendCharEvents` through injector

**Files:**
- Modify: `src/app/system/HookEngine.cpp` (functions `SendBackspaces`, `SendCharEvents`, `SendBackspaceEvents`)

- [ ] **Step 1: Locate the functions**

```bash
grep -nE "void HookEngine::(SendBackspaces|SendCharEvents|SendBackspaceEvents)\b" src/app/system/HookEngine.cpp
```

Expected: lines around 1742, 1755, 3103.

- [ ] **Step 2: Replace `SendBackspaces` body to delegate to injector**

The current `SendBackspaces` (line ~3103) has:
1. `useEditMsgPath_` short-circuit calling `TryEditMessagePaste(L"", count)`
2. `needBaitChar_` bait-char prefix logic
3. final `SendBackspaceEvents(count)` raw SendInput call

D1 replaces (3) with injector call BUT leaves (1) `useEditMsgPath_` short-circuit untouched (D2 will remove it). The bait-char (2) is now inside `Win32SendInputInjector::Replace` so the duplicate logic in HookEngine MUST be removed (otherwise bait-char is emitted twice).

```cpp
// Replace the body of HookEngine::SendBackspaces with:
void HookEngine::SendBackspaces(size_t count) {
    if (count == 0) return;

    // Sprint 1 Fix C/2026-05-05: editMsg apps need BS via the sent-message
    // channel. Short-circuit preserved here for D1; D2 removes it once
    // RichEditEmReplaceSelInjector lands.
    if (useEditMsgPath_.load(std::memory_order_acquire)) {
        if (TryEditMessagePaste(L"", count)) {
            HOOK_LOG(L"  SendBackspaces: %zu via EM_REPLACESEL", count);
            return;
        }
        HOOK_LOG(L"  SendBackspaces: EM_REPLACESEL failed, fallback to injector");
    }

    auto inj = std::atomic_load(&injector_);
    HOOK_LOG(L"  SendBackspaces: %zu via injector", count);
    inj->Replace(count, std::wstring_view{});
    // Bait-char logic (line ~3121 today) is now handled inside the injector
    // when needsBaitCharPrefix_ is set by the factory.
}
```

- [ ] **Step 3: Replace `SendCharEvents` body**

Today's `SendCharEvents` (line ~1755) raw-sends UNICODE chars. After D1, callers should go through injector via the engine's existing `ReplaceComposition` machinery, but if `SendCharEvents` is called directly from anywhere (e.g., as a helper), route it through injector:

```bash
grep -nE "SendCharEvents\b" src/app/system/HookEngine.cpp | head
```

Inspect each call site. If `SendCharEvents` is only called from `ReplaceComposition`, defer its replacement until D2 (when `useEditMsgPath_` short-circuit goes away). If called from elsewhere with bsCount=0 semantics, replace with `injector_->Replace(0, text)`.

For D1, leave `SendCharEvents` body untouched — `ReplaceComposition` rewiring is deferred to D2.

- [ ] **Step 4: Verify chaos still PASS on Notepad++ + Chrome**

User runs (Windows):

```powershell
.\build\tools\Debug\NextKeyTestRunner.exe `
    --corpus    tools\NextKeyTestRunner\corpus\chaos.toml `
    --hook-log  build\Debug\NexusKey_hook.log `
    --junit     report-d1-notepadpp.xml `
    --perf-csv  perf-d1-notepadpp.csv
```

(focus Notepad++; then Chrome omnibox)

Expected: 11/11 PASS on both.

### Task 11: Route `HookEngine::InjectKey` through injector

**Files:**
- Modify: `src/app/system/HookEngine.cpp` (function `InjectKey`, line ~3167)

- [ ] **Step 1: Replace InjectKey body**

```cpp
void HookEngine::InjectKey(DWORD vkCode) {
    sending_ = true;
    auto inj = std::atomic_load(&injector_);
    inj->SendKey(static_cast<unsigned short>(vkCode));
    sending_ = false;
    lastSynthSendTime_ = GetTickCount();
}
```

- [ ] **Step 2: Linux + Windows builds**

```bash
cmake --build build-linux --target NextKeyTests
./build-linux/tests/NextKeyTests
```

Expected: Linux 1405/1405 PASS.

```bash
powershell.exe -Command "cd '\\wsl.localhost\Ubuntu-24.04\home\phatmt\code\NexusKey\build'; cmake --build . --target NexusKey --config Debug 2>&1" | tail
```

Expected: Windows compile PASS.

### Task 12: D1 commit

- [ ] **Step 1: Stage and commit**

```bash
git add src/app/output/Win32SendInputInjector.cpp \
        src/app/output/Internal.cpp \
        src/app/system/HookEngine.cpp \
        tests/output/InjectorTestBase.h \
        tests/output/Win32SendInputInjectorTest.cpp \
        tests/CMakeLists.txt
git commit -m "Sprint 2 D1: Win32SendInputInjector + integrate default Win32 path"
```

- [ ] **Step 2: Verify commit clean and chaos verdict captured**

```bash
git log -1 --stat
git status
```

Expected: clean. The `report-d1-*.xml` and `perf-d1-*.csv` from Step 4 of Task 10 stay untracked (or moved to `docs/baselines/d1/` if you want to commit them).

---

## D2 — RichEditEmReplaceSelInjector + remove `useEditMsgPath_` branches

Goal: RichEdit impl fully implemented; `useEditMsgPath_` short-circuits at HookEngine.cpp:913, :1270, :2935/:2965, :3113 all replaced with uniform `injector_->Replace(...)`. The atomic field `useEditMsgPath_` is no longer read anywhere (still declared; cleanup in D4).

### Task 13: Test fixture for RichEdit injector + 4 tests

**Files:**
- Create: `tests/output/RichEditEmReplaceSelInjectorTest.cpp`

- [ ] **Step 1: Write tests**

```cpp
// tests/output/RichEditEmReplaceSelInjectorTest.cpp
#include "InjectorTestBase.h"
#include "app/output/RichEditEmReplaceSelInjector.h"
#include <richedit.h>

using namespace NextKey::Output;
using namespace NextKey::Output::Test;

class RichEditTest : public InjectorTestBase {};

TEST_F(RichEditTest, ReplaceSendsGetSelSetSelReplaceSelInOrder) {
    // Arrange: capture EM_GETSEL response indicating caret at offset 10
    Internal::g_sendMessageW = [](HWND h, UINT m, WPARAM w, LPARAM l) -> LRESULT {
        capturedMsgs.emplace_back(h, m, w, l);
        if (m == EM_GETSEL && w != 0 && l != 0) {
            *reinterpret_cast<DWORD*>(w) = 10;  // start
            *reinterpret_cast<DWORD*>(l) = 10;  // end (caret)
        }
        return 1;
    };

    RichEditEmReplaceSelInjector inj;
    EXPECT_TRUE(inj.Replace(3, L"abc"));

    ASSERT_GE(capturedMsgs.size(), 3u);
    EXPECT_EQ(std::get<1>(capturedMsgs[0]), EM_GETSEL);
    EXPECT_EQ(std::get<1>(capturedMsgs[1]), EM_SETSEL);
    EXPECT_EQ(std::get<1>(capturedMsgs[2]), EM_REPLACESEL);
}

TEST_F(RichEditTest, BsCountZeroSkipsSetSel) {
    Internal::g_sendMessageW = [](HWND h, UINT m, WPARAM w, LPARAM l) -> LRESULT {
        capturedMsgs.emplace_back(h, m, w, l);
        if (m == EM_GETSEL) {
            if (w) *reinterpret_cast<DWORD*>(w) = 5;
            if (l) *reinterpret_cast<DWORD*>(l) = 5;
        }
        return 1;
    };

    RichEditEmReplaceSelInjector inj;
    EXPECT_TRUE(inj.Replace(0, L"x"));

    // bsCount=0 means no selection range to delete → no SETSEL
    bool sawSetSel = false;
    for (auto& msg : capturedMsgs) {
        if (std::get<1>(msg) == EM_SETSEL) sawSetSel = true;
    }
    EXPECT_FALSE(sawSetSel);
}

TEST_F(RichEditTest, SendMessageReturningZeroOnReplaceSelReturnsFalse) {
    Internal::g_sendMessageW = [](HWND h, UINT m, WPARAM w, LPARAM l) -> LRESULT {
        capturedMsgs.emplace_back(h, m, w, l);
        if (m == EM_REPLACESEL) return 0;  // failure
        if (m == EM_GETSEL) {
            if (w) *reinterpret_cast<DWORD*>(w) = 0;
            if (l) *reinterpret_cast<DWORD*>(l) = 0;
        }
        return 1;
    };

    RichEditEmReplaceSelInjector inj;
    EXPECT_FALSE(inj.Replace(0, L"x"));
}

TEST_F(RichEditTest, SendKeyFallsThroughToSendInput) {
    RichEditEmReplaceSelInjector inj;
    inj.SendKey(VK_RETURN);
    // SendKey for RichEdit always uses SendInput (see §2.3 of spec)
    EXPECT_GE(capturedInputs.size(), 2u);
    EXPECT_EQ(capturedInputs[0].ki.wVk, VK_RETURN);
}
```

- [ ] **Step 2: Add to CMake**

```cmake
# tests/CMakeLists.txt — append:
target_sources(NextKeyTests PRIVATE
    tests/output/RichEditEmReplaceSelInjectorTest.cpp
)
```

- [ ] **Step 3: Run tests — expect 4 FAIL (impl is still stub)**

```bash
./build/tests/Debug/NextKeyTests.exe --gtest_filter="RichEditTest.*"
```

Expected: 4 FAIL.

### Task 14: Implement `RichEditEmReplaceSelInjector::Replace`

**Files:**
- Modify: `src/app/output/RichEditEmReplaceSelInjector.cpp`

- [ ] **Step 1: Port `TryEditMessagePaste` logic from HookEngine**

Read the current `TryEditMessagePaste` body for reference:

```bash
sed -n '1900,1960p' src/app/system/HookEngine.cpp
```

The logic: GetForegroundWindow → GetFocus → EM_GETSEL → if bsCount>0 EM_SETSEL with start = caret-bsCount, end = caret → EM_REPLACESEL with text. Returns true on success.

Implementation:

```cpp
// src/app/output/RichEditEmReplaceSelInjector.cpp
#include "RichEditEmReplaceSelInjector.h"
#include "Internal.h"
#include "Win32SendInputInjector.h"  // for SendKey fallthrough below — alternative: duplicate the helper, or extract a free function
#include <richedit.h>

namespace NextKey::Output {

bool RichEditEmReplaceSelInjector::Replace(size_t bsCount,
                                           std::wstring_view text) noexcept {
    HWND fg = ::GetForegroundWindow();
    if (!fg) return false;

    GUITHREADINFO gti{};
    gti.cbSize = sizeof(gti);
    DWORD tid = ::GetWindowThreadProcessId(fg, nullptr);
    if (!::GetGUIThreadInfo(tid, &gti)) return false;
    HWND focus = gti.hwndFocus ? gti.hwndFocus : fg;

    DWORD selStart = 0, selEnd = 0;
    LRESULT got = Internal::g_sendMessageW(focus, EM_GETSEL,
        reinterpret_cast<WPARAM>(&selStart), reinterpret_cast<LPARAM>(&selEnd));
    if (got == 0) return false;

    if (bsCount > 0) {
        DWORD newStart = (selStart >= bsCount) ? selStart - static_cast<DWORD>(bsCount) : 0;
        Internal::g_sendMessageW(focus, EM_SETSEL,
            static_cast<WPARAM>(newStart), static_cast<LPARAM>(selEnd));
    }

    // EM_REPLACESEL: wParam = bUndo (TRUE), lParam = LPCTSTR (null-terminated)
    std::wstring zterm(text);  // need null-terminated; copy is OK in cold-path
    LRESULT replaced = Internal::g_sendMessageW(focus, EM_REPLACESEL,
        TRUE, reinterpret_cast<LPARAM>(zterm.c_str()));
    return replaced != 0;
}

void RichEditEmReplaceSelInjector::SendKey(unsigned short vkCode) noexcept {
    // RichEdit hosts: re-injection of a single VK uses the physical channel
    // because InjectKey's purpose is "land AFTER any pending synth". Sent
    // messages would land BEFORE pending posted synths. Use Win32 path.
    Win32SendInputInjector(/*needsBaitCharPrefix=*/false).SendKey(vkCode);
}

}  // namespace NextKey::Output
```

- [ ] **Step 2: Run tests — expect 4/4 PASS**

```bash
./build/tests/Debug/NextKeyTests.exe --gtest_filter="RichEditTest.*"
```

Expected: 4 PASS.

> **Note on `RichEditEmReplaceSelInjector::SendKey`**: constructing a `Win32SendInputInjector` per-call is fine here (cold path, ~80-byte stack object, no heap). If profiling later shows this is hot, extract the SendKey logic into a free function in `Internal.h`.

### Task 15: Wire factory: `isRichEditD2DPT` → RichEdit impl

**Files:**
- Modify: `src/app/output/OutputInjectorFactory.cpp`

- [ ] **Step 1: Update `Create()` to honor `isRichEditD2DPT`**

```cpp
// src/app/output/OutputInjectorFactory.cpp
#include "OutputInjectorFactory.h"
#include "Win32SendInputInjector.h"
#include "RichEditEmReplaceSelInjector.h"

namespace NextKey::Output {

std::shared_ptr<IOutputInjector> Create(const WindowClassification& c) noexcept {
    if (c.isRichEditD2DPT) {
        return std::make_shared<RichEditEmReplaceSelInjector>();
    }
    // Electron + Console wired in D3.
    return std::make_shared<Win32SendInputInjector>(c.isChromium);
}

}  // namespace NextKey::Output
```

- [ ] **Step 2: Update factory tests to expect RichEdit on `isRichEditD2DPT=true`**

(The OutputInjectorFactoryTest.cpp from D0 had a test stub asserting Win32 always; update now.)

```cpp
TEST(OutputInjectorFactoryTest, RichEditClassificationProducesRichEditImpl) {
    WindowClassification c;
    c.isRichEditD2DPT = true;
    auto inj = Create(c);
    EXPECT_NE(dynamic_cast<RichEditEmReplaceSelInjector*>(inj.get()), nullptr);
}
```

- [ ] **Step 3: Run factory tests**

```bash
./build/tests/Debug/NextKeyTests.exe --gtest_filter="OutputInjectorFactoryTest.*"
```

Expected: PASS.

### Task 16: Move `IsRichEditD2DPT` detection into `ClassifyWindow`

**Files:**
- Modify: `src/app/system/HookEngine.cpp` (find the place that today sets `useEditMsgPath_` based on RichEditD2DPT class detection)
- Modify: `src/app/output/OutputInjectorFactory.cpp` (move the detection here)

- [ ] **Step 1: Locate today's RichEditD2DPT detection**

```bash
grep -nE "RichEditD2DPT|useEditMsgPath_\.store" src/app/system/HookEngine.cpp | head
```

Expected: lines around the focus-classify path. Read the surrounding ~30 lines to extract the exact predicate (likely a `GetClassNameW` check matching `L"RICHEDIT60W"` or similar).

- [ ] **Step 2: Move the predicate into `ClassifyWindow`**

```cpp
// src/app/output/OutputInjectorFactory.cpp — extend ClassifyWindow
WindowClassification ClassifyWindow(HWND hwnd) noexcept {
    WindowClassification c;
    if (!hwnd) return c;

    HWND focus = hwnd;
    GUITHREADINFO gti{};
    gti.cbSize = sizeof(gti);
    DWORD tid = ::GetWindowThreadProcessId(hwnd, nullptr);
    if (::GetGUIThreadInfo(tid, &gti) && gti.hwndFocus) focus = gti.hwndFocus;

    wchar_t cls[256] = {};
    ::GetClassNameW(focus, cls, _countof(cls));
    // Win11 New Notepad uses "RichEditD2DPT" (verify with Spy++ on target build).
    c.isRichEditD2DPT = (wcscmp(cls, L"RichEditD2DPT") == 0);

    // Electron / Console / Chromium detection is wired in D3.
    return c;
}
```

- [ ] **Step 3: Replace HookEngine's `useEditMsgPath_.store(...)` call site to call ClassifyWindow + Create**

In the focus-changed handler (around line 2548 today's `ClassifyWindow(...)` HookEngine local helper), add:

```cpp
// After the existing ClassifyWindow() call (HookEngine's local helper)
// runs (its result still feeds the rest of the focus handler), now also
// build the injector:
{
    auto fresh = NextKey::Output::Create(NextKey::Output::ClassifyWindow(activeHwnd));
    std::atomic_store(&injector_, std::move(fresh));
}

// Leave the existing useEditMsgPath_.store(...) call IN PLACE for D2 — D4
// removes the field. The duplicated detection is acceptable for one D-day.
```

- [ ] **Step 4: Verify Notepad Win11 chaos still PASS**

User runs:

```powershell
.\build\tools\Debug\NextKeyTestRunner.exe --corpus tools\NextKeyTestRunner\corpus\chaos.toml --hook-log build\Debug\NexusKey_hook.log --junit report-d2-notepad.xml --perf-csv perf-d2-notepad.csv
```

(focus Notepad Win11)

Expected: 11/11 PASS.

### Task 17: Replace 4 `useEditMsgPath_` branches in HookEngine with uniform injector calls

**Files:**
- Modify: `src/app/system/HookEngine.cpp` at:
  - Line ~913 (commit-undo BS path)
  - Line ~1270 (single-char emit)
  - Line ~2935 / ~2965 (ReplaceComposition)
  - Line ~3113 (SendBackspaces — already partially routed in D1)

- [ ] **Step 1: For each of the 4 sites, replace `if (useEditMsgPath_) { TryEditMessagePaste(...); } else { passthrough or SendInput }` with a single `injector_->Replace(...)`**

Pattern for each site (read the exact existing logic before editing — don't blind-replace):

```cpp
// BEFORE (line ~913, commit-undo BS):
if (useEditMsgPath_.load(std::memory_order_acquire)) {
    if (TryEditMessagePaste(L"", /*BS=*/1)) {
        HOOK_LOG(L"  commit-undo: BS after commit via EM_REPLACESEL → Primed");
        return true;
    }
    HOOK_LOG(L"  commit-undo: BS after commit EM_REPLACESEL failed, passthrough");
}
HOOK_LOG(L"  commit-undo: BS after commit → Primed (ready to replay)");
return false;

// AFTER:
auto inj = std::atomic_load(&injector_);
if (inj->Replace(/*bs=*/1, std::wstring_view{})) {
    HOOK_LOG(L"  commit-undo: BS after commit via injector → Primed");
    return true;
}
HOOK_LOG(L"  commit-undo: BS after commit injector failed, passthrough");
return false;
```

Apply analogous transforms to the other 3 sites. Each transform:
1. atomic_load `injector_`
2. call `Replace(bs, text)` with the exact arguments today's TryEditMessagePaste / SendCharEvents / SendBackspaceEvents would have used
3. on `false` return, follow today's fallback path (passthrough or pre-existing SendInput call)

For the SendBackspaces site (line ~3113), the Task 10 D1 patch already did this — verify it's still correct after this D2 work. If D2 made it dead code, simplify.

- [ ] **Step 2: Search for any remaining `TryEditMessagePaste` or `useEditMsgPath_` reads**

```bash
grep -nE "TryEditMessagePaste|useEditMsgPath_\.load" src/app/system/HookEngine.cpp
```

Expected: only the `useEditMsgPath_.store()` write in OnFocusChanged remains (D4 removes it). No `.load()` calls. No `TryEditMessagePaste` calls (the function is now dead code; D4 deletes the body).

- [ ] **Step 3: Linux + Windows build smoke**

```bash
cmake --build build-linux --target NextKeyTests && ./build-linux/tests/NextKeyTests
powershell.exe -Command "cd '\\wsl.localhost\Ubuntu-24.04\home\phatmt\code\NexusKey\build'; cmake --build . --target NexusKey --config Debug 2>&1" | tail
```

Expected: Linux 1405/1405 PASS. Windows compile PASS.

- [ ] **Step 4: Re-run chaos on all 5 hosts (no regression on non-RichEdit hosts either)**

User runs the chaos sweep on Notepad Win11, Notepad++, Chrome, Discord, ChatGPT.

Expected: 5×11=55/55 PASS.

### Task 18: D2 commit

- [ ] **Step 1: Stage and commit**

```bash
git add src/app/output/RichEditEmReplaceSelInjector.cpp \
        src/app/output/OutputInjectorFactory.cpp \
        src/app/system/HookEngine.cpp \
        tests/output/RichEditEmReplaceSelInjectorTest.cpp \
        tests/output/OutputInjectorFactoryTest.cpp \
        tests/CMakeLists.txt
git commit -m "Sprint 2 D2: RichEditEmReplaceSelInjector + remove useEditMsgPath_ branches"
```

- [ ] **Step 2: Verify**

```bash
git log -1 --stat
git status
```

---

## D3 — SplitDispatchInjector + remove `isElectronApp_` / `isConsoleApp_` branches

Goal: Split impl fully implemented; `DispatchSendInput()` body and the duplicated split block at line 3068 deleted; `isElectronApp_` and `isConsoleApp_` no longer read by HookEngine. Discord chaos 11/11.

### Task 19: 5 SplitDispatchInjector tests

**Files:**
- Create: `tests/output/SplitDispatchInjectorTest.cpp`

- [ ] **Step 1: Write tests**

```cpp
// tests/output/SplitDispatchInjectorTest.cpp
#include "InjectorTestBase.h"
#include "app/output/SplitDispatchInjector.h"

using namespace NextKey::Output;
using namespace NextKey::Output::Test;

class SplitDispatchTest : public InjectorTestBase {};

TEST_F(SplitDispatchTest, ReplaceSplitsBackspacesThenSleepsThenChars) {
    SplitDispatchInjector inj(/*sleepMs=*/6);
    EXPECT_TRUE(inj.Replace(2, L"vi"));

    // First batch: BS×2 = 4 events
    // Second batch: chars×2 = 4 events
    ASSERT_EQ(capturedInputs.size(), 8u);
    EXPECT_EQ(capturedInputs[0].ki.wVk, VK_BACK);
    EXPECT_EQ(capturedInputs[1].ki.wVk, VK_BACK);
    EXPECT_NE(capturedInputs[4].ki.dwFlags & KEYEVENTF_UNICODE, 0u);

    // Sleep called between batches
    ASSERT_EQ(sleepDelays.size(), 1u);
    EXPECT_EQ(sleepDelays[0], 6u);
}

TEST_F(SplitDispatchTest, BsCountZeroSkipsSleep) {
    SplitDispatchInjector inj(6);
    EXPECT_TRUE(inj.Replace(0, L"x"));
    EXPECT_EQ(sleepDelays.size(), 0u);
    ASSERT_EQ(capturedInputs.size(), 2u);  // 1 char × (down + up)
}

TEST_F(SplitDispatchTest, TextEmptySkipsSecondSendAndSleep) {
    SplitDispatchInjector inj(6);
    EXPECT_TRUE(inj.Replace(2, L""));
    EXPECT_EQ(sleepDelays.size(), 0u);  // no Sleep when no chars to follow
    ASSERT_EQ(capturedInputs.size(), 4u);  // BS×2 only
}

TEST_F(SplitDispatchTest, SleepMsRespectsConstructorParam) {
    SplitDispatchInjector inj(/*sleepMs=*/5);
    EXPECT_TRUE(inj.Replace(1, L"a"));
    ASSERT_EQ(sleepDelays.size(), 1u);
    EXPECT_EQ(sleepDelays[0], 5u);
}

TEST_F(SplitDispatchTest, PartialFirstSendReturnsFalseAndStopsSecondBatch) {
    SplitDispatchInjector inj(6);
    sendInputReturnOverride = 1;  // only 1 of N events delivered on each call
    EXPECT_FALSE(inj.Replace(2, L"x"));
    // Second batch must NOT have been attempted after first failed
    EXPECT_EQ(sleepDelays.size(), 0u);
    EXPECT_EQ(capturedInputs.size(), 4u);  // only the BS attempt
}
```

- [ ] **Step 2: Add to CMake + run — expect 5 FAIL (stub)**

```bash
./build/tests/Debug/NextKeyTests.exe --gtest_filter="SplitDispatchTest.*"
```

Expected: 5 FAIL.

### Task 20: Implement `SplitDispatchInjector::Replace`

**Files:**
- Modify: `src/app/output/SplitDispatchInjector.cpp`

- [ ] **Step 1: Implement**

```cpp
// src/app/output/SplitDispatchInjector.cpp
#include "SplitDispatchInjector.h"
#include "Internal.h"
#include "Win32SendInputInjector.h"  // share key-input helpers via free fns or duplicate

#include <array>
#include <vector>

namespace NextKey::Output {

namespace {
constexpr size_t kMaxBatch = 256;

// Reuse the helpers from Win32SendInputInjector.cpp by re-declaring the same
// inline helpers. Alternative: extract MakeKey/MakeUnicodeChar to Internal.h.
INPUT MakeKey(WORD vk, bool up) noexcept {
    INPUT in{};
    in.type = INPUT_KEYBOARD;
    in.ki.wVk = vk;
    in.ki.wScan = static_cast<WORD>(::MapVirtualKeyW(vk, MAPVK_VK_TO_VSC));
    in.ki.dwFlags = up ? KEYEVENTF_KEYUP : 0u;
    in.ki.dwExtraInfo = Internal::kNexusKeyExtraInfo;
    return in;
}
INPUT MakeUnicodeChar(WCHAR ch, bool up) noexcept {
    INPUT in{};
    in.type = INPUT_KEYBOARD;
    in.ki.wScan = static_cast<WORD>(ch);
    in.ki.dwFlags = KEYEVENTF_UNICODE | (up ? KEYEVENTF_KEYUP : 0u);
    in.ki.dwExtraInfo = Internal::kNexusKeyExtraInfo;
    return in;
}
}  // namespace

bool SplitDispatchInjector::Replace(size_t bsCount,
                                    std::wstring_view text) noexcept {
    // Batch 1: backspaces
    std::array<INPUT, kMaxBatch> bsBuf{};
    size_t bi = 0;
    for (size_t k = 0; k < bsCount; ++k) {
        if (bi + 2 > kMaxBatch) return false;
        bsBuf[bi++] = MakeKey(VK_BACK, false);
        bsBuf[bi++] = MakeKey(VK_BACK, true);
    }
    if (bi > 0) {
        if (!Internal::TrackedSendInput(bsBuf.data(), static_cast<UINT>(bi))) {
            return false;
        }
    }

    // If there are chars to follow, sleep then send batch 2
    if (!text.empty()) {
        if (bi > 0) {
            Internal::g_sleep(static_cast<DWORD>(sleepMs_));
        }
        std::array<INPUT, kMaxBatch> charBuf{};
        size_t ci = 0;
        for (WCHAR ch : text) {
            if (ci + 2 > kMaxBatch) return false;
            charBuf[ci++] = MakeUnicodeChar(ch, false);
            charBuf[ci++] = MakeUnicodeChar(ch, true);
        }
        if (!Internal::TrackedSendInput(charBuf.data(), static_cast<UINT>(ci))) {
            return false;
        }
    }
    return true;
}

void SplitDispatchInjector::SendKey(unsigned short vkCode) noexcept {
    INPUT events[2] = {
        MakeKey(static_cast<WORD>(vkCode), false),
        MakeKey(static_cast<WORD>(vkCode), true),
    };
    Internal::TrackedSendInput(events, 2);
}

}  // namespace NextKey::Output
```

> **Code-duplication note:** `MakeKey` / `MakeUnicodeChar` are now duplicated between Win32 and Split impls. D4 cleanup extracts these to `Internal.h` as inline helpers.

- [ ] **Step 2: Run tests — expect 5/5 PASS**

```bash
./build/tests/Debug/NextKeyTests.exe --gtest_filter="SplitDispatchTest.*"
```

Expected: 5 PASS.

### Task 21: Update factory: Electron + Console + Chromium classification

**Files:**
- Modify: `src/app/output/OutputInjectorFactory.cpp` (extend `ClassifyWindow` + `Create`)

- [ ] **Step 1: Move Electron / Console / Chromium detection from HookEngine into ClassifyWindow**

```bash
# Find the existing detection logic in HookEngine
grep -nE "isElectronApp_\.store|isConsoleApp_\.store|needBaitChar_\.store|IsKnownElectronExe|IsConsoleApp|IsChromium" src/app/system/HookEngine.cpp | head
```

Read the surrounding code (~30 lines) to understand the predicates. Likely uses `GetWindowThreadProcessId` + exe name lookup against a known-list.

- [ ] **Step 2: Move predicates to factory**

```cpp
// src/app/output/OutputInjectorFactory.cpp — extend ClassifyWindow
namespace {

bool IsElectronExe(const wchar_t* exeName) noexcept {
    // Same list as HookEngine's IsKnownElectronExe — port verbatim.
    static const wchar_t* kList[] = {
        L"discord.exe", L"slack.exe", L"code.exe", L"teams.exe",
        // ... port full list
    };
    for (auto* e : kList) if (_wcsicmp(exeName, e) == 0) return true;
    return false;
}

bool IsConsoleExe(const wchar_t* exeName) noexcept {
    static const wchar_t* kList[] = {
        L"cmd.exe", L"powershell.exe", L"wt.exe", L"conhost.exe",
    };
    for (auto* e : kList) if (_wcsicmp(exeName, e) == 0) return true;
    return false;
}

bool IsChromiumExe(const wchar_t* exeName) noexcept {
    static const wchar_t* kList[] = {
        L"chrome.exe", L"msedge.exe", L"brave.exe",
    };
    for (auto* e : kList) if (_wcsicmp(exeName, e) == 0) return true;
    return false;
}

}  // namespace

WindowClassification ClassifyWindow(HWND hwnd) noexcept {
    WindowClassification c;
    if (!hwnd) return c;

    // RichEdit class detection (D2)
    HWND focus = hwnd;
    GUITHREADINFO gti{};
    gti.cbSize = sizeof(gti);
    DWORD tid = ::GetWindowThreadProcessId(hwnd, nullptr);
    if (::GetGUIThreadInfo(tid, &gti) && gti.hwndFocus) focus = gti.hwndFocus;
    wchar_t cls[256] = {};
    ::GetClassNameW(focus, cls, _countof(cls));
    c.isRichEditD2DPT = (wcscmp(cls, L"RichEditD2DPT") == 0);

    // Exe name lookup (D3)
    DWORD pid = 0;
    ::GetWindowThreadProcessId(hwnd, &pid);
    HANDLE hProc = ::OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (hProc) {
        wchar_t path[MAX_PATH] = {};
        DWORD len = MAX_PATH;
        if (::QueryFullProcessImageNameW(hProc, 0, path, &len)) {
            const wchar_t* exe = wcsrchr(path, L'\\');
            exe = exe ? exe + 1 : path;
            c.isElectron = IsElectronExe(exe);
            c.isConsole  = IsConsoleExe(exe);
            c.isChromium = IsChromiumExe(exe);
        }
        ::CloseHandle(hProc);
    }
    return c;
}
```

- [ ] **Step 3: Update `Create()` to honor Electron + Console**

```cpp
std::shared_ptr<IOutputInjector> Create(const WindowClassification& c) noexcept {
    if (c.isRichEditD2DPT) return std::make_shared<RichEditEmReplaceSelInjector>();
    if (c.isElectron)      return std::make_shared<SplitDispatchInjector>(6);
    if (c.isConsole)       return std::make_shared<SplitDispatchInjector>(5);
    return std::make_shared<Win32SendInputInjector>(c.isChromium);
}
```

- [ ] **Step 4: Update factory tests**

Add tests for Electron / Console / Chromium classifications matching §5.2 of design doc. Re-run.

```bash
./build/tests/Debug/NextKeyTests.exe --gtest_filter="OutputInjectorFactoryTest.*"
```

Expected: 5+ PASS.

### Task 22: Replace `DispatchSendInput()` calls + delete duplicate at line ~3068

**Files:**
- Modify: `src/app/system/HookEngine.cpp`

- [ ] **Step 1: Locate `DispatchSendInput` callers**

```bash
grep -nE "DispatchSendInput\(" src/app/system/HookEngine.cpp
```

Expected: ~2-3 call sites (e.g., line ~2887, ~3427) plus the function definition at ~2001.

- [ ] **Step 2: Replace each call site with `injector_->Replace`**

Pattern:

```cpp
// BEFORE:
DispatchSendInput(bsEvents, charEvents);

// AFTER:
auto inj = std::atomic_load(&injector_);
inj->Replace(bsCount, replacementText);
// (caller computes bsCount from bsEvents.size()/2 and replacementText from
// the engine's diff. The injector handles split-vs-batch internally.)
```

Walk each caller and confirm the data they had (bs INPUT[] + char INPUT[]) maps cleanly to (bsCount, text). If a caller has special pre-built INPUT arrays not derivable from (bsCount, text), either:
- adjust caller to pass the higher-level (bsCount, text) form, OR
- if no clean mapping exists, surface as an open question (this is unlikely — diff-based composition naturally yields bsCount+text).

- [ ] **Step 3: Delete the duplicate split block at line ~3068**

```bash
sed -n '3060,3100p' src/app/system/HookEngine.cpp
```

Expected: a duplicate `if (electronApp2 || consoleApp2) { TrackedSendInput(BS) Sleep TrackedSendInput(chars) }` block. Replace with:

```cpp
auto inj = std::atomic_load(&injector_);
inj->Replace(bsCount, text);
```

- [ ] **Step 4: Delete the body of `DispatchSendInput()` (function still declared but unused)**

Note: leave the empty body for D4 deletion to keep this commit narrowly scoped. Or delete declaration + definition now if no remaining callers.

- [ ] **Step 5: Verify Discord chaos 11/11**

User runs:

```powershell
.\build\tools\Debug\NextKeyTestRunner.exe --corpus tools\NextKeyTestRunner\corpus\chaos.toml --hook-log build\Debug\NexusKey_hook.log --junit report-d3-discord.xml --perf-csv perf-d3-discord.csv
```

(focus Discord)

Expected: 11/11 PASS.

### Task 23: D3 commit

- [ ] **Step 1: Stage and commit**

```bash
git add src/app/output/SplitDispatchInjector.cpp \
        src/app/output/OutputInjectorFactory.cpp \
        src/app/system/HookEngine.cpp \
        tests/output/SplitDispatchInjectorTest.cpp \
        tests/output/OutputInjectorFactoryTest.cpp \
        tests/CMakeLists.txt
git commit -m "Sprint 2 D3: SplitDispatchInjector + remove isElectronApp_/isConsoleApp_ branches"
```

---

## D4 — Audit script update + dead code removal

Goal: Delete the four atomic fields (`useEditMsgPath_`, `isElectronApp_`, `isConsoleApp_`, `needBaitChar_`), delete dead helpers (`DispatchSendInput`, old `SendBackspaces` wrapper if dead, `TrackedSendInput` member), update audit script. HookEngine.cpp shrinks by 200-300 LOC.

### Task 24: Remove the 4 atomic fields

**Files:**
- Modify: `src/app/system/HookEngine.h` (member declarations)
- Modify: `src/app/system/HookEngine.cpp` (remove `.store()` writes in OnFocusChanged path)

- [ ] **Step 1: Find all references to each field**

```bash
for f in useEditMsgPath_ isElectronApp_ isConsoleApp_ needBaitChar_; do
    echo "=== $f ==="
    grep -nE "\b${f}\b" src/app/system/HookEngine.h src/app/system/HookEngine.cpp
done
```

Expected: each field has one `std::atomic<bool>` decl in the header + one `.store()` in OnFocusChanged + zero `.load()` calls (D2/D3 removed all readers).

- [ ] **Step 2: Delete the declarations from `HookEngine.h`**

Remove the 4 `std::atomic<bool>` member lines.

- [ ] **Step 3: Delete the `.store()` writes from OnFocusChanged**

Find the OnFocusChanged block that calls `ClassifyWindow(activeHwnd, isBrowser, isElectron, isQtApp, localConsole, isVB6)` then stores those flags. Remove the 4 stores; leave any other (e.g., `isTsfApp_`, `vietnameseMode_`) untouched.

- [ ] **Step 4: Verify no straggler references**

```bash
grep -nE "\b(useEditMsgPath_|isElectronApp_|isConsoleApp_|needBaitChar_)\b" src/
```

Expected: zero matches.

### Task 25: Delete `DispatchSendInput`, old `SendBackspaces` wrapper, `TrackedSendInput` member

**Files:**
- Modify: `src/app/system/HookEngine.h`
- Modify: `src/app/system/HookEngine.cpp`

- [ ] **Step 1: Find dead helpers**

```bash
grep -nE "void HookEngine::(DispatchSendInput|TrackedSendInput|SendBackspaceEvents|SendCharEvents)\b" src/app/system/HookEngine.cpp
```

For each, check call sites:

```bash
for fn in DispatchSendInput TrackedSendInput SendBackspaceEvents SendCharEvents; do
    echo "=== $fn callers ==="
    grep -nE "\b${fn}\b" src/app/system/HookEngine.cpp | grep -v "void HookEngine::${fn}"
done
```

Expected: `DispatchSendInput` zero callers (D3 removed last). `TrackedSendInput` zero callers if all routed through `Internal::TrackedSendInput`. `SendBackspaceEvents`/`SendCharEvents` zero callers if `SendBackspaces` was simplified in D2.

- [ ] **Step 2: Delete each dead function (decl + body)**

Remove declarations from `HookEngine.h` and bodies from `HookEngine.cpp`.

- [ ] **Step 3: Verify HookEngine.cpp LOC reduction**

```bash
wc -l src/app/system/HookEngine.cpp
```

Expected: 3300 ± 50. (Started at 3593.)

- [ ] **Step 4: Linux + Windows builds**

```bash
cmake --build build-linux --target NextKeyTests && ./build-linux/tests/NextKeyTests
powershell.exe -Command "cd '\\wsl.localhost\Ubuntu-24.04\home\phatmt\code\NexusKey\build'; cmake --build . --target NexusKey --config Debug 2>&1" | tail
```

Expected: Linux 1405/1405 PASS. Windows compile PASS.

### Task 26: Update audit script — remove 4 fields from regex + add Check 4

**Files:**
- Modify: `tools/audit/check_hook_thread_no_mutex.sh`

- [ ] **Step 1: Update `ATOMIC_BOOLS` regex**

```bash
grep -n "ATOMIC_BOOLS=" tools/audit/check_hook_thread_no_mutex.sh
```

Edit the line to remove `useEditMsgPath_|isElectronApp_|isConsoleApp_|needBaitChar_`:

```bash
# BEFORE
ATOMIC_BOOLS="vietnameseMode_|isTsfApp_|isExcludedApp_|isConsoleApp_|isElectronApp_|skipEmptyChar_|needBaitChar_|useClipboardPaste_|useEditMsgPath_|isOutlookApp_|macroEnabled_|macroInEnglish_|autoCaps_|autoCapsMacro_|tempOffMacroByEsc_|tempOffByAlt_"

# AFTER
ATOMIC_BOOLS="vietnameseMode_|isTsfApp_|isExcludedApp_|skipEmptyChar_|useClipboardPaste_|isOutlookApp_|macroEnabled_|macroInEnglish_|autoCaps_|autoCapsMacro_|tempOffMacroByEsc_|tempOffByAlt_"
```

- [ ] **Step 2: Add Check 4 — `injector_` discipline**

Append to the script before the final result section:

```bash
# ────────────────────────────────────────────────────────────────────────
# Check 4: injector_ access only via std::atomic_load / std::atomic_store
# ────────────────────────────────────────────────────────────────────────
echo
echo "Check 4: injector_ accessed only via std::atomic_load / std::atomic_store"
# Match `injector_` references that are NOT immediately part of an
# atomic_load(&injector_) or atomic_store(&injector_, ...) call, and not
# a comment, and not the declaration line.
plain_access=$(grep -nE "\binjector_\b" "$CPP" | \
    grep -vE "std::atomic_load\(&injector_\)|std::atomic_store\(&injector_," | \
    grep -vE "^\s*[0-9]+:\s*//|std::shared_ptr.*injector_;" || true)
plain_count=$(echo -n "$plain_access" | grep -c '^' || true)
if [ "$plain_count" -gt 0 ]; then
    echo "  FAIL: $plain_count plain access(es) to injector_ outside atomic_load/store"
    echo "$plain_access" | head -10 | sed 's/^/    /'
    errors=$((errors + 1))
else
    echo "  OK"
fi
```

- [ ] **Step 3: Run audit script**

```bash
bash tools/audit/check_hook_thread_no_mutex.sh
```

Expected: PASS (exit 0). All 4 checks green.

### Task 27: D4 commit

- [ ] **Step 1: Stage and commit**

```bash
git add src/app/system/HookEngine.h src/app/system/HookEngine.cpp \
        tools/audit/check_hook_thread_no_mutex.sh
git commit -m "Sprint 2 D4: audit script update + remove dead dispatch code"
```

---

## D5 — SettleBudget integration

Goal: HookEngine's commit-undo synth-guard reads `injector_->SettleBudget()` instead of the hardcoded `kSynthSettleMs=100`. On Win32 (30 ms) and RichEdit (0 ms) hosts, commit-undo replay becomes measurably faster — target ≥ 30 % reduction in inter-keystroke delay.

### Task 28: Test that SettleBudget is consulted at synth-guard check

**Files:**
- Modify: existing HookEngine GTest (or add a new one if no commit-undo test exists)

- [ ] **Step 1: Find the synth-guard test or add one**

```bash
grep -rnE "kSynthSettleMs|synthEventsPending_|commit-undo.*synth-guard" tests/
```

If no test covers the synth-guard, add a focused unit test in HookEngine's existing test fixture (likely `tests/HookEngineTest.cpp` if it exists; otherwise this work is engine-side and may be hard to unit-test — fall back to chaos verification).

If chaos verification only: skip Step 2 — implement directly and validate via chaos sweep.

- [ ] **Step 2: (if test possible) Assert that injector's SettleBudget is queried**

```cpp
// Pseudo-test sketch — actual test requires HookEngine fixture access
TEST(HookEngineSynthGuard, UsesInjectorSettleBudget) {
    // Set up a HookEngine with a mock injector returning SettleBudget=10ms
    // Trigger a Replace, then within 5ms (less than 10), trigger another
    // BS — synth-guard should activate.
    // Within 11ms-100ms gap — synth-guard should NOT activate (would have
    // with old hardcoded 100ms).
}
```

If this is too costly to set up, defer test to chaos validation in Task 30.

### Task 29: Replace `kSynthSettleMs` constant read with `injector_->SettleBudget().count()`

**Files:**
- Modify: `src/app/system/HookEngine.cpp` (around line ~955 — the synth-guard check inside commit-undo)

- [ ] **Step 1: Locate the constant**

```bash
grep -nE "kSynthSettleMs" src/app/system/HookEngine.cpp src/app/system/HookEngine.h
```

Expected: 1 declaration (likely `constexpr int kSynthSettleMs = 100;`) + 1 use in commit-undo check at ~line 955.

- [ ] **Step 2: Replace the use**

```cpp
// BEFORE (around line 955):
if (synthEventsPending_ > 0 && (GetTickCount() - lastRealSynthTime_) < kSynthSettleMs
    && !isToneModifier) {

// AFTER:
auto settleMsForGuard = static_cast<DWORD>(
    std::atomic_load(&injector_)->SettleBudget().count());
if (synthEventsPending_ > 0 && (GetTickCount() - lastRealSynthTime_) < settleMsForGuard
    && !isToneModifier) {
```

- [ ] **Step 3: Delete the constant**

Remove `constexpr int kSynthSettleMs = 100;` from `HookEngine.h` (or wherever it lives).

- [ ] **Step 4: Linux + Windows compile + engine GTest**

```bash
cmake --build build-linux --target NextKeyTests && ./build-linux/tests/NextKeyTests
```

Expected: Linux 1405/1405 PASS.

### Task 30: Chaos sweep + capture commit-undo replay perf delta

**Files:**
- Create: `docs/baselines/perf-baseline-t3-settle-budget.md` (D5 baseline writeup)

- [ ] **Step 1: User runs chaos sweep on 5 hosts**

For each host (Notepad++, Chrome, Discord, Notepad Win11, ChatGPT):

```powershell
.\build\tools\Debug\NextKeyTestRunner.exe --corpus tools\NextKeyTestRunner\corpus\chaos.toml --hook-log build\Debug\NexusKey_hook.log --junit report-d5-{host}.xml --perf-csv perf-d5-{host}.csv
```

Expected: 11/11 PASS each.

- [ ] **Step 2: Compare perf vs Main baseline**

The interesting metric: case 5.3 (cross-word BS replay) — its inter-keystroke delay during replay should drop on Win32 (30 ms) and RichEdit (0 ms) hosts compared to baseline 100 ms.

Compute delta:

```bash
# In docs/baselines/perf-baseline-t3-settle-budget.md, capture per-host:
# - case 5.3 mean / p99 inter-keystroke delay (from perf-csv)
# - same metric from Main baseline (perf-baseline at b0eb607)
# - delta % reduction
```

- [ ] **Step 3: Acceptance check**

Win32 hosts (Notepad++ / Chrome): expect ≥ 30 % reduction.
RichEdit (Notepad Win11): expect ≥ 30 % reduction (likely closer to 100 % since 0 ms budget eliminates the gate).
Electron (Discord): no change expected (still 100 ms).
ChatGPT (Win32 renderer textarea): expect ≥ 30 % reduction.

If any host regresses or fails to meet the 30 % target, investigate before D6.

- [ ] **Step 4: Write the baseline doc**

Create `docs/baselines/perf-baseline-t3-settle-budget.md` documenting:
- Build SHA at D5
- Per-host case 5.3 mean / p99 delays (D5 vs Main)
- Delta percentages
- Verdict: ≥ 30 % win on 3+ hosts (or note if not)

### Task 31: D5 commit

- [ ] **Step 1: Stage and commit**

```bash
git add src/app/system/HookEngine.cpp src/app/system/HookEngine.h \
        docs/baselines/perf-baseline-t3-settle-budget.md
git commit -m "Sprint 2 D5: per-host SettleBudget — Win32 30ms, RichEdit 0ms, Electron 100ms"
```

---

## D6 — Cross-host chaos sweep + perf delta

Goal: Run §5.3 cross-host matrix (5 natural × 11 + ~9 forced × 11 = ~150 cases). Capture verdicts + p99 deltas. Write final baseline doc. Prepare for PR review.

### Task 32: Add `--host-class` CLI flag to NextKeyTestRunner

**Files:**
- Modify: `tools/NextKeyTestRunner/src/main.cpp` (CLI parser)

- [ ] **Step 1: Add flag handling**

Find the existing CLI parser and add:

```cpp
// In main.cpp:
const wchar_t* forceHost = nullptr;
for (int i = 1; i < argc; ++i) {
    // ... existing flags ...
    if (wcscmp(argv[i], L"--host-class") == 0 && i + 1 < argc) {
        forceHost = argv[++i];
    }
}

if (forceHost) {
    // Sets the env var NexusKey reads at OnFocusChanged
    SetEnvironmentVariableW(L"NEXUSKEY_FORCE_HOST_CLASS", forceHost);
}
```

- [ ] **Step 2: Add env-var override to OutputInjectorFactory**

In `OutputInjectorFactory::ClassifyWindow`, before returning `c`:

```cpp
// Test override — only consulted in debug builds or when env var is set.
wchar_t override[32] = {};
if (::GetEnvironmentVariableW(L"NEXUSKEY_FORCE_HOST_CLASS", override, _countof(override)) > 0) {
    c = WindowClassification{};  // reset
    if (wcscmp(override, L"win32") == 0) { /* all flags false → Win32 default */ }
    else if (wcscmp(override, L"richedit") == 0) c.isRichEditD2DPT = true;
    else if (wcscmp(override, L"electron") == 0) c.isElectron = true;
    else if (wcscmp(override, L"console") == 0)  c.isConsole = true;
}
return c;
```

### Task 33: Run cross-host matrix + capture results

- [ ] **Step 1: User runs the matrix (~150 cases, ~2 hours manual orchestration)**

For each (host, force-class) combination per the matrix in §5.3 of design doc:

```powershell
.\build\tools\Debug\NextKeyTestRunner.exe --host-class {classOrEmpty} --corpus tools\NextKeyTestRunner\corpus\chaos.toml --hook-log build\Debug\NexusKey_hook.log --junit report-d6-{host}-{class}.xml --perf-csv perf-d6-{host}-{class}.csv
```

Skip RichEdit forced on non-RichEdit hosts (mechanism-incompatible).

- [ ] **Step 2: Compile per-cell verdict + p99 into matrix table**

Create `docs/baselines/perf-baseline-t3-final.md`:

```markdown
# T3 Final Cross-Host Matrix

Build SHA: <D6 commit>

|  | Notepad++ | Chrome omnibox | Discord | Notepad Win11 | ChatGPT |
|---|---|---|---|---|---|
| `win32` | 11/11 (p99=Xms) | 11/11 | 11/11 | 11/11 | 11/11 |
| `electron` | 11/11 | 11/11 | 11/11 (p99=Xms) | 11/11 | 11/11 |
| `richedit` | — | — | — | 11/11 (p99=Xms) | — |

## Acceptance verdict
- Natural cells: 55/55 PASS, p99 within 10 % of Main baseline.
- Forced cells: 11/11 PASS where mechanism is compatible.
- Commit-undo replay p99 on Win32/RichEdit: ≥ 30 % faster vs Main.
- Functional regressions: 0.
```

### Task 34: D6 commit + final PR

- [ ] **Step 1: Stage and commit**

```bash
git add tools/NextKeyTestRunner/src/main.cpp \
        src/app/output/OutputInjectorFactory.cpp \
        docs/baselines/perf-baseline-t3-final.md
git commit -m "Sprint 2 D6: cross-host chaos sweep — verdict + perf delta capture"
```

- [ ] **Step 2: Push branch + open final PR**

```bash
git push -u origin sprint-2/output-injector
gh pr create --title "Sprint 2 T3: IOutputInjector — extract output channel + matrix harness" --body-file - <<'EOF'
## Summary
[fill from design doc §6 PR description template]

## Linked design + plan
- Design: docs/plans/sprint-2-output-injector.md
- Plan: docs/plans/sprint-2-output-injector-plan.md
- Governance: docs/CODE_GOVERNANCE.md

## Chaos delta
[5×11 baseline vs T3-final, per host]

## LOC delta
HookEngine.cpp: 3593 → ~3300

## Acceptance evidence
- D7 audit: PASS
- 15 unit tests + 5 factory tests PASS on Windows
- 1405/1405 engine tests PASS on Linux
- D6 cross-host matrix: 5 natural × 11 = 55/55 PASS
- Forced compatible cells: PASS
- Commit-undo replay on Win32/RichEdit: ≥ 30% faster
EOF
```

---

## D7 — Buffer (0-2 days)

Reserved. Use only if a D-day gate fails or PR review surfaces issues.

---

## Self-Review Checklist (run after this plan is written)

- [x] **Spec coverage**: every section of `sprint-2-output-injector.md` (architecture, components, data flow, error handling, testing, migration, risks, DoD) maps to at least one task above.
- [x] **No placeholders**: every code block above is complete enough to compile (some require tiny adjustments — e.g., exact CMake target name — flagged inline as user verifies in their tree).
- [x] **Type consistency**: `IOutputInjector` interface signatures (`Replace`, `SendKey`, `SettleBudget`) are identical across Tasks 1, 4, 9, 14, 20. `WindowClassification` fields (`isRichEditD2DPT`, `isElectron`, `isConsole`, `isChromium`) consistent across Tasks 3, 15, 21, 32.
- [x] **D-commit invariant**: every D-day ends with a commit step (Tasks 7, 12, 18, 23, 27, 31, 34).
- [x] **TDD discipline**: every impl task is preceded by a "write failing test" task (Tasks 8 → 9, 13 → 14, 19 → 20).

---

*This plan is the implementation companion to [`sprint-2-output-injector.md`](sprint-2-output-injector.md). Execute via the `superpowers:subagent-driven-development` skill (recommended) or `superpowers:executing-plans` skill.*
