# Hook Self-Healer + Watchdog Process — Design

- **Date:** 2026-05-08
- **Status:** Approved — ready for implementation planning
- **Target release:** v3.0.0 (bundled with other v3 milestone work)
- **Source:** Brainstorm session 2026-05-08, scoped down from CODE_GOVERNANCE §3 (SPSC + watchdog)
- **Memory pre-read:** [`project_h6_premise_questioned.md`](../../../.claude-work/projects/-home-phatmt-code-NexusKey/memory/project_h6_premise_questioned.md)

## 1. Context

CODE_GOVERNANCE §3 prescribes a "minimal hook + watchdog process" with SPSC ring buffer + side-channel context. Brainstorm uncovered:

- **TSF/Hook không capture parallel hiện tại** (TSF_ACTIVE mutex flag) → SPSC ring premise stale, defer
- **3 user-pain modes triaged**:
  - Mode 1 (long-uptime drop, NexusKey 2.1.18 user) — kill+restart NexusKey không fix, phải reboot Win → state corrupt ngoài userland → **dropped from scope**, telemetry path also dropped (anh quyết định 2026-05-08: user moved to other app, không đáng đầu tư)
  - Mode 2 (hook hijack, Issue #99 Dorion) — Anti-Dorion code đã general (Raw Input dual-channel + reinstall) nhưng tightly coupled với HookEngine
  - Mode 3 (crash, Issue #103) — process chết → no auto-restart → user phải tự re-launch
- **CODING_RULES audit của Anti-Dorion** (Rule 4 interface, Rule 9 naming, Rule 3 reinstall error path, Rule 11.5 exception safety in WndProc) phát hiện 4 vi phạm fixable

## 2. Scope

### In scope

- **Phase 1**: Extract `HookSelfHealer` module từ `HookEngine` (Mode 2 fix + 4 CODING_RULES violations)
- **Phase 2**: Build `NexusKeyWatchdog.exe` process supervisor (Mode 3 fix)

### Out of scope

- Mode 1 long-uptime — separate brainstorm sau, không phải watchdog problem
- Telemetry build cho any mode — anh quyết định không đầu tư
- SPSC ring buffer + side-channel context — premise stale, revisit khi TSF Phase 2 enabled
- Hook hijack defense bổ sung beyond Anti-Dorion — không có evidence beyond Issue #99
- Quick Convert HOA→Title Case bug (`project_quick_convert_case_bug.md`) — bug riêng, track separately
- H8 deferred items (`WaitOnAddress`, ETW, foreground detection)

## 3. Phase 1 — HookSelfHealer Extraction

### Goal
Tách Anti-Dorion logic ra module riêng, sửa 4 vi phạm CODING_RULES, giữ behavior chính xác như hiện tại.

### Architecture

**New files:**
- `src/app/system/HookSelfHealer.h` — interface `IHookSelfHealer` + concrete `RawInputSelfHealer`
- `src/app/system/HookSelfHealer.cpp` — implementation
- `tests/HookSelfHealerTest.cpp` — Linux-runnable test (mock hook reinstall callback)

**Interface:**
```cpp
namespace NextKey {

class IHookSelfHealer {
public:
    virtual ~IHookSelfHealer() = default;
    virtual bool Start() = 0;                  // Create Raw Input window + register devices
    virtual void Stop() = 0;                   // Tear down
    virtual void RecordHookFire() = 0;         // Called from LowLevelKeyboardProc
};

// Reinstall callback — invoked when healer detects hook death
using ReinstallHookFn = std::function<bool()>;  // returns true on success

class RawInputSelfHealer : public IHookSelfHealer {
public:
    RawInputSelfHealer(HINSTANCE hInst, ReinstallHookFn reinstaller);
    // ... Start / Stop / RecordHookFire
};

}
```

**Wiring in HookEngine:**
```cpp
// Constructor: inject self-healer
HookEngine::HookEngine()
    : selfHealer_(std::make_unique<RawInputSelfHealer>(
          cachedHInstance_,
          [this]() { return ReinstallKeyboardAndMouseHooks(); }
      )) {}

// In LowLevelKeyboardProc, line 670-672 becomes:
if (self) self->selfHealer_->RecordHookFire();
```

`HookEngine::ReinstallKeyboardAndMouseHooks()` — new private method extracting the unhook/SetWindowsHookExW pair từ `RawInputWndProc:3492-3501`. Returns false on `SetWindowsHookExW` failure (fix Rule 3 violation).

### CODING_RULES fixes bundled

1. **Rule 4 (interface)** — Solved by extraction
2. **Rule 9 (constants UPPER_SNAKE)** — Rename inside HookSelfHealer.cpp:
   - `kSelfHealMissThreshold` → `SELF_HEAL_MISS_THRESHOLD`
   - `kSelfHealCooldownMs` → `SELF_HEAL_COOLDOWN_MS`
   - `kSelfHealHookFreshnessMs` → `SELF_HEAL_HOOK_FRESHNESS_MS`
   - `kSelfHealTimerId` → `SELF_HEAL_TIMER_ID`
3. **Rule 3 (reinstall error path)** — `ReinstallKeyboardAndMouseHooks()` checks `SetWindowsHookExW` return, log `GetLastError`, escalate (set internal flag, skip cooldown reset on failure to avoid infinite-cooldown loop)
4. **Rule 11.5 (exception safety in WndProc)** — Top-level try/catch in `RawInputWndProc` mirroring LL hook pattern
5. **Comment fix** — `HookEngine.cpp:3532` "< 0.5μs total" → realistic "~5-20μs total (well within 1ms hot-path budget)"

### Tests

- **HookSelfHealerTest.MissThresholdTriggersReinstall** — RecordHookFire() then 3 simulated WM_INPUT without RecordHookFire → verify reinstaller invoked
- **HookSelfHealerTest.CooldownPreventsReentry** — trigger, then 2nd miss-streak within 10s → verify reinstaller NOT invoked twice
- **HookSelfHealerTest.ReinstallFailureDoesNotResetCooldown** — mock reinstaller returning false → verify state allows future retry without infinite-cooldown loop
- Existing 1555/1555 GTest must remain PASS

### Effort
~1 day, single PR.

## 4. Phase 2 — NexusKeyWatchdog.exe

### Goal
Khi NexusKey crash (Issue #103) → external supervisor respawn within seconds, user không phải tự launch lại.

### Architecture

**New files:**
- `src/watchdog/main.cpp` — entry point (~150 LOC)
- `src/watchdog/CMakeLists.txt` — separate target `NexusKeyWatchdog`
- `src/app/system/HeartbeatPublisher.{h,cpp}` — NexusKey-side: signals named event every 30s
- Update `installer/` (or wherever Task Scheduler XML lives) — register watchdog at-logon task with `/it` flag

**Heartbeat protocol:**
- Named event: `Local\NexusKeyHeartbeat` (Windows kernel object, per-session)
- Named flag: `Local\NexusKeyGracefulShutdown` (set khi user quit qua tray)
- NexusKey: thread spawned từ `MainThreadWorker`, `SetEvent` mỗi 30s
- Watchdog: `WaitForSingleObject(heartbeat, 90s)` — 3× missed = pulse stale

**Respawn logic (watchdog side):**
```
loop:
  WaitForSingleObject(heartbeat, 90_000)
  if (signaled) continue;                                    // alive
  if (gracefulShutdown.IsSet()) { exit watchdog; }           // user quit
  if (NexusKey process still in process list) continue;     // alive but slow heartbeat — don't fight
  CreateProcess(NexusKey.exe);                               // crashed → respawn
  Sleep(2_000);                                              // grace period before next check
```

**Race protection:**
- Watchdog never SIGKILL/TerminateProcess NexusKey — chỉ respawn nếu **không thấy process trong list** AND heartbeat stale AND graceful flag clear
- 3 conditions tránh false-positive: NexusKey hung but alive (UI freeze) → watchdog không double-spawn
- Single-instance guard trong NexusKey.exe (existing): nếu watchdog accidentally double-spawn, second instance exits immediately

**Launch path:**
- Task Scheduler at-logon, `/it` flag (interactive token, per-user, no UAC prompt)
- Tận dụng existing `src/app/system/StartupHelper.h` pattern (NexusKey already uses Task Scheduler for `run_at_startup`)
- Watchdog task name: `NexusKeyWatchdog` (separate from NexusKey startup task)
- Installer registers BOTH tasks; uninstaller removes BOTH

### AV / signing strategy

- Watchdog binary signed cùng cert với NexusKey.exe (existing code-signing infra in `UpdateSecurity.cpp`)
- Avoids "self-restart" heuristic flag: watchdog không spawn FROM NexusKey process tree → looks like normal scheduled task respawning child app, not malware self-restart pattern
- AV check: validate against Windows Defender + 1 third-party (suggest test on Avast or Bitdefender) before ship

### Tests

- **Manual smoke test (Windows only)**:
  1. Launch NexusKey + watchdog
  2. `taskkill /F /IM NextKeyApp.exe` — simulate crash
  3. Within 90-120s, NexusKey should be alive again
  4. Verify no double-instance (single-instance guard works)
- **Manual graceful-shutdown test**:
  1. Quit NexusKey qua tray
  2. Watchdog should observe `gracefulShutdown` flag and exit, no respawn
- **Sleep/wake cycle test**:
  1. Force suspend (`rundll32 powrprof.dll,SetSuspendState 0,1,0`)
  2. Wake → both NexusKey and watchdog still operational, heartbeat resumes

### Effort
~3-5 days. Separate PR after Phase 1 lands.

## 5. Sequencing

Phase 1 + Phase 2 are **technically independent** (different code paths, separate exe target). Sequencing rule:

1. **Phase 1 first** — review focus stays small; HookEngine refactor lands cleanly without watchdog noise in diff
2. **Phase 2 second** — separate PR. Watchdog binary does not import HookSelfHealer; the two layers are belt + suspenders against orthogonal failure modes (in-process hijack vs out-of-process crash)

## 6. Acceptance Criteria

### Phase 1
- [ ] `IHookSelfHealer` + `RawInputSelfHealer` extracted, HookEngine uses interface
- [ ] 4 CODING_RULES violations fixed (interface, naming, reinstall error, exception safety)
- [ ] 3 new HookSelfHealer tests added, 1555+ GTest still PASS Linux
- [ ] Manual smoke on Windows: Anti-Dorion still works in Dorion (Issue #99 reproduction)

### Phase 2
- [ ] `NexusKeyWatchdog.exe` builds via separate CMake target
- [ ] Heartbeat protocol works: NexusKey publishes, watchdog reads
- [ ] Crash respawn manual test passes (taskkill → respawn within 120s)
- [ ] Graceful shutdown manual test passes (tray quit → no respawn)
- [ ] AV scan clean on Windows Defender + 1 third-party AV
- [ ] Installer registers/unregisters both tasks correctly

## 7. Risks + Open Questions

| Risk | Mitigation |
|---|---|
| Watchdog itself crashes | Task Scheduler "restart task on failure" attribute (Win10+ supports). 3 retries, 1-min interval. |
| Heartbeat thread starves under load | 30s interval is generous (90s timeout = 3× margin). MainThreadWorker not on hot path. |
| Sleep/hibernate clears named event | Test cycle (acceptance test). If event killed by OS, watchdog re-creates on next loop. |
| Single-cert sign breakage | Watchdog uses same cert/timestamp as NexusKey.exe — already deployed pipeline. |

**Decisions made (locked):**
- Heartbeat interval: **30s** (publisher) / **90s timeout** (watchdog). Trade-off accepted: detection latency up to 90s vs ~3 wakeups/min on idle.
- AV scan target: **Windows Defender** (mandatory) + **Bitdefender** (representative third-party with high false-positive rate on unsigned background processes).

**Additional decisions (locked 2026-05-08 user review):**
- Watchdog Task Scheduler path: **`\NexusKey\Watchdog`** (user-root, visible in Task Scheduler MMC for user debug/disable). NexusKey is a user app, không pollute `\Microsoft\Windows\` namespace.
- Release packaging: **bundle Phase 1 + Phase 2 vào v3.0.0** (major milestone). Both phases ship together, no intermediate 2.1.x patch release.

## 8. File Touch List

### Phase 1
- NEW: `src/app/system/HookSelfHealer.h`
- NEW: `src/app/system/HookSelfHealer.cpp`
- NEW: `tests/HookSelfHealerTest.cpp`
- EDIT: `src/app/system/HookEngine.h` — remove 5 fields + 3 methods, add `selfHealer_` member
- EDIT: `src/app/system/HookEngine.cpp` — replace inline self-heal calls with `selfHealer_->RecordHookFire()`, extract `ReinstallKeyboardAndMouseHooks()`, fix comment
- EDIT: `CMakeLists.txt` — add HookSelfHealer.cpp to NextKeyApp + NextKeyLite targets, add test to NextKeyTests

### Phase 2
- NEW: `src/watchdog/main.cpp`
- NEW: `src/watchdog/CMakeLists.txt`
- NEW: `src/app/system/HeartbeatPublisher.h`
- NEW: `src/app/system/HeartbeatPublisher.cpp`
- EDIT: `src/app/system/MainThreadWorker.cpp` — start heartbeat publisher thread
- EDIT: `src/app/system/StartupHelper.h` — add watchdog task registration
- EDIT: top-level `CMakeLists.txt` — add watchdog subdirectory
- EDIT: installer scripts (path determined during implementation — current installer location: `installer/` based on roadmap docs; verify and update during Phase 2 plan)
