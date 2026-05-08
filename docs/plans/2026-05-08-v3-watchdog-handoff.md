# v3.0.0 Hook Self-Healer + Watchdog — Session Handoff

- **Date:** 2026-05-08
- **Branch:** `feature/v3-hook-self-healer-watchdog`
- **HEAD commit:** `8cab06f`
- **Base:** Main `0447d83`
- **Status:** Code complete (Phase 1 + Phase 2). Smoke 1 FAILED — watchdog không tự respawn NexusKey sau `taskkill /F`. Needs investigation.

## TL;DR session tiếp theo

1. Phase 1 (HookSelfHealer extraction) đã verified trên Windows — Phase 1 smoke (Notepad + Dorion) PASS, log clean (commit `695bcb9`).
2. Phase 2 (Watchdog process supervisor) build OK Windows + Linux, NHƯNG **Smoke 1 (crash respawn) fail** — anh quan sát "không thấy tự chạy lại" sau `taskkill /F /IM NexusKey.exe`.
3. Cần debug TẠI SAO watchdog không respawn — list giả thuyết bên dưới.
4. Sau khi Smoke 1 pass, implement UX (Option A + C) anh đã chốt: kill-twice-stop + tray menu "Stop Watchdog".

## Commits trên branch (15 commits)

Plan + spec:
- `8a7ae1c` docs(spec)
- `f6e138c` docs(plan)

Phase 1 — HookSelfHealer extraction:
- `c14527c` Task 1.1: IHookSelfHealer interface header
- `b4cc3ba` Task 1.2: 3 lifecycle tests (Windows-only)
- `059c9a7` Task 1.3: RawInputSelfHealer impl
- `adcd11b` Task 1.3 fix: HOOK_LOG → NEXTKEY_LOG (cross-TU visibility)
- `695bcb9` Task 1.4: Wire HookEngine to inject IHookSelfHealer

Phase 2 — Watchdog process:
- `cf18547` Task 2.1: HeartbeatPublisher class
- `f4c1df5` Task 2.1 fix: auto-reset event (canonical pulse pattern, was unreliable PulseEvent-equivalent)
- `47d291c` Task 2.2: Wire HeartbeatPublisher in main.cpp
- `23ff309` Task 2.3: NexusKeyWatchdog.exe scaffold
- `b53a23a` Task 2.4: StartupHelper Watchdog Task Scheduler helpers
- `17f8afa` Task 2.5: Wire watchdog registration in main.cpp first-run

Cleanup + fix:
- `980c2f2` Doc cleanup: stale comments in HookEngine.cpp post-extraction
- `8cab06f` Fix C4189 (Release build): `[[maybe_unused]]` on `loggedMethod`

## Smoke 1 — FAIL state

**Setup attempted:**
1. Build Windows Release: `--target NextKeyApp` (CLAUDE.md ghi sai `--target NexusKey`, target thực = `NextKeyApp` because of `OUTPUT_NAME` aliasing — see [feedback_cmake_target_name.md](../../.claude-work/projects/-home-phatmt-code-NexusKey/memory/feedback_cmake_target_name.md))
2. Anh chạy NexusKey + watchdog
3. `taskkill /F /IM NexusKey.exe`
4. Wait 90-120s
5. **Result:** NexusKey không respawn

**Anh đã KHÔNG paste:**
- `%LOCALAPPDATA%\NexusKey\watchdog.log` content
- `Get-Process NexusKeyWatchdog` output (watchdog có chạy không)
- `Get-ScheduledTask -TaskName Watchdog -TaskPath "\NexusKey\"` (task registered chưa)

→ **Cần data này để debug session sau.**

## Hypotheses cho Smoke 1 fail

Sắp xếp theo likelihood (cao xuống thấp):

### H1: Watchdog chưa được launch (most likely)
- Anh có thể chạy NexusKey nhưng KHÔNG launch watchdog manually
- Task Scheduler `\NexusKey\Watchdog` chỉ trigger at-logon — anh không logout/login lại sau khi register
- **Verify:** `Get-Process NexusKeyWatchdog` — nếu rỗng → watchdog không chạy
- **Fix:** Manually launch `build\src\watchdog\Release\NexusKeyWatchdog.exe` để test mechanism, separately verify Task Scheduler trigger

