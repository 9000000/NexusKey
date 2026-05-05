# Sprint 2 T3 — IOutputInjector Design

> **Branch:** `sprint-2/output-injector` (to be created when implementation starts)
> **Foundation:** [`docs/PHILOSOPHY.md`](../PHILOSOPHY.md), [`docs/CODE_GOVERNANCE.md`](../CODE_GOVERNANCE.md), [`docs/CODING_RULES/11-hook-system-rules.md`](../CODING_RULES/11-hook-system-rules.md)
> **Scope rule (CODE_GOVERNANCE Part 3):** T3 implements §2 (Output Injection Strategy) ONLY. §1 FSM, §3 SPSC, §4 plugins are roadmap, out of scope.
> **Baseline before T3:** chaos corpus 5 hosts × 11 cases = 55/55 PASS on Main (commit `b0eb607`). T3 must preserve this.

---

## 0. CODE_GOVERNANCE — 5-Question Pre-Code Gate

### Q1 — Layer Check

`IOutputInjector` lives in a new layer **`src/app/output/`**, between the engine layer (HookEngine state machine: Telex pipeline, commit-undo FSM, composition diff) and the OS-API layer (`SendInput`, `SendMessageW`, `Sleep`). It is the **Output Injection Strategy** layer named in CODE_GOVERNANCE §2. Hot-path access is via the `injector_` member; selection (cold path) lives in `OutputInjectorFactory`.

### Q2 — Performance Impact

Hot-path overhead introduced by T3 per emit:

| Step | Cost |
|---|---|
| `std::atomic_load(&injector_)` (RCU shared_ptr) | ~10 ns |
| 1 virtual call via `IOutputInjector*` | ~1 ns (each impl marked `final` → compiler devirtualization when static type known) |
| Mechanism (SendInput / SendMessage / Sleep) | unchanged from today |
| **Net additional cost per Replace/SendKey** | **≈ 11 ns** |

