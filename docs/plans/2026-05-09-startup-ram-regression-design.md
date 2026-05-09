# Startup RAM regression — design & measurement plan

**Date**: 2026-05-09
**Owner**: PhatMT
**Status**: Phase 1 fix shipped (HeartbeatPublisher opt-in) — Windows verification pending

## Implementation status (2026-05-09)

**Shipped — HeartbeatPublisher opt-in lifecycle:**

- `src/app/main.cpp:526` — `g_heartbeat.Start()` now guarded by
  `if (systemConfig.watchdogEnabled)`. Default config (watchdog OFF) skips
  the thread + 2 named events at boot.
- `src/app/main.cpp:996` — toggle Watchdog ON: explicit `Start()` after
  config save.
- `src/app/main.cpp:979` — toggle Watchdog OFF: explicit `Stop()` after
  config save and `taskkill`.
- `tests/HeartbeatPublisherTest.cpp` — added `IdempotentStartWhileRunning`
  + `RestartAfterStop` to cover the toggle ON→OFF→ON cycle. (Note:
  `IdempotentStartWhileRunning` is weak — see TODO.md 2026-05-09 entry.)

**Expected recovery (default config):** ~50–80 KB Working Set Private +
2 named-event handles + 1 thread schedule slot.

**Not yet measured on Windows.** Once Windows MSVC build runs with both
versions side-by-side, fill `summary.md` and decide whether further fixes
(Sciter resource embed, OutputInjector lazy init) are needed to hit the
≤2.0 MB acceptance target.

**Pending Windows verification:**
1. Cold launch default config → Process Explorer Threads = 6 (was 7).
2. Toggle Watchdog ON → Threads = 7.
3. Toggle Watchdog OFF → Threads back to 6.
4. Tray Quit → no crash, watchdog process (if running) does not respawn.
5. Run `HeartbeatPublisherTest` MSVC → 5 tests PASS.

## Problem

Release build at HEAD (`80a906f`) idle steady-state right after launch consumes
**~3.0 MB** Working Set vs **~1.7 MB** at tag `v2.1.24` — a regression of
**~1.3 MB** (≈+76%) per Task Manager observation on `NextKeyApp.exe`.

User confirmed: measured idle, immediately after launch (not runtime growth).
This rules out unbounded caches (HWND→AppProfile, FSM, macros) and points at
static data, startup allocations, and always-on threads.

## What changed between v2.1.24 and HEAD

`git log v2.1.24..HEAD` shows ≈170 commits across Sprint 1, 2, 3, T2.1, H1, H5,
and the v3 watchdog feature. Top startup-cost suspects:

| Source | Estimate | Always-on at default config? |
|---|---|---|
| Sciter resources baked into binary (`src/app/resources.cpp` 609 KB) | ~400–600 KB | Yes — file not present at v2.1.24 |
| `HeartbeatPublisher` thread (`g_heartbeat.Start()` `main.cpp:525`) | ~80–200 KB | **Yes** — no `if (watchdogEnabled)` guard |
| `MainThreadWorker` thread (`g_mainThreadWorker.Start()` `main.cpp:520`) | ~80–200 KB | Yes — drives 200 ms CJK poll |
| 4 `OutputInjector` instances via Factory | ~30–80 KB | Yes |
| `HookSelfHealer` (RawInputSelfHealer + hidden window) | ~30–60 KB | Yes |
| HWND→AppProfile cache (`unordered_map` reserve) | ~10–30 KB | Yes |
| Phonotactics + IPhonologyRules DI tables | ~50–100 KB | Yes |
| Sciter SDK upgrade (6.0.3.5) runtime overhead | ~50–200 KB | Yes |
| Misc (RCU `shared_ptr`, atomics, new headers) | ~50–100 KB | Yes |

Sum ≈ 770 KB – 1570 KB → consistent with the observed +1.3 MB.

**Note on Watchdog**: the watchdog *process* (`NexusKeyWatchdog.exe`) is opt-in
and stays off in default config (`main.cpp:345 if (systemConfig.watchdogEnabled)`).
But `HeartbeatPublisher::Start()` runs unconditionally — it publishes a 30 s
event "best effort" so a later watchdog opt-in receives a pulse immediately.
This thread therefore contributes to default-config RAM even when watchdog is
disabled.

## Approach: measure first, fix second

User selected **measurement-driven fix** over guess-and-revert. Aligns with
existing project discipline:

- Sprint 1 D1–D4 baseline-locking pattern.
- `feedback_review_discipline` memory: report only, never auto-fix without
  evidence.
- Core philosophy "Nhanh / Nhẹ / Mượt" — RAM regression is a Nhẹ violation,
  worth a proper diagnosis.

## Measurement design (4 phases)

### Phase 1 — Tooling & build setup

**Tools (Windows, native):**