### H2: Watchdog chạy nhưng UAC denial → task không registered
- Task 2.5 `IsWatchdogTaskRegistered` guard runs at first launch. Nếu anh deny UAC popup → task không được registered, watchdog không bao giờ tự chạy
- **Verify:** `Get-ScheduledTask -TaskName Watchdog -TaskPath "\NexusKey\"` — empty = not registered
- **Fix:** `schtasks /query /tn "\NexusKey\Watchdog"` from CMD; if missing, manually register via PowerShell or relaunch NexusKey + accept UAC

### H3: Watchdog chạy nhưng không nhận heartbeat (IL mismatch)
- Nếu anh chạy NexusKey với `runAsAdmin=true` (high IL) → CreateEventW tạo events ở high-IL → watchdog limited-IL có thể KHÔNG mở được handle (security descriptor)
- **Verify:** `watchdog.log` có "Heartbeat events absent + process not running → respawn" hoặc tương tự liên tục? Then watchdog đang chạy nhưng OpenEventW fail
- **Fix:** Add explicit security descriptor to events (low integrity label) HOẶC test với NexusKey không elevated

### H4: Watchdog chạy nhưng CreateProcessW fail
- Watchdog `RespawnNexusKey()` dùng `GetModuleFileNameW + \NexusKey.exe` — assumes co-located. Có thể path wrong (vd: NexusKeyWatchdog.exe ở build subfolder, NexusKey.exe ở khác build folder)
- **Verify:** `watchdog.log` có "RespawnNexusKey: CreateProcess FAILED err=..."
- **Fix:** Adjust path resolution OR colocate binaries via install rule (currently no `install(TARGETS)` for watchdog)

### H5: Test methodology issue
- 90-120s wait có đủ không? Watchdog logic: 30s pulse + 90s timeout = lên đến 120s detection latency. Anh wait đủ?
- Hoặc: heartbeat publisher chưa kịp Start (NexusKey đang init) khi taskkill → events chưa exist → watchdog "events absent" path → vẫn respawn nhưng anh kill quá nhanh
- **Verify:** `watchdog.log` timestamps vs taskkill timestamp — gap thực tế
- **Fix:** Wait longer (3-5 phút) hoặc instrument với perf-counter tighter

## Debug action checklist (session sau)

```powershell
# 1. Check watchdog process
Get-Process NexusKeyWatchdog -ErrorAction SilentlyContinue
# Expected: 1 row showing PID + memory

# 2. Check task registration
Get-ScheduledTask -TaskName Watchdog -TaskPath "\NexusKey\" -ErrorAction SilentlyContinue
# Expected: State Ready/Running

# 3. Check log content (most useful diagnostic)
Get-Content $env:LOCALAPPDATA\NexusKey\watchdog.log -Tail 50

# 4. Check if heartbeat events exist (requires Sysinternals handle.exe)
handle.exe -a NexusKeyHeartbeat 2>$null
# Expected: NexusKey.exe owns 2 named events

# 5. Manual launch watchdog for isolated testing
& "$env:USERPROFILE\path\to\build\src\watchdog\Release\NexusKeyWatchdog.exe"
# Then taskkill NexusKey, observe log
```

## UX scope chốt cho session sau (Option A + C)

Anh đã decide 2026-05-08: implement BOTH options để user có thể chủ động tắt watchdog.

### Option A — "Kill twice within 60s = stop"
**Implementation (~15 LOC trong `src/watchdog/main.cpp`):**
- Track `lastSpawnTime_ = GetTickCount()` ngay sau RespawnNexusKey() success
- Trước khi respawn, check: nếu `now - lastSpawnTime_ < 60'000` → user kill kép → log "User killed twice within 60s — stopping watchdog" + `return 0;`
- Reset `lastSpawnTime_` = 0 sau 60s+ (no-op effectively, since check uses fresh GetTickCount())

**Discoverable:** user instinctively kill again nếu lần đầu không stick.

### Option C — Tray menu "Stop Watchdog"
**Implementation (~30 LOC trong `src/app/system/TrayIcon.{h,cpp}` + `src/app/main.cpp`):**
- Add new tray menu item `TrayMenuId::StopWatchdog` (e.g., right-click → "Stop Auto-Restart" hoặc tương tự)
- Handler:
  ```cpp
  case TrayMenuId::StopWatchdog: {
      // Tell watchdog this is an intentional stop — set graceful flag
      g_heartbeat.SignalGracefulShutdown();
      // Wait for watchdog to observe (next 90s wait cycle in worst case)
      // Optional: also kill watchdog process explicitly via taskkill for instant feedback
      ShellExecuteW(nullptr, L"runas", L"taskkill", L"/F /IM NexusKeyWatchdog.exe", nullptr, SW_HIDE);
      MessageBoxW(nullptr, L"Watchdog stopped. NexusKey will not auto-restart on crash.", L"NexusKey", MB_OK);
      break;
  }
  ```