11 ns is < 0.001 % of the 1 ms hook budget (Rule #11.1). On the cold path (focus change), one additional `make_shared` heap alloc (~80 bytes) per focus event — negligible.

### Q3 — Native Alternative

| Alternative considered | Rejected because |
|---|---|
| Inline all 4 sites of `if (useEditMsgPath_) ... else ...` | Already what we have; produces 4 duplicated decision sites + line 3068 split-dispatch duplicate of line 2001. Adding a host class touches 4+ sites. Maintenance cost rises super-linearly. |
| Template-policy dispatch (`HookEngine<TInjector>`) | Cannot swap policy at focus change without recompiling the engine; HookEngine is a single instance, hosts vary at runtime. |
| Function-pointer table indexed by host class | Captures the dispatch but loses per-impl state (SettleBudget, bait-char flag, sleepMs); would need a parallel table for each piece of state, multiplying the source of truth. |
| C++23 `std::expected` style return for callers to inline-pick | Requires every caller to know all impls — defeats the point of abstraction. |

The chosen abstraction (`IOutputInjector` + RCU `shared_ptr`) is the lightest one that closes all four objections.

### Q4 — No-Lock / No-Exception Rule

Hot-path audit (every code path reachable from `LowLevelKeyboardProc`):

| Element | Mutex? | Heap alloc? | try/catch? | Verdict |
|---|---|---|---|---|
| `std::atomic_load(&injector_)` | NO (RCU lock-free) | NO | NO | ✅ |
| `injector_->Replace(bsCount, text)` virtual call | NO | NO | NO (`noexcept`) | ✅ |
| `Win32SendInputInjector::Replace` body | NO | NO (`std::array<INPUT, kMaxBatch>` stack-local) | NO | ✅ |
| `RichEditEmReplaceSelInjector::Replace` body | NO (SendMessage is sync; we don't lock) | NO | NO | ✅ |
| `SplitDispatchInjector::Replace` body | NO | NO | NO | ✅ (`Sleep` is the OS-allowed exception per Rule #11.6) |
| Top-level `try/catch` in `LowLevelKeyboardProc` | — | — | YES (rate-limited) | ✅ Rule #11.5 sole allowed exception |

T3 introduces zero new lock / heap-on-hot-path / try-catch-in-hot-path. ✅ PASS.

### Q5 — Trade-off

| We gain | We give up |
|---|---|
| HookEngine.cpp shrinks ~300 LOC; 4 duplicated decision sites collapse to 1; line 3068 duplicate split block deletes | ~11 ns per emit (rounding error vs syscall) |
| Adding a new host class = 1 new impl file (no HookEngine touch) | ~80 byte heap alloc per focus change (cold path) |
| `SettleBudget()` per host → commit-undo replay 30 ms (Win32) / 0 ms (RichEdit) instead of hardcoded 100 ms ⇒ tangible "Mượt" win | one new folder + 5 new files (`output/`) |
| Aligns with CODE_GOVERNANCE §2 — future host-class additions need no further interface-shape negotiation | ~7 active days of work (within Sprint 2's 2-week budget) |

**Net: positive.** Hot-path cost is rounding error; the "Mượt" win on commit-undo replay is user-perceptible; the maintainability dividend is permanent.

---

## 1. Architecture

```
                  ┌─ HookEngine (state machine: Telex pipeline,
                  │   commit-undo, composition diff — UNCHANGED by T3)
                  │
                  │  member: std::shared_ptr<IOutputInjector> injector_
                  │      published via std::atomic_store on focus change,
                  │      read via std::atomic_load on hook thread (RCU)
                  │
   OnFocusChanged─┤   ─────────────────────────────────────────┐
   [focus thread] │     Phase 1 (no lock):                     │
                  │       auto c = Output::ClassifyWindow(hwnd);│
                  │     Phase 2 (atomic publish):              │
                  │       auto fresh = Output::Create(c);       │
                  │       std::atomic_store(&injector_, fresh); │
                  │   ─────────────────────────────────────────┘
                  │
                  ├─ ProcessKeyDown(...)  [hook thread]
                  │       auto inj = std::atomic_load(&injector_);
                  │       inj->Replace(bsCount, text);
                  │       inj->SendKey(vk);
                  │       cooldown = inj->SettleBudget();
                  │
                  └─ NEVER touches INPUT[], SendInput, SendMessage directly
                     (after T3 D4 cleanup)

  src/app/output/  ── NEW folder (CMake: if(WIN32) gated)
  ├── IOutputInjector.h           (header-only interface, Linux-friendly via fwd-decl)
  ├── OutputInjectorFactory.{h,cpp}  (Detect / Create + ClassifyWindow refactored in)
  ├── Internal.{h,cpp}            (TrackedSendInput + g_sendInput/g_sendMessageW/g_sleep test seam)
  ├── Win32SendInputInjector.{h,cpp}     [final]
  ├── RichEditEmReplaceSelInjector.{h,cpp} [final]
  └── SplitDispatchInjector.{h,cpp}      [final]

  tests/output/    ── new folder (Windows-only)
  ├── Win32SendInputInjectorTest.cpp        ── mocked SendInput
  ├── RichEditEmReplaceSelInjectorTest.cpp  ── mocked SendMessage
  ├── SplitDispatchInjectorTest.cpp         ── mocked SendInput + verify Sleep
  └── OutputInjectorFactoryTest.cpp         ── classify → impl mapping

  tools/audit/check_hook_thread_no_mutex.sh — UPDATE in T3 D4
   - remove from ATOMIC_BOOLS regex: useEditMsgPath_|isElectronApp_|isConsoleApp_|needBaitChar_
   - add Check 4: injector_ accessed only via std::atomic_load / std::atomic_store
```

**Threading invariants (from Sprint 1 / Rule #11.3):**

- Hot path reads `injector_` via `std::atomic_load(shared_ptr)` — lock-free, never blocks.
- Focus thread writes via `std::atomic_store(shared_ptr, fresh)` — single-writer, no lock needed for shared_ptr atomic.
- A hook thread holding a stale `shared_ptr` keeps the old impl alive until its current `Replace`/`SendKey` returns; the new impl takes effect on the next keystroke. This is the same RCU pattern as Sprint 1 D6 (`config_`).

---

## 2. Components

### 2.1 `IOutputInjector` (interface)

```cpp
namespace NextKey::Output {

class IOutputInjector {
public:
    virtual ~IOutputInjector() = default;

    // Replace caret region: delete bsCount chars, then insert text.
    // Returns true if delivered, false if caller should fall back to passthrough.
    [[nodiscard]] virtual bool Replace(size_t bsCount, std::wstring_view text) noexcept = 0;

    // Re-inject a single VK as if the user pressed it (down + up, marker).
    // Used by HookEngine::InjectKey for the synth-pending re-inject case.
    virtual void SendKey(WORD vkCode) noexcept = 0;

    // Time the synth pressure from this channel takes to drain.
    // Used by HookEngine commit-undo synth-guard. Default 100 ms (paranoid).
    // Each impl overrides to match its actual mechanism.
    [[nodiscard]] virtual std::chrono::milliseconds SettleBudget() const noexcept {
        return std::chrono::milliseconds{100};
    }
};

}  // namespace NextKey::Output
```

**Why these three methods (β+ shape from brainstorm Q2):**

- `Replace(bsCount, text)` covers ReplaceComposition / SendBackspaces / single-char emit (95 % of sites). Returning `bool` preserves today's `TryEditMessagePaste` failure-fallback semantics.
- `SendKey(vk)` covers `InjectKey` (line 905) — re-inject a single VK with `NEXUSKEY_EXTRA_INFO` marker after synthetic events.
- `SettleBudget()` is non-pure with a safe default. Future additive query methods (e.g., `IsRendererBased()`) follow the same pattern → adding a method does NOT break existing impls.

### 2.2 `Win32SendInputInjector` — default catch-all + Chromium variant

```cpp
class Win32SendInputInjector final : public IOutputInjector {
public:
    explicit Win32SendInputInjector(bool needsBaitCharPrefix) noexcept;

    bool Replace(size_t bsCount, std::wstring_view text) noexcept override;
    void SendKey(WORD vkCode) noexcept override;
    std::chrono::milliseconds SettleBudget() const noexcept override {
        return std::chrono::milliseconds{30};
    }

private:
    bool needsBaitCharPrefix_;  // Chromium autocomplete-dismiss quirk
};
```

- **Hosts**: Chrome omnibox, Notepad++, Chromium renderer textareas (Gmail / ChatGPT), generic Win32 Edit. ~85 % of cases.
- **Replace logic**: build INPUT[] in stack-local `std::array<INPUT, kMaxBatch>` (kMaxBatch = 256 — covers worst-case BS×128 + chars×128). Single batch `Internal::TrackedSendInput`. Bait-char prefix when `needsBaitCharPrefix_ && text.empty() && bsCount > 0` — replicates `HookEngine::SendBackspaces` line 3121 logic.
- **Returns false** when `TrackedSendInput` reports partial-send (renderer drop).
- **SettleBudget = 30 ms** — measured: batch SendInput drains within 20-25 ms on real hosts.

### 2.3 `RichEditEmReplaceSelInjector` — Win11 New Notepad RichEditD2DPT

```cpp
class RichEditEmReplaceSelInjector final : public IOutputInjector {
public:
    bool Replace(size_t bsCount, std::wstring_view text) noexcept override;
    void SendKey(WORD vkCode) noexcept override;
    std::chrono::milliseconds SettleBudget() const noexcept override {
        return std::chrono::milliseconds{0};
    }
};
```

- **Hosts**: Win11 New Notepad RichEditD2DPT only. ~5 % of cases. (Sprint 1 D12 verdict.)
- **Replace logic**: foreground HWND → `EM_GETSEL` → `EM_SETSEL` (BS range) → `EM_REPLACESEL`. Logic moves from `HookEngine::TryEditMessagePaste` line 1902.
- **SendKey**: uses `SendInput` (re-inject single VK is always physical channel — semantics of `InjectKey` at line 905 require physical events delivered AFTER any pending synth).
- **SettleBudget = 0 ms**: SendMessage is synchronous — by the time it returns the edit is applied, no settle gap needed for synth-guard.
- **Returns false** when HWND no longer valid OR SendMessage returns 0 OR control class != RichEdit.

> **Note (CODE_GOVERNANCE §2)**: A generic `WmCharInjector` was considered for legacy hosts that need character-level WM_CHAR delivery. It is **not in T3 scope** — no current host requires WM_CHAR; if one appears, add as a sibling impl.

### 2.4 `SplitDispatchInjector` — Electron + Console

```cpp
class SplitDispatchInjector final : public IOutputInjector {
public:
    explicit SplitDispatchInjector(int sleepMsBetweenBatches) noexcept;

    bool Replace(size_t bsCount, std::wstring_view text) noexcept override;
    void SendKey(WORD vkCode) noexcept override;
    std::chrono::milliseconds SettleBudget() const noexcept override {
        return std::chrono::milliseconds{100};
    }

private:
    int sleepMs_;
};
```

- **Hosts**: Discord, Slack, VSCode (Electron, sleepMs=6); CMD, PowerShell (Console, sleepMs=5). ~10 % of cases.
- **Why merge Electron + Console into one impl**: the only difference today is `electronApp ? 6 : 5` — 1 ms delta. Two impls 99 % identical = code duplication; one impl with `sleepMs_` constructor param reads cleanly.
- **Replace logic**: `TrackedSendInput(bsBuf)` → `Sleep(sleepMs_)` → `TrackedSendInput(charBuf)`. Logic moves from `HookEngine::DispatchSendInput` line 2001 + duplicate at line 3068.
- **bsCount=0** path: no Sleep, single `TrackedSendInput(charBuf)`.
- **text.empty()** path: no second SendInput, no Sleep.
- **SettleBudget = 100 ms**: covers split Sleep (5-6 ms) + Electron / Qt event-loop processing (~30-90 ms). Empirical from current hardcode `kSynthSettleMs=100`.
- **Returns false** when first or second `TrackedSendInput` partial.

### 2.5 `Internal::` — shared infrastructure

```cpp
// src/app/output/Internal.h
namespace NextKey::Output::Internal {

using SendInputFn    = UINT (WINAPI*)(UINT, LPINPUT, int);
using SendMessageWFn = LRESULT (WINAPI*)(HWND, UINT, WPARAM, LPARAM);
using SleepFn        = void (WINAPI*)(DWORD);

// Test seams — production wires these to ::SendInput etc at startup.
extern SendInputFn    g_sendInput;
extern SendMessageWFn g_sendMessageW;
extern SleepFn        g_sleep;

// Wrapper around g_sendInput with partial-send detection. Returns true iff
// all events delivered; false if renderer dropped some (renderer drop case).
[[nodiscard]] bool TrackedSendInput(INPUT* events, UINT count) noexcept;

// Marker dwExtraInfo so own synth events skip our own hook (Rule #11.4).
constexpr ULONG_PTR kNexusKeyExtraInfo = NEXUSKEY_EXTRA_INFO;

}  // namespace NextKey::Output::Internal
```

- All three impls call `Internal::TrackedSendInput` → partial-send logic single source of truth.
- Function-pointer indirection (chosen over interface-DI in brainstorm Q3) costs 1 register-indirect call ≈ 1 ns vs direct — immaterial vs syscall cost.

### 2.6 `OutputInjectorFactory` — classify + create

```cpp
namespace NextKey::Output {

// Phase 1 result — pure data, no shared-state writes. Producer only.
struct WindowClassification {
    bool isRichEditD2DPT = false;  // Win11 New Notepad
    bool isElectron      = false;  // Discord / Slack / VSCode + governance §2 detection
    bool isConsole       = false;  // CMD / PowerShell
    bool isChromium      = false;  // Chrome / Edge — bait-char hint only
};

// Phase 1 — no shared state writes (Rule #11.3 two-phase pattern).
[[nodiscard]] WindowClassification ClassifyWindow(HWND hwnd) noexcept;

// Phase 2 input — caller atomic_store-publishes the result.
[[nodiscard]] std::shared_ptr<IOutputInjector> Create(
    const WindowClassification& c) noexcept;

}  // namespace NextKey::Output
```

Decision tree inside `Create()`:

```
if c.isRichEditD2DPT  → make_shared<RichEditEmReplaceSelInjector>()
if c.isElectron       → make_shared<SplitDispatchInjector>(6)
if c.isConsole        → make_shared<SplitDispatchInjector>(5)
otherwise             → make_shared<Win32SendInputInjector>(c.isChromium)
```

**Priority order matters**: RichEdit > Electron > Console > default. Catches the case where a future host triggers multiple flags (e.g., a hypothetical RichEdit-inside-Electron host — RichEdit channel wins because Sprint 1 D12 proved it required).

**Factory invariants (per §4 error handling):**

- `Create()` is `noexcept` and **always returns a usable injector**. Default (catch-all) branch is the safest impl (batch SendInput).
- If `make_shared` throws OOM → terminates (catastrophic; we cannot run the IME without an injector).

---

## 3. Data flow

### 3.1 Hot path — single keystroke (hook thread, < 1 ms budget)

```
LowLevelKeyboardProc(VK)              [hook thread]
├─ early-returns (Rule #11.4):
│  ├─ NEXUSKEY_EXTRA_INFO  → CallNextHookEx
│  ├─ nCode < 0            → CallNextHookEx
│  ├─ sending_             → CallNextHookEx  (re-entrant guard)
│  └─ ctrl/alt/win modifier → CallNextHookEx
│
├─ ProcessKeyDown(vk):
│  │
│  ├─ State machine: Telex → commit-undo FSM → composition diff
│  │  (HookEngine concern — UNCHANGED by T3)
│  │
│  ├─ Synth-guard check (Sprint 2 D1 logic, NOW per-host):
│  │     auto inj = std::atomic_load(&injector_);             // ~10 ns
│  │     auto settleMs = inj->SettleBudget();                 // 0 / 30 / 100 ms
│  │     if (synthEventsPending_ > 0
│  │         && (now - lastRealSynthTime_) < settleMs.count()
│  │         && !isToneModifier) {
│  │         CancelCommitUndo();
│  │     }
│  │
│  ├─ Decision: replace / sendKey / passthrough
│  │
│  ├─ if replace(bsCount, newText):
│  │     bool ok = inj->Replace(bsCount, newText);            // ~1 ns + mechanism
│  │     if (!ok) → return false;  // caller fallback (passthrough)
│  │     synthEventsPending_++;                               // engine state
│  │     lastRealSynthTime_ = GetTickCount();                 // engine state
│  │
│  ├─ if sendKey(vk):
│  │     sending_ = true;
│  │     inj->SendKey(vk);
│  │     sending_ = false;
│  │     lastSynthSendTime_ = GetTickCount();
│  │
│  └─ return true (eaten) or false (passthrough)
│
└─ return result ? 1 : CallNextHookEx(...)
```

### 3.2 Focus change path (focus event thread, asynchronous)

```
WinEventProc / OnFocusChanged(HWND)         [focus event thread]
│
├─ Phase 1: CLASSIFY (no shared state writes — Rule #11.3)
│     auto c = Output::ClassifyWindow(focusedHwnd);
│     // Win32 calls, exe path lookup, process scan — may take 1-5 ms.
│     // No lock, no shared write.
│
├─ Phase 2: APPLY (atomic publish, < 0.1 ms)
│     auto fresh = Output::Create(c);                         // 1 heap alloc
│     std::atomic_store(&injector_, std::move(fresh));        // RCU swap
│     // hook thread reading old shared_ptr keeps refcount → safe drain
│
└─ Other focus-change concerns (vietnameseMode_, TSF flag, etc) — UNCHANGED
```

### 3.3 Worked example — chaos 5.3 on Discord (Electron split)

`vieejt nam BS×4 s` → `viết` (host = Electron, sleepMs=6, settle=100 ms).

```
1.  Open Discord, focus message input.
    OnFocusChanged: ClassifyWindow → c.isElectron=true.
    Create → SplitDispatchInjector(sleepMs=6).
    atomic_store(&injector_, fresh).

2.  User types 'v' [hook thread]
    atomic_load(&injector_) → SplitDispatchInjector*.
    Engine: composition "v" → Replace(0, "v").
    Split: SendInput([v]) — bsCount=0 path optimized to no Sleep.

3.  User types i, e, e, j, t.
    Each adjusts composition; engine emits Replace(prevLen, newComposition).
    'j' triggers tone: Replace(4, "việ") — split: SendInput(BS×4), Sleep(6), SendInput("việ").

4.  User types ' ' (space — commit trigger).
    Engine: commit "việt", commitUndoState_ = Ready.
    SendKey(VK_SPACE) — pass space through to app.

5.  User types 'n', 'a', 'm' similarly.

6.  User types BS BS BS BS.
    First BS: commitUndoState_ = Primed, Replace(1, "") via injector.
    SplitDispatch: SendInput(VK_BACK), no chars → no Sleep.

7.  User types 's' [hook thread]
    atomic_load(&injector_) → SplitDispatch.
    Engine: state Primed, vk='S', method=Telex → isToneModifier=TRUE.
    Sprint 2 D1: skip synth-guard cancel (tone modifier exempt).
    ReplayCommittedChars: replays "viet" backing buffer.
    Engine: 's' as sắc tone → composition "viết".
    Replace(4, "viết") → split: SendInput(BS×4), Sleep(6), SendInput("viết").

8.  Settle: settleMs=100 ms (Electron). Subsequent BS within 100 ms would be
    guarded; user is done, no further input → no guard triggered.
```

---

## 4. Error handling

### 4.1 `Replace()` returns `false` — fallback

Same semantics as today's `TryEditMessagePaste()`. Caller fallback chain per site:

| Site | On `Replace==false` |
|---|---|
| `commit-undo Primed BS` (today's HookEngine.cpp:913) | Pass BS through to app |
| `single char emit` (today's :1270) | Send via batch SendInput (degraded to default Win32 path) |
| `ReplaceComposition` (today's :2935 / :2965) | Engine resets composition, lets user retype |
| `SendBackspaces` (today's :3113) | Send VK_BACK via batch SendInput |

No new failure modes; T3 propagates the channel signal more cleanly than today's silent `TrackedSendInput` PARTIAL log.

### 4.2 Factory cannot fail

`Create()` is `noexcept` and always returns a usable injector. Default branch (catch-all) is `Win32SendInputInjector` — the safest mechanism. Any `make_shared` OOM → terminates.

### 4.3 Hot path never sees `nullptr injector_`

HookEngine ctor seeds `injector_` with `Output::Create(WindowClassification{})` → default `Win32SendInputInjector`. Hot path's `std::atomic_load` therefore never returns null. Defensive nullcheck unnecessary (and would slow hot path).

### 4.4 RCU safety during focus change

A hook thread reading an old `shared_ptr` at the moment of swap keeps the old impl alive (refcount > 0) until its `Replace`/`SendKey` returns. New keystrokes after the store see the fresh impl. Zero locks, zero waits — same pattern as Sprint 1 D6 `config_`.

### 4.5 Hook callback exception (Rule #11.5)

Existing top-level `try/catch` in `LowLevelKeyboardProc` covers any exception escaping injector impls. T3 does NOT change this. Each impl method `noexcept` to enforce: any throw inside Replace/SendKey is `std::terminate` — surfaces bugs immediately.

### 4.6 Audit script update

`tools/audit/check_hook_thread_no_mutex.sh` is updated as part of T3 D4:

- Remove from `ATOMIC_BOOLS` regex: `useEditMsgPath_|isElectronApp_|isConsoleApp_|needBaitChar_` (fields no longer exist).
- Add Check 4: grep that `injector_` access is only via `std::atomic_load` / `std::atomic_store` (no plain `injector_.method()` outside an atomic op).

---

## 5. Testing strategy

### 5.1 Layer 1 — unit tests (Windows GTest, mocked APIs)

- **Mock seam**: function pointers `Internal::g_sendInput / g_sendMessageW / g_sleep`. Production initializes to `::SendInput / ::SendMessageW / ::Sleep` at startup. Tests swap to capturing lambdas in `SetUp()`, restore in `TearDown()`.
- **Coverage matrix** (≈ 15 tests):

| Impl | Tests |
|---|---|
| `Win32SendInputInjector` | (a) Replace(empty text) → only BS×N events; (b) Replace(bs, chars) → BS×N + UNICODE chars in 1 batch; (c) bait-char prefix when `needsBaitCharPrefix && text.empty() && bs>0` injects `U+202F` + extra BS; (d) partial-send → return false; (e) SendKey emits down+up with `kNexusKeyExtraInfo` marker |
| `RichEditEmReplaceSelInjector` | (a) Replace → `EM_GETSEL` then `EM_SETSEL` then `EM_REPLACESEL` order; (b) bsCount=0 → no SETSEL; (c) `SendMessage` returning 0 → return false; (d) SendKey falls through to SendInput |
| `SplitDispatchInjector` | (a) Replace → SendInput(BS), Sleep(ms), SendInput(chars); (b) bsCount=0 → no Sleep; (c) text empty → no second SendInput; (d) sleepMs from constructor; (e) partial first SendInput → return false, second batch not sent |

Fast: < 1 s total, no GUI, deterministic.

### 5.2 Layer 2 — factory tests

`OutputInjectorFactoryTest.cpp` (5-7 tests):

| Test | Setup | Expected |
|---|---|---|
| `Default produces Win32` | `WindowClassification{}` | `dynamic_cast<Win32SendInputInjector*>` non-null, `needsBaitCharPrefix=false` |
| `RichEdit beats Electron` | `{isRichEditD2DPT=true, isElectron=true}` | RichEdit impl (priority order) |
| `Electron uses 6 ms` | `{isElectron=true}` | `SplitDispatchInjector*`, sleepMs=6 |
| `Console uses 5 ms` | `{isConsole=true}` | `SplitDispatchInjector*`, sleepMs=5 |
| `Chromium gets bait` | `{isChromium=true}` | `Win32SendInputInjector*`, `needsBaitCharPrefix=true` |

### 5.3 Layer 3 — `NextKeyTestRunner` extension (D6)

Add CLI flag `--host-class={win32|richedit|electron|console}`. Mechanism: runner sets process-wide env var `NEXUSKEY_FORCE_HOST_CLASS=win32`; HookEngine factory reads at `OnFocusChanged`, ignores classification, uses forced class.

**Cross-host matrix** (D6 deliverable):

|  | Notepad++ | Chrome omnibox | Discord | Notepad Win11 | ChatGPT |
|---|---|---|---|---|---|
| `win32` (default) | natural ✓ | natural ✓ | force | force | natural ✓ |
| `electron` (split) | force | force | natural ✓ | force | force |
| `richedit` (EM_REPLACESEL) | — | — | — | natural ✓ | — |

5 natural + 9 force = ~14 host×impl cells × 11 cases = ~150 case runs. ~2 hours manual orchestration.

**Acceptance gate D6:**

- All natural cells: 11/11 PASS, p99 ≤ baseline within 10 % noise.
- All force cells where mechanism is compatible: 11/11 PASS.
- p99 on Win32 / RichEdit hosts during commit-undo replay: ≥ 30 % better (D5 SettleBudget win).
- Functional regressions: 0.

### 5.4 Layer 4 — engine GTest (Linux, unchanged)

T3 does not touch engine logic. 1405 existing Linux tests must continue passing. Linux build excludes `src/app/output/` and `tests/output/` via `if(WIN32)` CMake gate. HookEngine.h uses forward declaration `namespace NextKey::Output { class IOutputInjector; }` so the `injector_` member compiles on Linux.

### 5.5 Acceptance gates per commit

| Gate | When | Criteria |
|---|---|---|
| Layer 1 unit | every Windows commit after D1 | new impl tests pass |
| Layer 2 factory | every commit after D0 | factory tests pass |
| Layer 4 engine | every commit | 1405 Linux GTests pass |
| D7 audit | every commit after D4 | `check_hook_thread_no_mutex.sh` PASS |
| D6 chaos sweep | end of T3 | criteria above |

---

## 6. Migration plan (D-day breakdown)

**Branch**: `sprint-2/output-injector` (created from Main when implementation starts).
**Cadence**: 1 commit per D-day, single feature branch, single PR at end. ~7 active days + 0-3 buffer.

### D0 — Scaffolding (½ day)

- New folder `src/app/output/` (CMake `if(WIN32)` gated).
- `IOutputInjector.h` (interface, header-only, Linux-friendly via fwd-decl in HookEngine.h).
- `OutputInjectorFactory.{h,cpp}` — stub: `Create()` always returns `Win32SendInputInjector` placeholder.
- `Internal.{h,cpp}` — function-pointer seams initialized to `::SendInput / ::SendMessageW / ::Sleep`.
- `Win32SendInputInjector.{h,cpp}` — empty stub returning `false` / no-op.
- `RichEditEmReplaceSelInjector.{h,cpp}` — empty stub.
- `SplitDispatchInjector.{h,cpp}` — empty stub.
- `tests/output/` folder + `OutputInjectorFactoryTest.cpp` (5 factory tests, work against stub).
- HookEngine: add `std::shared_ptr<IOutputInjector> injector_`, init in ctor via `Output::Create({})`. NOT yet called from anywhere.

**Gate**: Linux engine GTest 1405/1405 PASS. Windows compile clean.
**Commit**: `Sprint 2 D0: IOutputInjector scaffolding + factory stub + test seam`

### D1 — Win32SendInputInjector + integrate default path (1.5 days)

- Implement `Win32SendInputInjector::Replace` / `SendKey` with bait-char prefix logic.
- Move `TrackedSendInput` from `HookEngine.cpp` to `Internal::TrackedSendInput`.
- 5 unit tests `Win32SendInputInjectorTest.cpp`.
- HookEngine: route default-path sites through injector:
  - `SendBackspaces` (line 3103) → `injector_->Replace(count, L"")`.
  - `SendCharEvents` / `SendBackspaceEvents` callers go through injector.
  - `InjectKey` (line 3167) → `injector_->SendKey(vk)`.
- Engine still has `useEditMsgPath_` / `isElectronApp_` flags — they short-circuit BEFORE injector call (preserving today's behavior). D2/D3 removes them.

**Gate**: Notepad++ / Chrome chaos 11/11 PASS. Linux GTest unchanged. Layer 1 unit tests PASS.
**Commit**: `Sprint 2 D1: Win32SendInputInjector + integrate default Win32 path`

### D2 — RichEditEmReplaceSelInjector + integrate (1 day)

- Implement `RichEditEmReplaceSelInjector::Replace` (move from `TryEditMessagePaste` line 1902).
- 4 unit tests `RichEditEmReplaceSelInjectorTest.cpp`.
- Update `OutputInjectorFactory::Create()` to return RichEdit impl when `c.isRichEditD2DPT`.
- HookEngine: **remove** `useEditMsgPath_` flag and all 4 conditional branches (lines 913, 1270, 2935 / 2965, 3113). All sites uniformly call `injector_->Replace(...)`.
- Move `IsRichEditD2DPT` HWND-class detection into `OutputInjectorFactory::ClassifyWindow`.

**Gate**: Notepad Win11 chaos 11/11 PASS. Notepad++ / Chrome unchanged.
**Commit**: `Sprint 2 D2: RichEditEmReplaceSelInjector + remove useEditMsgPath_ branches`

### D3 — SplitDispatchInjector + integrate (1 day)

- Implement `SplitDispatchInjector::Replace` (move from `DispatchSendInput` line 2001 + duplicate at 3068).
- 5 unit tests `SplitDispatchInjectorTest.cpp`.
- Factory: `SplitDispatchInjector(6)` for Electron, `SplitDispatchInjector(5)` for Console.
- HookEngine: **remove** `isElectronApp_` / `isConsoleApp_` flags + their conditional branches. Delete entire `DispatchSendInput()` function. Delete duplicated split block at line 3068.
- Move ClassifyWindow Electron / Console detection into `OutputInjectorFactory::ClassifyWindow`.

**Gate**: Discord chaos 11/11 PASS. All other hosts unchanged.
**Commit**: `Sprint 2 D3: SplitDispatchInjector + remove isElectronApp_/isConsoleApp_ branches`

### D4 — Audit script update + dead code removal (½ day)

- Delete the four dead atomic fields from HookEngine.h (and their store sites in `OnFocusChanged`):
  - `useEditMsgPath_` (last reader removed in D2)
  - `isElectronApp_` (last reader removed in D3)
  - `isConsoleApp_` (last reader removed in D3)
  - `needBaitChar_` (last reader removed in D1 — bait-char logic moved into `Win32SendInputInjector`; the classify path now feeds `c.isChromium → Win32SendInputInjector(needsBaitCharPrefix=true)`)
- Delete dead helpers in HookEngine: `DispatchSendInput`, old `SendBackspaces` (the routing wrapper), the `TrackedSendInput` member (now in `Internal::`).
- Update `tools/audit/check_hook_thread_no_mutex.sh`:
  - Remove from `ATOMIC_BOOLS` regex: `useEditMsgPath_|isElectronApp_|isConsoleApp_|needBaitChar_` (fields no longer exist after the deletions above).
  - Add Check 4: grep that `injector_` access is only via `std::atomic_load` / `std::atomic_store` (no plain `injector_->method()` outside an atomic op).
- HookEngine.cpp line count: target reduction ~200-300 LOC (3593 → ~3300).

**Gate**: D7 audit PASS, all chaos hosts PASS, line-count drop measurable.
**Commit**: `Sprint 2 D4: audit script update + remove dead dispatch code`

### D5 — SettleBudget integration (½ day)

- HookEngine: replace `kSynthSettleMs = 100` constant at synth-guard check (line 955) with `injector_->SettleBudget().count()`.
- Capture commit-undo replay latency on Win32 (Notepad++) and RichEdit (Notepad Win11) — should improve materially because settle budget drops from 100 ms → 30 ms / 0 ms.
- Document delta in `docs/baselines/perf-baseline-t3-settle-budget.md`.

**Gate**: Chaos 5 hosts 11/11 PASS. Win32 / RichEdit settle-replay measurably faster (target: ≥ 30 % reduction in inter-keystroke delay during commit-undo replay).
**Commit**: `Sprint 2 D5: per-host SettleBudget — Win32 30ms, RichEdit 0ms, Electron 100ms`

### D6 — Cross-host chaos sweep + perf delta (1 day)

- Run §5.3 cross-host matrix: 5 natural × 11 + ~9 forced × 11 = ~150 cases.
- Capture report.xml + perf.csv per cell.
- Compare against Main baseline (current 55/55 PASS).
- Write summary in `docs/baselines/perf-baseline-t3-final.md` — verdict + p99 delta per cell.

**Acceptance**:
- Natural cells: 55/55 PASS, p99 ≤ baseline within 10 % noise.
- Forced compatible cells: 11/11 PASS.
- p99 on Win32 / RichEdit hosts during commit-undo replay: ≥ 30 % better.
- Functional regressions: 0.

**Gate**: All acceptance criteria met. PR ready for review.
**Commit**: `Sprint 2 D6: cross-host chaos sweep — verdict + perf delta capture`

### D7 — Buffer (0-2 days)

Reserved for issues uncovered in D6 sweep, PR review feedback rework, or unexpected integration bugs.

### Final PR

Single PR at end: `Sprint 2 T3: IOutputInjector — extract output channel + matrix harness`. ~10-12 commits on `sprint-2/output-injector`. Bisect-friendly: each D-commit leaves system functional with chaos passing.

PR description includes: linkback to this design doc; D-day breakdown summary; chaos delta table (before / after, 5+ hosts); LOC delta; D7 audit gate evidence.

---

## 7. Open questions / risks

| # | Risk | Mitigation |
|---|---|---|
| R1 | `--host-class` env var override might leak across processes if not scoped | Set in runner process before launching child; HookEngine reads on focus change, not at startup. Document in runner README. |
| R2 | RCU `shared_ptr` atomic operations require C++20 `std::atomic<std::shared_ptr>` or `std::atomic_load(shared_ptr*)`. MSVC support depends on standard level. | Verify MSVC 2022 + `/std:c++20` support. Sprint 1 D6 already uses this for `config_` → precedent confirms it works. |
| R3 | `kMaxBatch=256` stack array might be too small for very long composition replacements (e.g., macro expansion) | Engine composition is bounded by language: max 8 chars per Vietnamese word + tone. Macro expansion goes through a separate path (clipboard worker). 256 is 16× margin. |
| R4 | Bait-char prefix only applies to BS-only flow today; T3 must preserve this exact gate (`text.empty() && bsCount > 0`) | Unit test (c) in §5.1 explicitly asserts the gate. Regression-protected. |
| R5 | Sprint 2 D1 tone-modifier exemption is policy in HookEngine; if it ever needs per-host tuning, interface needs `IsRendererBased()` query | Defer until evidence; β+ interface allows additive non-pure-virtual extension without breaking impls. |

---

## 8. Acceptance — definition of done for T3

- [ ] All 6 D-commits land on `sprint-2/output-injector` branch.
- [ ] Final PR opened, description complete, all gates green.
- [ ] HookEngine.cpp ≤ 3300 LOC (started at 3593).
- [ ] Zero plain references to `useEditMsgPath_`, `isElectronApp_`, `isConsoleApp_`, `needBaitChar_` in `src/`.
- [ ] D7 audit script PASS, including new Check 4.
- [ ] Layer 1 + Layer 2 GTests added: ≥ 15 unit + ≥ 5 factory.
- [ ] Layer 4 GTests on Linux: 1405/1405 (no regression).
- [ ] D6 cross-host sweep: natural 55/55 PASS, force-compatible cells PASS.
- [ ] Commit-undo replay p99 on Win32 / RichEdit hosts: ≥ 30 % reduction.
- [ ] Design doc (this file) referenced in final PR description.

---

*This design doc satisfies the brainstorming-skill terminal state. Implementation plan will be created via the `writing-plans` skill in a follow-up step.*
