# Architecture / Performance Roadmap — Post-W7

**Date:** 2026-05-23
**Branch:** `feat/architecture-review-v3.1` (HEAD `58ffbf0`)
**Status:** Design — pre-implementation
**Author of source assessment:** em (junior dev)
**Reviewer:** claude (this doc)

## Context

W7 feature pipeline framework đã đóng (engine layer: TypingEngine + 4 rules carved). 60 commits pushed to `feat/architecture-review-v3.1`. 2010/2010 GTest pass, chaos 44/44 × 5 verification runs.

Em chạy round assessment kế tiếp, focus vào **hot path latency + invariants** thay vì engine perf. Kết luận em: TypingEngine không phải bottleneck (Telex single PushChar p95 ~546ns / p99 ~654ns lokal benchmark) — rủi ro nằm ở các đường phụ vẫn chạm `WH_KEYBOARD_LL`.

Doc này verify findings của em + present 3-wave roadmap để giải quyết theo blast-radius order.

## Verified findings (from em's assessment)

| Finding | Path/line trong assessment | Verified path/line | Verdict | Tier |
|---|---|---|---|---|
| Hotkey LL hook + lock in callback | `HotkeyManager.cpp:68,160` | `src/app/system/HotkeyManager.cpp:69,160` (path drift) | Đúng | **S** |
| Hotkey → `hookEngine.CommitPending()` cross-thread | `HotkeyWiring.cpp:46` | `src/app/system/HotkeyWiring.cpp:47` | Đúng | **S** |
| `CommitPending` locks `stateMutex_` | `HookEngine.cpp:156` | Same | Đúng | **S** |
| QuickSync fast/slow path | `:584` / `:611` | Same | Đúng | **A** |
| Worker reload locks stateMutex_ | `:3124` | Line 3126 (2-line drift post-W7) | Đúng | **A** |
| 7-TOMLs parse cold-cache 35-100ms | `TODO.md:1074` | Section khớp | Đúng | **A** |
| HookEngine god object 4k+ | — | 4588 LOC | Đúng | **B** |
| RichEdit + clipboard fallback Sleep | `:3778, :3831` | Line ~3800 (~20-line drift) | Đúng nhưng intentional | **C** |
| TSF/hook path trùng logic | `EngineController.cpp:70` | Line 70 destructor — drift; substance ở chỗ khác trong file | Đúng nhưng overstated (TSF apps không qua hook) | **C** |

**Path note:** Em viết `src/app/HookEngine.cpp` — đường thực là `src/app/system/HookEngine.cpp`. Line numbers drift ±2-20 dòng do 4 cleanup commits cuối W7 (`cfdc06d`, `64e71c0`, `58ffbf0`, etc.) — substance không sai.

**Tier explained:**
- **S** = invariant violation + every-keystroke blast radius — fix first
- **A** = p99 spike during specific user actions — fix second
- **B** = risk reducer long-term, không perf — fix third
- **C** = intentional tradeoff hoặc overstated — defer indefinitely

## Roadmap overview

```
Wave 1: Hotkey LL hook cross-thread fix     ~1-2 ngày    Tier S
Wave 2: Config reload RCU + lazy parse      ~3-4 ngày    Tier A
Wave 3: HookEngine decomp (5 PRs)           ~1-2 tuần    Tier B
```