- Add string resource for menu label (Vietnamese + English per existing strings.toml pattern)

**File touch list cho A+C:**
- `src/watchdog/main.cpp` — Option A logic (kill-twice-stop)
- `src/app/system/TrayIcon.h` — TrayMenuId::StopWatchdog enum
- `src/app/system/TrayIcon.cpp` — menu item creation + (if applicable) string handling
- `src/app/main.cpp` — OnMenuCommand handler
- `src/core/Strings.cpp` (or equivalent) — "Stop Auto-Restart" string entry
- Optional: docs/CHANGELOG.md update

## Risks / open items

1. **Mode 1 long-uptime** vẫn dropped khỏi v3.0.0 scope (no user follow-up). Note: nếu user 2.1.18 nào tái xuất hiện khiếu nại, telemetry build sẽ là path đúng — không phải watchdog (xem `project_h6_premise_questioned.md` + spec).

2. **Watchdog AV scan chưa run** (Smoke 6 deferred until Smoke 1 unblocks).

3. **`install(TARGETS NexusKeyWatchdog)` rule chưa add** — packaging cho release sẽ cần co-locate NexusKeyWatchdog.exe với NexusKey.exe trong installer. Final reviewer flag (Important #3) chưa addressed.

4. **UAC retry-loop UX gap** (final reviewer Important #1) — nếu user deny UAC at first-run, mỗi launch sẽ re-prompt. Cần `SystemConfig::watchdogPromptDeclined` flag. Defer to v3.0.1 hoặc fold vào Option C work.

5. **CLAUDE.md outdated:** ghi `--target NexusKey` (sai). Cần update sang `--target NextKeyApp`. Não-critical, just doc.

## Linux GTest baseline

1555/1555 PASS xuyên suốt branch.

## Files inventory

### Phase 1 NEW
- `src/app/system/HookSelfHealer.h` (75 LOC)
- `src/app/system/HookSelfHealer.cpp` (172 LOC)
- `tests/HookSelfHealerTest.cpp` (54 LOC, Windows-only)

### Phase 2 NEW
- `src/app/system/HeartbeatPublisher.h` (60 LOC)
- `src/app/system/HeartbeatPublisher.cpp` (84 LOC, auto-reset event)
- `tests/HeartbeatPublisherTest.cpp` (54 LOC, Windows-only)
- `src/watchdog/main.cpp` (160 LOC)
- `src/watchdog/CMakeLists.txt` (26 LOC)

### Modified
- `src/app/system/HookEngine.h` (-19 / +9 = -10)
- `src/app/system/HookEngine.cpp` (-137 / +27 + later +6 doc / +1 maybe_unused)
- `src/app/system/StartupHelper.h` (+89 LOC for watchdog helpers)
- `src/app/main.cpp` (+27 LOC: heartbeat wire + watchdog registration)
- `CMakeLists.txt` (+20 LOC: 3 spots × 2 modules + watchdog subdirectory)

### Documentation
- `docs/plans/2026-05-08-hook-self-healer-watchdog-design.md` (spec, 232 LOC)
- `docs/plans/2026-05-08-hook-self-healer-watchdog-plan.md` (plan, 1462 LOC)
- This handoff doc

## Memory entries created/updated

- `project_h6_premise_questioned.md` — H6 SPSC ring premise stale (TSF/Hook không parallel)
- `project_quick_convert_case_bug.md` — Quick Convert HOA→Title Case bug (separate from watchdog)
- `feedback_design_philosophy.md` — Design philosophy chốt 2026-05-08
- `feedback_no_rm_rf_without_confirm.md` — destructive ops protocol incident
- `feedback_cmake_target_name.md` — `--target NextKeyApp` not `NexusKey`

## Next session entry point

```
Continue branch feature/v3-hook-self-healer-watchdog (HEAD 8cab06f).
Read this handoff doc first: docs/plans/2026-05-08-v3-watchdog-handoff.md.
Smoke 1 failed — debug per checklist Section "Debug action checklist".
After Smoke 1 pass, implement Option A + C per Section "UX scope chốt".
Resume Task 2.6 from there.
```
