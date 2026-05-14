# VKeyTestRunner

E2E stress test harness for the VKey IME (Vietnamese hook engine on Windows).

For project context and Phase 1+ roadmap see [`/HANDOFF.md`](../../HANDOFF.md).

## What it does

Drives Win32 SendInput against a running VKey hook, types raw Telex
keystrokes from a TOML corpus, verifies clipboard contents matches an
expected Vietnamese string, and post-mortem-parses VKey's debug log
to compute per-keystroke L1 timing. Two output formats:

- **JUnit XML** — for CI consumption
- **Perf CSV** — for spreadsheet / pandas analysis

## Build

### Linux (cross-platform tests, no key driving)
```bash
cmake -B build-linux -G "Unix Makefiles" -DCMAKE_BUILD_TYPE=Debug
cmake --build build-linux --target NextKeyTestRunnerTests
./build-linux/tests/VKeyTestRunnerTests
```

### Windows (full runner)
```bash
cmake --build build --target NextKeyTestRunner --config Debug
```
Output: `build/tools/Debug/NextKeyTestRunner.exe`.

## Run

### Smoke test (no verify)
```powershell
NextKeyTestRunner.exe --send vieejt --raw
```
Types `v-i-e-e-j-t` into focused window after 3 s. With VKey active,
target shows "việt".

### Verify single case
```powershell
chcp 65001                                                 # for non-ASCII --expected
NextKeyTestRunner.exe --send vieejt --raw --verify --expected "việt"
```
Selects all + copies + diffs clipboard vs expected. Exit 0 PASS, 1 FAIL.

### Full corpus + reports
```powershell
NextKeyTestRunner.exe ^
    --corpus    Z:\...\tools\VKeyTestRunner\corpus\chaos.toml ^
    --hook-log  Z:\...\build\Debug\VKey_hook.log ^
    --junit     report.xml ^
    --perf-csv  perf.csv
```
After the 11 cases run, the tool prompts you to **stop VKey** so its
8 KB debug log buffer flushes. Press Enter to continue → post-mortem L1
analysis prints + reports written.

## CLI reference

`NextKeyTestRunner.exe --help` for the full option list.

## Source layout

| File | Purpose |
|---|---|
| `src/main.cpp` | CLI dispatch (Windows `wmain`) |
| `src/Telex.h` | vn-str `strToTelex` C++ port (header-only, BMP only) |
| `src/SendInputDriver.{h,cpp}` | Win32 SendInput wrapper + `timeBeginPeriod` + busy-wait |
| `src/ClipboardReader.{h,cpp}` | `OpenClipboard` with retry, returns `std::optional<u16string>` |
| `src/Encoding.h` | UTF-8 ↔ UTF-16 helpers (BMP) |
| `src/KeyEscapes.{h,cpp}` | `\b\t\n\r\\\"` resolution for TOML literal `keys` field |
| `src/TomlLoader.{h,cpp}` | Corpus parser (uses `extern/tomlplusplus`) |
| `src/HookLogParser.{h,cpp}` | VKey debug log → `KeystrokeEntry` vector |
| `src/JunitXmlWriter.{h,cpp}` | JUnit XML report writer |
| `src/PerfCsvWriter.{h,cpp}` | Perf CSV report writer |
| `src/CaseResult.h` | Per-case verdict + timing struct |
| `tests/` | Cross-platform GTest suite (250 tests) |
| `corpus/chaos.toml` | 11 chaos test cases from brainstorm Phase 3 |
| `test_data/generate_golden.js` | Node.js generator for `tests/TelexGolden.h` (one-shot, vendored vn-str dependency) |

## Test corpus authoring

`corpus/chaos.toml` schema:
```toml
[[tests]]
name           = "1.2-tone-ghost-toans-bs3-i"
target_app     = "notepad"          # informational
keys           = 'toans\b\b\bi'     # raw Telex; literal string for backslash escapes
expected       = "toi"               # expected clipboard
inter_key_us   = 5000                # optional, default 10 000
budget_p99_us  = 1500                # optional, 0 disables
```

`keys` accepts raw Telex sequences (post-VKey-hook the engine should
produce `expected`). Special chars: `\b` = backspace, `\n` / `\r` = Enter,
`\t` = Tab, `\\` and `\"` literal.

## Limitations

See [`/HANDOFF.md`](../../HANDOFF.md) "Known limitations" for the full list.
The big ones:
- Sub-ms inter-key floored at ~3 ms (driver/OS limit)
- L1 timing post-mortem only (VKey hook log buffered)
- Heisenbug history: `_IONBF` log mode masked the very bugs the corpus
  exists to surface — see commit `d58bb4e` revert.