| Tool | Purpose | Source |
|---|---|---|
| **VMMap** (Sysinternals) | Snapshot + diff between `.mmp` files | https://learn.microsoft.com/sysinternals/downloads/vmmap |
| **Process Explorer** | Thread / handle / Private Bytes / Working Set columns | https://learn.microsoft.com/sysinternals/downloads/process-explorer |
| **dumpbin /HEADERS** | PE section sizes (`.text`, `.rdata`, `.data`) | Visual Studio Developer Command Prompt |
| **UMDH** *(optional)* | Heap stack-trace attribution if Phase 3 is inconclusive | Windows SDK Debugging Tools |

**Builds to compare:**

- **HEAD** — already built, expected at `build/src/app/Release/NextKeyApp.exe`
  (or wherever Release output lands).
- **v2.1.24** — user has the old `.exe` from a prior install / release zip.
  No rebuild needed.

**Configs must match:** Release, `/MD`, `NEXUSKEY_HOOK_ENGINE` ON, same MSVC
version, same Windows version on same machine.

### Phase 2 — Snapshot capture protocol

For each version (v2.1.24, then HEAD):

1. End any running `NextKeyApp.exe` in Task Manager.
2. Launch the target `NextKeyApp.exe`.
3. Wait **15 seconds** — past tray init, hook attach, cold-start I/O.
4. **Do not** open Settings dialog (keeps Sciter window unloaded).
5. **Do not** hover the tray icon (avoids tooltip render).
6. VMMap → File → Select Process → `NextKeyApp.exe` → File → Save → `.mmp`.
7. Process Explorer → record Private Bytes, Working Set Private/Shareable,
   Threads, Handles into `summary.md`.
8. From VS Developer Cmd Prompt: `dumpbin /HEADERS NextKeyApp.exe > headers.txt`.

Output (per version): 1 `.mmp` + 1 `headers.txt` + Process Explorer numbers.

Snapshot directory: `.planning/measurements/ram-regression-2026-05-09/`

### Phase 3 — Diff analysis & attribution

VMMap → File → **Compare** → load both `.mmp` files. Walk each category:

| VMMap category | Maps to | Action when diff > 200 KB |
|---|---|---|
| **Image** | EXE/DLL `.text` + `.rdata` mapped pages | `dumpbin` diff → identify which section |
| **Stack** | Per-thread stack commit | Count threads; quantify per-thread overhead |
| **Heap** | Committed allocations (CRT default heap) | Suspect Sciter parsed DOM, Factory instances, cache buckets |
| **Mapped File** | `sciter.dll`, fonts, SharedState memory-mapped | Check Sciter SDK version diff |
| **Shareable** | Read-only shared pages | Usually noise |
| **Private Data** | VirtualAlloc'd regions | Less common; investigate per-region |

**Threshold tiers:**
- < 50 KB / section → noise, ignore.
- 50–200 KB → log, do not fix individually.
- &gt; 200 KB → primary fix candidate.

### Phase 4 — Decision matrix → fix priorities

Order from cheap → expensive once attribution is known:

| Nghi can | Fix | Effort | Risk | Est. recover |
|---|---|---|---|---|
| `HeartbeatPublisher` always-on | Guard `Start()` with `watchdogEnabled`; start lazily on toggle ON | XS | L | 30–80 KB |
| `unordered_map` reserve patterns | Verify HookEngine init does not pre-reserve buckets | XS | L | 5–20 KB |
| Sciter resources `.rdata` (609 KB) | Revert packfolder embed → load from disk like v2.1.24 | S | M (single-binary distro) | 300–500 KB |
| Sciter parsed DOM at startup | Lazy-create Sciter window when user opens Settings | M | L | 100–300 KB |
| 4 OutputInjector eagerly created | Lazy-create per app dispatch | M | M (Factory pattern change) | 30–80 KB |
| `MainThreadWorker` always-on | Cannot drop — drains config-reload + CJK poll | — | — | ~0 |
| `HookSelfHealer` window | Required for hook recovery | — | — | — |
| FSM / Phonotactics tables (.rdata) | Untouched pages → already accounted by Image | — | — | — |

**Acceptance criteria:**
- Idle steady-state Working Set ≤ **2.0 MB** (allows +300 KB headroom for v3
  features over v2.1.24).
- Chaos test 11/11 PASS unchanged.
- L1 latency p99 unchanged (verify via `NextKeyTestRunner` perf CSV).

**Attack order:**
1. Guard `HeartbeatPublisher.Start()` (XS effort, L risk).
2. Audit `unordered_map` reserve calls in `HookEngine` init.
3. Re-measure. If still > 2.0 MB → tackle Sciter resource embedding.
4. Last resort: OutputInjector lazy init.

## Artifacts

- This design doc.
- `.planning/measurements/ram-regression-2026-05-09/` — VMMap snapshots,
  dumpbin output, summary table, screenshots.
- (Future) Implementation plan once Phase 3 attribution is known.
