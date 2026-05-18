# VKey Tools

## run-chaos.ps1

Drives `VKeyTestRunner.exe` against the chaos corpus across multiple host
apps in one shot. Replaces the per-host manual workflow (start VKey,
focus target, run runner, wait for "stop VKey" prompt, kill, press
Enter, rename outputs) — verifies a hook-engine change doesn't regress
the natural-classification matrix without a full afternoon of clicking.

**Requirements:** Windows PowerShell 5.1+, VKey + VKeyTestRunner
already built (`-DCMAKE_BUILD_TYPE=Debug`).

### Quick Start

```powershell
# 1. Open Discord + sign in (if testing 'discord' host)
# 2. Open Chrome on chat.openai.com / chatgpt.com + sign in (if testing 'gpt')
# 3. Run the sweep
powershell -ExecutionPolicy Bypass -File tools\run-chaos.ps1 -Tag channeltraits

# Subset run — skip auth-dependent hosts
powershell -ExecutionPolicy Bypass -File tools\run-chaos.ps1 -Tag dev -Hosts notepad,notepadpp,chrome
```

### Parameters

| Parameter | Required | Default | Description |
|-----------|----------|---------|-------------|
| `-Tag` | yes | — | Identifier embedded in output filenames |
| `-Hosts` | no | all 5 | Subset of `notepad notepadpp chrome discord gpt` |
| `-Corpus` | no | `tools/VKeyTestRunner/corpus/chaos.toml` | Test corpus |
| `-VKeyExe` | no | `build/Debug/VKey.exe` | App under test |
| `-RunnerExe` | no | `build/tools/VKeyTestRunner/Debug/VKeyTestRunner.exe` | Driver |
| `-HookLog` | no | auto: `<install dir>\VKey_VKey_<pid>.log` | Override only if VKey writes elsewhere (PathOverride / AppData fallback). Script resolves the per-launch PID from the VKey.exe process it spawns. |
| `-OutDir` | no | repo root | Where reports land |

### Outputs (per host)

- `report-{Tag}-{host}.xml` — JUnit; check `tests=` and `failures=` attrs.
- `perf-{Tag}-{host}.csv` — per-keystroke L1 timing (post-mortem).
- `runner-{Tag}-{host}.log` — full runner stdout for triage.

### Verdict

Exit code 0 if every host reports `failures=0` and runner exit code 0.
Non-zero if any host failed or had no target window (e.g. Discord not
signed in). Final summary table prints per-host counts.

### Host preconditions

| Host | Auto-launch? | Manual prep |
|------|--------------|-------------|
| notepad | yes | none |
| notepadpp | yes (if installed in default path) | none |
| chrome | yes (if not already running) | none |
| discord | no | open + sign in beforehand |
| gpt | no | open Chrome tab on chatgpt.com + sign in |

During each run **do not touch keyboard / mouse** — the runner needs
target focus for the full 11-case sequence.

## benchmark_ime.ps1

End-to-end IME benchmark. Compares VKey vs UniKey (or any IME) by injecting keystrokes into Notepad and measuring latency.

**Requirements:** Windows PowerShell 5.1+ (built-in). No extra dependencies.

### Quick Start

```powershell
# 1. Switch to VKey, run benchmark
powershell -ExecutionPolicy Bypass -File .\benchmark_ime.ps1 -IME "VKey"

# 2. Switch to UniKey, run again
powershell -ExecutionPolicy Bypass -File .\benchmark_ime.ps1 -IME "UniKey"

# 3. Compare results
powershell -ExecutionPolicy Bypass -File .\benchmark_ime.ps1 -Compare
```

### Parameters

| Parameter | Default | Description |
|-----------|---------|-------------|
| `-IME` | (prompt) | Name of the active IME |
| `-Rounds` | 5 | Number of rounds per test |
| `-DelayMs` | 80 | Delay between words (ms), simulates typing speed |
| `-Compare` | | Compare the 2 most recent result files |
| `-NoEnglish` | | Skip the English typing test |

### What it measures

- Opens Notepad automatically
- Types 20 Vietnamese words (Telex) x N rounds via `SendInput`
- Types 20 English words x N rounds
- Reports per-key latency (us), per-round total (ms), avg/min/max
- Saves results to `benchmark_<ime>_<timestamp>.json`

### Example output

```
============================================================
  Vietnamese Telex - VKey
============================================================
  Round 1:    45.23 ms  (  34.2 us/key, 132 keys)
  Round 2:    43.87 ms  (  33.2 us/key, 132 keys)
  --------------------------------------------------------
  Average:    44.55 ms  (  33.7 us/key)
  Best:       43.87 ms
  Worst:      45.23 ms

=================================================================
  IME COMPARISON
=================================================================
                          VKey         UniKey
                      ────────────    ────────────
Vietnamese (avg/key)       33.7 us         35.2 us
English (avg/key)          28.1 us         29.5 us

  Vietnamese: VKey is 4.3% faster
```

## build_lite.ps1 / build_app.ps1

Build & run scripts for the two VKey variants.

| Script | What it builds | Output |
|--------|---------------|--------|
| `build_lite.ps1` | Classic Win32 UI (no Sciter) | `build-lite\Debug\VKeyClassic.exe` |
| `build_app.ps1` | Full Sciter UI | `build\Debug\VKey.exe` |

```powershell
# Build + run Classic (Lite)
.\tools\build_lite.ps1 -Run

# Build + run Sciter (App)
.\tools\build_app.ps1 -Run

# Clean rebuild
.\tools\build_lite.ps1 -Clean -Run

# Release build
.\tools\build_lite.ps1 -Release

# Debug both side by side
.\tools\build_app.ps1 -Run; .\tools\build_lite.ps1 -Run
```

Auto-kills existing instance before launching. Auto-configures CMake if no cache exists.

## release_tag.sh

Create a git tag for release.

## update_sciter.sh

Update Sciter SDK to latest version from GitLab. Copies includes + binaries to `extern/sciter/`.

```bash
bash tools/update_sciter.sh
```