Mỗi wave atomic (per anh's "đừng refactor tới lui" rule). Wave 1 + 2 độc lập, có thể song song nếu 2 người cùng làm. Wave 3 needs Wave 1 done trước khi PR 3.4 (CommitState).

---

## Wave 1 — Hotkey LL hook cross-thread fix

### Problem

`HotkeyManager` cài LL hook **thứ hai** (`SetWindowsHookExW(WH_KEYBOARD_LL, ...)` ở line 69). Callback (line 160) lock `slotsMutex_` mỗi keystroke hệ thống.

Tệ hơn: khi hotkey match, callback gọi thẳng `hookEngine.CommitPending()` qua `HotkeyWiring.cpp:47`. `CommitPending` ở `HookEngine.cpp:156-157` lock `stateMutex_`.

**Hai vấn đề:**
1. **Latency** — LL hook callback chặn key delivery cho mọi app trên máy; thêm mutex acquire mỗi keystroke
2. **Invariant violation** — design contract là "hook-thread-only writes to engine state" (xem comments `HookEngine.cpp:891-955`). Hotkey callback chạy trên thread riêng của hotkey LL hook → call `CommitPending` từ thread đó phá invariant

### Design options considered

| Option | Approach | Pros | Cons |
|---|---|---|---|
| A. Merge vào hook #1 | Drop LL #2, đăng ký slots vào HookEngine's LL hook | Latency thấp nhất | Touch HookEngine hot path, risk cao, refactor sâu |
| B. `RegisterHotKey()` Win32 | Replace LL hook với Win32 RegisterHotKey | Cleanest contract | Conflict với app khác system-wide; Shift+Space khó register |
| **C. RCU slots + post message** | LL #2 stays. Slots = `shared_ptr<vector<Slot>>` lock-free read. Match → `PostThreadMessage` to hook thread → hook thread calls `CommitPending` trong context của nó | Atomic, không touch HookEngine hot path, fix cả invariant + mutex | LL #2 vẫn còn (~100ns/key overhead nhưng acceptable) |

**Chosen: C.**

### Implementation sketch

```cpp
// HotkeyManager.h
class HotkeyManager {
    std::atomic<std::shared_ptr<const std::vector<Slot>>> slots_;
    DWORD hookThreadId_;  // set on Install()

    LRESULT CALLBACK LowLevelKeyboardProc(...) {
        auto slots = slots_.load();  // RCU read, lock-free ~5ns
        for (const auto& slot : *slots) {
            if (Matches(slot, vkCode, mods)) {
                // Post to hook thread, do NOT call CommitPending here
                PostThreadMessageW(hookThreadId_, WM_HOTKEY_FIRED, slot.id, 0);
                return 1;  // eat key
            }
        }
        return CallNextHookEx(...);
    }

    void Register(Slot s) {
        std::lock_guard lk(mutationMutex_);  // ONLY for writers
        auto newSlots = std::make_shared<std::vector<Slot>>(*slots_.load());
        newSlots->push_back(s);
        slots_.store(newSlots);  // publish
    }
};
```

Hook thread message loop xử lý `WM_HOTKEY_FIRED` → dispatches to per-slot callback (which can safely call `CommitPending` because we're on hook thread now).

### Success criteria

1. `check_hook_thread_no_mutex.sh` pass clean — fix false positive atomic<bool> warning trong cùng PR
2. Hotkey LL callback acquires **zero mutex** in steady state (only mutators lock)
3. `CommitPending` callable only from hook thread — add `assert(GetCurrentThreadId() == hookThreadId_)` guard
4. Functional test: Shift+Space toggle while typing rapidly (1ms inter-key) — no regression
5. 2010 GTest + chaos 44/44 unchanged

### Risk

User gõ Shift+Space giữa lúc NexusKey có pending commit → race giữa key delivery và `PostThreadMessage` arrival. Cần functional test cover combo này. Worst case: 1 keystroke delay (~10ms) before commit fires — acceptable, không lost state.

### Estimated PR size

~150 LOC changed across 3 files (HotkeyManager.h/.cpp, HotkeyWiring.cpp). 1-2 ngày bao gồm test.

---

## Wave 2 — Config reload RCU publish + lazy parse

### Problem

**Two slow paths đụng hot path:**

1. `QuickSyncFromSharedState` (line 611): khi `configGeneration` đổi → grab `stateMutex_` → rebuild snapshot → blocks 1 keystroke
2. `ReloadFromToml` worker (line 3126): 7 `toml::parse_file` calls (cold-cache 35-100ms per TODO #1074) → grab `stateMutex_` → blocks hook thread nếu hook đang sync cùng lúc

**Hiện tại:** fast path tốt (atomic generation compare, ~5ns). Nhưng slow path không amortize — user vừa đổi setting vừa gõ = visible p99 spike.

### Design — Full RCU publish, ZERO mutex on hot path

```
Worker thread (background)                  Hook thread (hot path)
─────────────────────────                  ──────────────────────
1. Detect file change                       QuickSync slow path:
2. ParseAllChanged() → ConfigBundle           ptr = atomic_load(configPtr_);
   ├─ hash-compare each of 7 files          if (ptr->gen > localGen_):
   ├─ skip unchanged (lazy parse)             localConfig_ = *ptr;  // shallow copy
   └─ re-parse only changed                   localGen_ = ptr->gen;
3. Build dependent maps                     // ZERO mutex, zero TOML parse
4. atomic_store(configPtr_, newBundle)
```

**Key components:**

- `ConfigBundle` struct: holds parsed result of all 7 TOMLs (TypingConfig + macros + appOverrides + excludedApps + tsfApps + convertConfig + customKeyMap) + generation
- `configPtr_` = `std::atomic<std::shared_ptr<const ConfigBundle>>`
- Worker owns ALL parsing + dependent map building
- Hook thread only does atomic load + shallow copy when gen changes (~50ns)
- `stateMutex_` removed from QuickSync slow path entirely
- `ReloadFromToml` no longer touches `stateMutex_` — pure publish

### Lazy parse optimization (bundled per anh's decision)

```cpp
struct FileSig { std::filesystem::file_time_type mtime; uint64_t hash; };

ConfigBundle ParseAllChanged(const ConfigBundle& prev) {
    ConfigBundle next = prev;  // copy-on-write
    for (auto& [path, slot] : kConfigFiles) {
        auto sig = ReadSig(path);
        if (sig == prev.sigs[slot]) continue;  // unchanged, skip parse
        next.fields[slot] = ParseFile(path);
        next.sigs[slot] = sig;
    }
    next.generation = prev.generation + 1;
    return next;
}
```

**Effect:** user thay đổi 1 file → re-parse 1 file (~5-15ms) thay vì 7 (35-100ms). Cold-cache only on first load; subsequent reloads incremental.

### Success criteria

1. **Bench:** type 1000 keys WHILE worker reloads config — p99 within H6 baseline (no spike > 20ms over baseline)
2. `stateMutex_` no longer acquired inside any config-sync path (audit script + grep verify)
3. Worker reload doesn't block hook thread — instrumentation: max wait time hook thread = 0
4. Chaos 44/44 + new `reload_burst` scenario added
5. TODO #1074 closed
6. **PerfHistogramTest** 4 failing flush-log cases fixed BEFORE Wave 2 success measurement (blocker)

### Risk

`std::atomic<std::shared_ptr<T>>` C++20 — verify ABI/codegen trên MSVC2022 Win10 + Win11 + Linux GTest path. Fallback: `boost::atomic_shared_ptr` hoặc DIY hazard pointer if MSVC implementation suboptimal.

Lazy parse hash mismatch could miss legitimate change → file_time_type + content hash both required (belt + suspenders).

### Estimated PR size

~400-500 LOC changed: HookEngine config sync block, ConfigSyncer extraction (precursor to Wave 3 PR 3.0), worker reload, snapshot apply. 3-4 ngày bao gồm test + bench.

---

## Wave 3 — HookEngine decomp

### Problem

`HookEngine.cpp` = **4588 LOC**, gom 6 concerns:

| Concern | Approx LOC | State owned |
|---|---|---|
| Hook lifecycle (Install, ThreadProc, mailbox) | ~600 | hookHandle_, mailbox_, threadId_ |
| Focus tracking (PID, layout, foreground watch) | ~800 | lastForegroundPid_, layoutCache_, focusCache_ |
| Config sync (QuickSync, ReloadFromToml) | ~700 | configPtr_, typingConfig_ |
| Pipeline executor (PushChar wiring, features) | ~900 | features_, pipeline_, coordinator_ |
| Output dispatch (injectors, RichEdit, clipboard) | ~1000 | injectors_, lastDispatchHost_ |
| Telemetry / commit state | ~600 | stateMutex_, pendingCommit_, undoBuffer_ |

W7 feature pipeline đã proof-of-concept pattern ở engine layer. Apply same pattern to HookEngine:

### End shape

```
HookEngine (~500 LOC thin coordinator)
├─ HookLifecycle      — hook handle + thread + mailbox
├─ FocusOwner         — focus state + layout cache + foreground watch
├─ ConfigSyncer       — configPtr_ + RCU publish (Wave 2's result)
├─ PipelineExecutor   — features_ + coordinator (W7's result, mostly extracted)
├─ OutputDispatcher   — injectors + RichEdit/clipboard/Electron paths
└─ CommitState        — stateMutex_ + pendingCommit + undoBuffer
```

### Sequencing — 5 atomic PRs

Per anh's "đừng scaffold→activate" rule: each PR extracts ONE owner completely (end-shape), not interface-then-impl chain.

#### PR 3.1 — HookLifecycle
- **Scope:** Install/Uninstall, ThreadProc, HookCommandMailbox, thread ID
- **Difficulty:** Easiest, fewest deps
- **Risk:** Low — purely lifecycle, no business logic
- **LOC delta:** ~600 carved out, HookEngine → ~4000

#### PR 3.2 — FocusOwner
- **Scope:** OnFocusChanged, ApplyFocusOnHookThread, layout cache, foreground watch
- **Difficulty:** Medium, depends on HookLifecycle (thread ID, mailbox)
- **Risk:** Medium — recent regression area (focus poll reset loop, project_focus_poll_reset_loop_2026-05-21)
- **LOC delta:** ~800 carved, HookEngine → ~3200
- **Bonus:** isolate focus logic = easier to guard against future regressions

#### PR 3.3 — OutputDispatcher
- **Scope:** DispatchSendInput, DispatchEmReplaceSel, RichEdit retry, clipboard fallback, Electron path
- **Difficulty:** Medium, mostly isolated (injectors already separate files)
- **Risk:** Medium — host-compat tradeoffs (Sleep, retry) must preserve exact behavior
- **LOC delta:** ~1000 carved, HookEngine → ~2200

#### PR 3.4 — CommitState
- **Scope:** stateMutex_, CommitPending, pendingCommit, undoBuffer
- **Difficulty:** Hardest, central state
- **Risk:** High — invariant violation area (Wave 1 fixes the cross-thread call but extraction surface still risky)
- **Prereq:** Wave 1 must be done (so CommitPending callers are all hook-thread)
- **LOC delta:** ~600 carved, HookEngine → ~1600

#### PR 3.5 — Cleanup → thin coordinator
- **Scope:** Shrink HookEngine to ~500 LOC, delete dead refs, finalize interfaces
- **Difficulty:** Low (mechanical cleanup)
- **Risk:** Low
- **LOC delta:** HookEngine → ~500

### Success criteria per PR

1. Owner has clear interface, no public access to internal state
2. HookEngine LOC decreases monotonically each PR (no oscillation)
3. 2010 GTest + chaos 44/44 unchanged per PR
4. Zero behavior change (refactor only — feature work blocked during Wave 3)
5. PerfHistogramTest p99 within 10% of pre-PR baseline (no perf regression)

### Risk

5 PRs over 1-2 weeks. Each PR ship-able alone — if anh muốn dừng giữa chừng (e.g., chỉ 3.1 + 3.2 + 3.5), HookEngine vẫn coherent state.

PR 3.4 highest regression risk. Plan: extract on dedicated branch with extra chaos rounds (44/44 × 3) before merge.

### Estimated total

~3500 LOC re-organized (no net growth). 1-2 tuần aggregate. Each PR 1-3 ngày.

---

## Cross-cutting items

### CI / instrumentation prereqs

| Item | Status | Blocks |
|---|---|---|
| `check_hook_thread_no_mutex.sh` false positive (atomic<bool>) | Failing | Wave 1 success measurement |
| `PerfHistogramTest` 4 flush log cases | Failing | Wave 2 + Wave 3 p99 measurement |
| `reload_burst` chaos scenario | Not yet added | Wave 2 success criteria |

**Wave 1 PR carries CI fix #1.**
**Wave 2 must have PerfHistogramTest green before merge** — otherwise p99 measurements untrusted.

### Out of scope

Intentional non-goals — do NOT bundle into waves:

- **TSF/hook drift consolidation** — Tier C. TSF apps don't go through hook (TSF_ACTIVE flag tách lane), so "drift" is maintenance risk not perf risk. Defer indefinitely.
- **RichEdit + clipboard fallback Sleep** — Tier C. Intentional host-compat tradeoff. Touching breaks RichEdit/Qt/VB6 compat.
- **Output injector consolidation (Sprint 2 T3 IOutputInjector)** — Paused 2026-05-05, remains paused. Wave 3 PR 3.3 will NOT resurrect it; just carves dispatch logic into owner.
- **REFACTOR_STATUS.md update** — Deferred until `feat/architecture-review-v3.1` merges to Main.

---

## Timeline

```
2026-05-23 → 2026-05-24    Wave 1 (Hotkey RCU + post)
2026-05-25 → 2026-05-28    Wave 2 (Config RCU + lazy parse + CI fixes)
2026-05-29 → 2026-06-10    Wave 3 PR 3.1 → PR 3.5
```

Realistic with solo dev pace. Anh may interleave with other priorities (RELEASE_NOTES, GUIDE.md WIP). Each wave standalone ship-able — can pause between waves without leaving broken intermediate state.

## Decision required before starting

1. Wave 1 design Option C confirmed (RCU slots + post message)
2. Wave 2 scope: RCU publish + lazy parse bundled (per anh's decision 2026-05-23)
3. Wave 3 full 5 PRs (per anh's decision 2026-05-23)

## References

- Source assessment: em's report (in-conversation 2026-05-23)
- W7 retro + ADRs: `docs/plans/2026-05-23-feature-pipeline-w7-retro.md`
- HookEngine recent regressions: `memory/project_focus_poll_reset_loop_2026-05-21.md`, `memory/project_hdldd_abbreviation_2026-05-21.md`
- TODO #1074: `docs/TODO.md:1070-1090`
- Invariant docs: `HookEngine.cpp:891-955`, `:2739`, `:4051`
- Atomic shared_ptr (C++20): https://en.cppreference.com/w/cpp/atomic/atomic_shared_ptr
