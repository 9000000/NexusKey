# 10. Refactoring & Optimization Checklist

When refactoring, extracting, or optimizing code, verify these invariants are preserved.
These have been broken before by well-intentioned changes.

## 10.1 Single-Instance Mutex

- [ ] `CreateMutexW` uses **`nullptr`** for security attributes (not `MakeCreatorOnlySecurityAttributes`)
- [ ] CO (Creator Owner) SID does NOT resolve for non-container objects (mutex, event, file mapping) — using it causes `ERROR_ACCESS_DENIED` instead of `ERROR_ALREADY_EXISTS`
- [ ] Test: launch EXE twice rapidly — second instance must exit silently (or show existing Settings)
- [ ] Mutex name matches between builds: `Local\NexusKey_Main_Mutex` / `Local\NexusKeyLite_Main_Mutex`

## 10.2 Subprocess Lifecycle

- [ ] All `SpawnSubprocess()` / `CreateProcessW` calls → `TrackChildProcess()` to register with Job Object
- [ ] `RunSettingsSubprocess()` calls `TerminateAllSubprocesses()` before `ExitProcess(0)`
- [ ] Main process `TerminateAllSubprocesses()` is called on both normal exit and `WM_CLOSE`
- [ ] Job Object with `JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE` catches orphaned grandchild processes
- [ ] Test: open Settings → open ExcludedApps → Exit from tray → all 3 processes must die

## 10.3 DPI Awareness

- [ ] `NexusKey.exe.manifest` exists with `PerMonitorV2` DPI awareness
- [ ] `NexusKeyLite.exe.manifest` exists with `PerMonitorV2` DPI awareness
- [ ] Both manifests include Common Controls v6 dependency
- [ ] No duplicate manifest sources (don't mix `.manifest` file with `#pragma comment(linker, "/manifestdependency:...")`)
- [ ] Test: right-click tray icon at 125%/150% — menu text must be crisp, not blurry

## 10.4 SharedState DACL

- [ ] `SharedStateManager::Create()` uses `nullptr` DACL (not `MakeCreatorOnlySecurityAttributes`)
- [ ] Cross-process `OpenReadWrite()` works from subprocess (same user, default DACL)
- [ ] Same-process readers pass `&g_sharedState` pointer — don't create new handles

## 10.5 Extract/Move Function Safety

When extracting functions from one file to another (e.g., `SciterHelper` → `DarkModeHelper`):

- [ ] All callers updated (grep for old namespace/class name)
- [ ] Include paths updated in ALL files that used the old header
- [ ] CMakeLists.txt updated for BOTH targets (NextKeyApp + NextKeyLite)
- [ ] No circular includes introduced
- [ ] Functions that were `inline` in headers remain accessible (ODR)
- [ ] Old header still compiles (remaining functions don't depend on moved functions)
