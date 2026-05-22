# HookEngine De-God — Selective Probe Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Close 2 concrete refactor seams in HookEngine — (a) eliminate cross-thread enum race on `currentCodeTable_/globalCodeTable_/globalInputMethod_`; (b) extract `RebuildSnapshotFromToml` as pure function `ConfigSnapshotBuilder::BuildFromToml()` to prove ConfigApplier seam works before committing to full class extraction.

**Architecture:** Two-phase probe. Phase 0 = atomic enum migration (mechanical, low-risk, 30-45 min). Phase 1 = lift the TOML→snapshot logic to a pure free function in `src/core/config/` (Linux-testable, zero behavior change). Phase 2 = explicit GATE — evaluate Phase 1 outcome before deciding whether to continue with ConfigApplier class extraction; this plan does NOT pre-commit to Phase 2 work.

**Tech Stack:** C++20, std::atomic, CMake, Google Test (Linux GTest is the green-gate; Windows chaos = safety verification after).

**Sequencing rationale:**
- Anh declared HookEngine "architecturally complete" 2026-05-07 (REFACTOR_STATUS line 5), BUT the file grew +862 LOC (24%) in the 15 days since, concentrated in ConfigApply (P3a-P3f, ~+500 LOC) and Composition/commit-undo bug fixes (~+300 LOC). The growth disproves the "complete" verdict for ConfigApply specifically.
- Anh design philosophy: "không phân mảnh trừ khi mang lại hiệu quả". → Probe with the smallest possible safe slice; commit further only if probe pays off.
- T3 OutputDispatch (Sprint 2 PR #120/#121/#123) already extracted output layer. Composition state machine method-extracted via H1a/H1b/H1c (PR #143-145). Only ConfigApply layer remains as a credible split target.

**Out of scope for this plan:**
- ConfigApplier full class extraction (defer to Phase 2 gate decision).
- CompositionController class extraction (REFACTOR_STATUS treats H1a/b/c as sufficient; revisit only if Phase 2 proves out).
- Focus/AppProfile extraction (Win32-heavy, low test-value, skip per prior analysis).
- `cachedFocusedClass_` mailbox, 200ms tick adaptive, dynamic `timeBeginPeriod(1)` — all P3, defer.

---

## File Structure

### Phase 0 — Atomic enum migration
**Modify:**
- `src/app/system/HookEngine.h` (lines 442-447) — change 3 field declarations to `std::atomic<>`.
- `src/app/system/HookEngine.cpp` — convert ~30 read/write sites to `.load()/.store()`.
- `/home/phatmt/.claude-work/projects/-home-phatmt-code-NexusKey/memory/MEMORY.md` + relevant project memory files — refresh stale T3/Sprint 3 status.

### Phase 1 — `ConfigSnapshotBuilder` extraction
**Create:**
- `src/core/config/ConfigSnapshotBuilder.h` — declaration of `BuildFromToml()` free function.
- `src/core/config/ConfigSnapshotBuilder.cpp` — implementation (52 LOC lifted from `HookEngine::RebuildSnapshotFromToml`).
- `tests/core/config/ConfigSnapshotBuilderTest.cpp` — Linux gtest fixture with synthetic TOML.

**Modify:**
- `src/app/system/HookEngine.cpp` — replace `RebuildSnapshotFromToml` body with call to `ConfigSnapshotBuilder::BuildFromToml()`. Keep `isExcludedApp_.store(false)` side-effect in HookEngine when `excludeApps_` is off (NOT moved — it's HookEngine state).
- `CMakeLists.txt` — add new `.cpp` to `NextKeyApp` + `NextKeyLite` + test target source lists.

### Phase 2 — GATE (no files)
Document outcome of Phase 1 in REFACTOR_STATUS Section G + decide go/no-go for further extraction.

---

## Phase 0 — Atomic CodeTable / InputMethod migration

**Why:** Three fields (`currentCodeTable_`, `globalCodeTable_`, `globalInputMethod_`) are written from BOTH the worker thread (`ReloadFromToml` → `ApplyConfig` → field writes at HookEngine.cpp:239-241, 707-709, 757-758) AND the hook thread (focus override at HookEngine.cpp:4248-4249, SetCodeTable at 517, QuickSync at 642-643). They are read on the hook hot path (HookEngine.cpp:2107, 2143, 2176, 2463, 3536, 3547, 3769, 3915, 3941, 4248). x64 hardware torn-read-safe at 1 byte (`uint8_t` underlying), so empirically benign — but formal UB per the C++ memory model, and `std::atomic` formalization is free at this size.

**Pre-flight check (do NOT skip):**
```bash
git status   # MUST be clean OR only contain expected dirty files (README.md, RELEASE_NOTES.md, src/app/system/HookEngine.cpp, GUIDE.md)
git log -1 --oneline   # confirm HEAD before any edit
git branch --show-current   # MUST be feat/architecture-review-v3.1 or a branch off it
```

If the working tree has unrelated modifications, stash them first (`git stash push -m "pre-degod-probe"`) — do NOT mix unrelated work into this PR.

### Task 0.1: Verify enum underlying types are lock-free atomic

**Files:**
- Read: `src/core/config/TypingConfig.h:16-30`

- [ ] **Step 1: Inspect enum declarations**

Run: `grep -n "enum class CodeTable\|enum class InputMethod" src/core/config/TypingConfig.h`
Expected output:
```
16:enum class InputMethod : uint8_t {
25:enum class CodeTable : uint8_t {
```

Both are `uint8_t`. `std::atomic<T>` for any T satisfying `std::is_trivially_copyable_v` and ≤ 8 bytes is lock-free on x64. No assertion needed — proceed.

### Task 0.2: Migrate field declarations to `std::atomic`

**Files:**
- Modify: `src/app/system/HookEngine.h:442-447`

- [ ] **Step 1: Replace the 3 declarations**

Open `src/app/system/HookEngine.h`. Find lines 442-447:
```cpp
    CodeTable currentCodeTable_ = CodeTable::Unicode;
    CodeTable globalCodeTable_ = CodeTable::Unicode;     // config value, restored when no override
    // (appEncodingOverrides_, appInputMethodOverrides_, and
    // appSendMethodOverrides_ all removed — Phase 3d + 2026-05-19
    // follow-up. Readers go through configSnapshot_.load()->...)
    InputMethod globalInputMethod_ = InputMethod::Telex; // config value, restored when no override
```

Replace with:
```cpp
    // Writers: worker thread (ReloadFromToml → ApplyConfig) AND hook thread
    // (SetCodeTable, QuickSyncFromSharedState, focus override). Readers: hook
    // hot path (HandleAlphaKey, CommitComposition, ClassifyFocusedWindow,
    // ReplaceComposition, macro expansion). Atomic load/store with
    // acquire/release ordering — uint8_t underlying is lock-free on x64.
    std::atomic<CodeTable> currentCodeTable_{CodeTable::Unicode};
    std::atomic<CodeTable> globalCodeTable_{CodeTable::Unicode};   // config value, restored when no override
    // (appEncodingOverrides_, appInputMethodOverrides_, and
    // appSendMethodOverrides_ all removed — Phase 3d + 2026-05-19
    // follow-up. Readers go through configSnapshot_.load()->...)
    std::atomic<InputMethod> globalInputMethod_{InputMethod::Telex}; // config value, restored when no override
```

- [ ] **Step 2: Verify `<atomic>` is already included**

Run: `grep -n "#include <atomic>" src/app/system/HookEngine.h`
Expected: at least one match (the header already uses `std::atomic` heavily for `synthEventsPending_`, `cachedFocusedHwnd_`, etc.). If missing, add `#include <atomic>` to the system-include block.

### Task 0.3: Migrate all writers to `.store()`

**Files:**
- Modify: `src/app/system/HookEngine.cpp` lines: 239-241, 517, 642-643, 707-709, 757-758, 4249

- [ ] **Step 1: Find all writers**

Run: `grep -nE "currentCodeTable_\s*=|globalCodeTable_\s*=|globalInputMethod_\s*=" src/app/system/HookEngine.cpp`
Expected 12 hits across the 6 locations above (line 757-758 has 2 writes on assignment expression).

- [ ] **Step 2: Convert ApplyConfig writes (cpp:239-241)**

Find:
```cpp
    currentCodeTable_ = config.codeTable;
    globalCodeTable_ = config.codeTable;
    globalInputMethod_ = config.inputMethod;
```
*(occurs at lines 239-241 AND lines 707-709 — both inside config-load paths; replace_all = false, do them one at a time and verify context with `git diff -U2` before next edit)*

For each occurrence, replace with:
```cpp
    currentCodeTable_.store(config.codeTable, std::memory_order_release);
    globalCodeTable_.store(config.codeTable, std::memory_order_release);
    globalInputMethod_.store(config.inputMethod, std::memory_order_release);
```

- [ ] **Step 3: Convert SetCodeTable write (cpp:517)**

Find at line 517:
```cpp
    currentCodeTable_ = ct;
```

Replace with:
```cpp
    currentCodeTable_.store(ct, std::memory_order_release);
```

- [ ] **Step 4: Convert QuickSync writes (cpp:642-643)**

Find at lines 642-643:
```cpp
        currentCodeTable_ = cfg.codeTable;
        globalCodeTable_ = cfg.codeTable;
```

Replace with:
```cpp
        currentCodeTable_.store(cfg.codeTable, std::memory_order_release);
        globalCodeTable_.store(cfg.codeTable, std::memory_order_release);
```

- [ ] **Step 5: Convert focus-override snapshot read+write (cpp:757-758)**

Find at lines 757-758:
```cpp
        currentCodeTable_ = (it != rcuSnap->appEncodingOverrides.end())
            ? it->second : globalCodeTable_;
```

Replace with:
```cpp
        currentCodeTable_.store(
            (it != rcuSnap->appEncodingOverrides.end())
                ? it->second
                : globalCodeTable_.load(std::memory_order_acquire),
            std::memory_order_release);
```

- [ ] **Step 6: Convert focus-driven runtime override (cpp:4248-4251)**

Find at lines 4248-4251:
```cpp
        if (targetTable != currentCodeTable_) {
            currentCodeTable_ = targetTable;
            HOOK_LOG(L"[Focus] codeTable override → %d (current exe %ls)",
                     static_cast<int>(currentCodeTable_), currentExe_.c_str());
```

Replace with:
```cpp
        if (targetTable != currentCodeTable_.load(std::memory_order_acquire)) {
            currentCodeTable_.store(targetTable, std::memory_order_release);
            HOOK_LOG(L"[Focus] codeTable override → %d (current exe %ls)",
                     static_cast<int>(currentCodeTable_.load(std::memory_order_acquire)),
                     currentExe_.c_str());
```

- [ ] **Step 7: Verify writer migration complete**

Run: `grep -nE "(currentCodeTable_|globalCodeTable_|globalInputMethod_)\s*=[^=]" src/app/system/HookEngine.cpp`
Expected: ZERO hits (all writers now use `.store()`).

### Task 0.4: Migrate all readers to `.load()`

**Files:**
- Modify: `src/app/system/HookEngine.cpp` lines: 513, 535, 628, 2107, 2143, 2176, 2463, 3402-3403, 3536, 3547, 3769, 3915, 3941

- [ ] **Step 1: Find all readers (raw symbol uses minus the writes already converted)**

Run: `grep -nE "currentCodeTable_|globalCodeTable_|globalInputMethod_" src/app/system/HookEngine.cpp | grep -v "\.store\|^[0-9]*:.*//"`
Expected ~14 hits.

- [ ] **Step 2: Convert comparison reads (cpp:513, 628, 2107, 2143, 2176, 2463, 3536, 3769, 4248 — already done)**

For each remaining `currentCodeTable_ != X` / `currentCodeTable_ == X` / `currentCodeTable_ != CodeTable::Unicode`:
- Pattern: `currentCodeTable_ != Y` → `currentCodeTable_.load(std::memory_order_acquire) != Y`
- Pattern: `globalCodeTable_` (as r-value) → `globalCodeTable_.load(std::memory_order_acquire)`

Example — line 513:
```cpp
    if (ct != currentCodeTable_ && engine_->Count() > 0) {
```
→
```cpp
    if (ct != currentCodeTable_.load(std::memory_order_acquire) && engine_->Count() > 0) {
```

Example — line 2463:
```cpp
    if (currentCodeTable_ != CodeTable::Unicode) return false;
```
→
```cpp
    if (currentCodeTable_.load(std::memory_order_acquire) != CodeTable::Unicode) return false;
```

- [ ] **Step 3: Convert return statement (cpp:535)**

Find:
```cpp
CodeTable HookEngine::GetCodeTable() const noexcept {
    return currentCodeTable_;
}
```

Replace with:
```cpp
CodeTable HookEngine::GetCodeTable() const noexcept {
    return currentCodeTable_.load(std::memory_order_acquire);
}
```

- [ ] **Step 4: Convert cast/assignment reads (cpp:3402-3403, 3547, 3915, 3941)**

Example — line 3402-3403:
```cpp
    cls.targetCodeTable = static_cast<int>(globalCodeTable_);
    cls.targetMethod    = static_cast<int>(globalInputMethod_);
```
→
```cpp
    cls.targetCodeTable = static_cast<int>(globalCodeTable_.load(std::memory_order_acquire));
    cls.targetMethod    = static_cast<int>(globalInputMethod_.load(std::memory_order_acquire));
```

Example — line 3915:
```cpp
        .currentCodeTable      = currentCodeTable_,
```
→
```cpp
        .currentCodeTable      = currentCodeTable_.load(std::memory_order_acquire),
```

Example — line 3547:
```cpp
            auto enc = CodeTableConverter::ConvertChar(newText[i], currentCodeTable_);
```
→
```cpp
            auto enc = CodeTableConverter::ConvertChar(newText[i], currentCodeTable_.load(std::memory_order_acquire));
```

- [ ] **Step 5: Convert remaining read in TryExpandMacro (cpp:3941)**

Find:
```cpp
        for (const auto& s : Macro::BuildSegments(plan.expansion, currentCodeTable_)) {
```

Replace with:
```cpp
        for (const auto& s : Macro::BuildSegments(plan.expansion, currentCodeTable_.load(std::memory_order_acquire))) {
```

- [ ] **Step 6: Verify reader migration complete**

Run: `grep -nE "currentCodeTable_|globalCodeTable_|globalInputMethod_" src/app/system/HookEngine.cpp | grep -vE "\.(load|store)\("`
Expected: ZERO hits outside comments. (Comment-only mentions like `// readers go through configSnapshot_` are fine.)

### Task 0.5: Build + test gate

- [ ] **Step 1: Linux build**

Run:
```bash
cmake --build build-linux --target VKeyTests 2>&1 | tail -30
```
Expected: clean build, no warnings. If `build-linux/` is missing or stale, regenerate:
```bash
cmake -B build-linux -G "Unix Makefiles" -DCMAKE_BUILD_TYPE=Debug
cmake --build build-linux --target VKeyTests
```

- [ ] **Step 2: Linux GTest sweep**

Run:
```bash
./build-linux/tests/VKeyTests 2>&1 | tail -10
```
Expected: `[  PASSED  ] 1905 tests.` (current baseline per MEMORY HDDLDD note; exact count may differ — must equal pre-change baseline, no regressions).

Capture baseline first if uncertain:
```bash
git stash && ./build-linux/tests/VKeyTests --gtest_brief 2>&1 | tail -3 && git stash pop
```

- [ ] **Step 3: Static check — no `.load() = X` mistakes**

Run: `grep -nE "(currentCodeTable_|globalCodeTable_|globalInputMethod_)\.load\([^)]*\)\s*=" src/app/system/HookEngine.cpp`
Expected: ZERO hits (atomic `load()` returns by value — assigning to it is UB and the compiler would catch it, but defense-in-depth).

- [ ] **Step 4: Commit Phase 0**

```bash
git add src/app/system/HookEngine.h src/app/system/HookEngine.cpp
git commit -m "$(cat <<'EOF'
refactor(hook): make currentCodeTable_/globalCodeTable_/globalInputMethod_ atomic

Eliminates formal UB cross-thread race. Writers: worker thread (ReloadFromToml
→ ApplyConfig) AND hook thread (SetCodeTable, QuickSync, focus override).
Readers: hook hot path (HandleAlphaKey, CommitComposition, ClassifyFocusedWindow,
ReplaceComposition, macro expansion).

x64 hardware was already torn-read-safe at uint8_t — this is formalization not
behavior change. Lock-free on x64. Acquire/release memory order matches existing
RCU pattern (config_, configSnapshot_).
EOF
)"
```

### Task 0.6: Windows verification (USER-RUN, NOT CLAUDE)

Anh phải chạy phần này — Claude không build Windows trong cùng session.

- [ ] **Step 1: Windows MSVC build**

User runs from WSL:
```bash
powershell.exe -Command "cd '\\wsl.localhost\Ubuntu-24.04\home\phatmt\code\NexusKey\build'; cmake --build . --target VKeyApp --config Debug 2>&1"
```
Expected: clean build, no `/WX` warnings.

- [ ] **Step 2: Chaos smoke (Notepad + Chrome)**

User runs at least Notepad + Chrome chaos to confirm no regression. Both must show no new failures vs Main baseline. Per memory, `chaos.toml` is the gate suite.

- [ ] **Step 3: User reports back PASS/FAIL**

If PASS → proceed to Phase 1. If FAIL → revert atomic migration via `git revert HEAD`, debug the regression, do NOT proceed to Phase 1 until Phase 0 is green.

### Task 0.7: Refresh stale memory

**Files:**
- Modify: `/home/phatmt/.claude-work/projects/-home-phatmt-code-NexusKey/memory/MEMORY.md`
- Read first: `/home/phatmt/code/NexusKey/docs/REFACTOR_STATUS.md`

- [ ] **Step 1: Identify stale entries**

The MEMORY.md Notes section says:
> "**Sequencing rule (anh decision 2026-05-07):** complete HookEngine backlog (H1, H3, H5, H8) before TypingEngine TODOs (T2, T3, T5, T6)."

Reality per REFACTOR_STATUS line 5: H1-H7 + T1-T6 are DONE except H8. T2.1 sprint also closed.

- [ ] **Step 2: Update MEMORY.md note**

Replace the "Sequencing rule" line with a current-state line. Edit `/home/phatmt/.claude-work/projects/-home-phatmt-code-NexusKey/memory/MEMORY.md`:

Find:
```
- **Refactor inventory (single source of truth)** — `docs/REFACTOR_STATUS.md` on Main. Living doc tracking done/in-flight/TODO refactor work + dead code + vital signs. Update when items ship. **Sequencing rule (anh decision 2026-05-07):** complete HookEngine backlog (H1, H3, H5, H8) before TypingEngine TODOs (T2, T3, T5, T6).
```

Replace with:
```
- **Refactor inventory (single source of truth)** — `docs/REFACTOR_STATUS.md` on Main. **HookEngine + TypingEngine backlog effectively closed 2026-05-08**: H1-H7 + T1-T6 + T2.1 sprint all merged; only H8 (Sprint 4+ roadmap) remains. File still grew +862 LOC (24%) in next 15 days, concentrated in ConfigApply (Phase 3a-3f RCU) and commit-undo bug fixes — closure was architectural-method-level, not file-level. See `docs/plans/2026-05-22-hookengine-degod-probe.md` for selective re-open probe.
```

- [ ] **Step 3: Commit memory + plan**

```bash
git add docs/plans/2026-05-22-hookengine-degod-probe.md
git commit -m "docs(plan): de-god probe plan — atomic enum + ConfigSnapshotBuilder lift"
```

Memory file is outside the repo (`~/.claude-work/...`) so no git commit for it — just save in place.

---

## Phase 1 — `ConfigSnapshotBuilder` extraction (probe)

**Goal:** Lift `HookEngine::RebuildSnapshotFromToml` body (52 LOC, HookEngine.cpp:3004-3055) into a pure free function `ConfigSnapshotBuilder::BuildFromToml()` in `src/core/config/`. Adds Linux-testable seam for TOML→ConfigSnapshot. Zero behavior change.

**Why this slice and not "ConfigApplier class":**
- `ApplyConfig` is 19 LOC and mutates 9 HookEngine fields — extraction would create more callbacks than it removes (anti-pattern).
- `RebuildSnapshotFromToml` reads 3 booleans + writes 1 atomic — clean module boundary.
- If this probe succeeds + delivers test value, anh has evidence for a larger ConfigApplier extract decision in Phase 2 GATE.

**Constraint preserved:** The `isExcludedApp_.store(false)` side-effect when `excludeApps_` is off (HookEngine.cpp:3032) must stay in HookEngine — it's not snapshot data, it's runtime app state. The lift function must NOT touch this; caller (HookEngine) keeps the side-effect.

### Task 1.1: Define `ConfigSnapshotBuilder::BuildFromToml` interface

**Files:**
- Create: `src/core/config/ConfigSnapshotBuilder.h`

- [ ] **Step 1: Write header**

```cpp
// src/core/config/ConfigSnapshotBuilder.h
#pragma once

#include <filesystem>
#include <memory>
#include <cstdint>

namespace NextKey {

struct ConfigSnapshot;

namespace ConfigSnapshotBuilder {

// Parse the live TOML config at `configPath` and build an immutable
// ConfigSnapshot. Pure — no global / engine state mutation. Caller decides
// what to do with side-effects (e.g. clearing isExcludedApp_ when excludeApps
// is off — see HookEngine::RebuildSnapshotFromToml caller).
//
// Reads:
//   - `[app_overrides]` (encoding / input-method / send-method per exe)
//   - `[excluded_apps]` (only if `excludeAppsEnabled` is true)
//   - `[tsf_apps]` (only if `tsfAppsEnabled` is true)
//   - `[macros]` (only if `macroEnabled` is true)
//
// Returns a heap-allocated, ready-to-publish snapshot.
[[nodiscard]] std::shared_ptr<const ConfigSnapshot> BuildFromToml(
    const std::filesystem::path& configPath,
    bool excludeAppsEnabled,
    bool tsfAppsEnabled,
    bool macroEnabled,
    std::uint32_t generation);

}  // namespace ConfigSnapshotBuilder
}  // namespace NextKey
```

- [ ] **Step 2: Verify header compiles (forward-decl only, no impl yet)**

Header is forward-decl + signature only — should compile against the existing tree without an impl. No action; just save and proceed.

### Task 1.2: Write failing test (TDD red)

**Files:**
- Create: `tests/core/config/ConfigSnapshotBuilderTest.cpp`

- [ ] **Step 1: Locate test target in CMake**

Run: `grep -n "NEXTKEY_TEST_SOURCES\|VKeyTests" CMakeLists.txt | head -10`
Expected: there is an aggregate `NEXTKEY_TEST_SOURCES` variable. New tests append here.

- [ ] **Step 2: Write the test**

```cpp
// tests/core/config/ConfigSnapshotBuilderTest.cpp
#include <gtest/gtest.h>
#include <filesystem>
#include <fstream>
#include "core/config/ConfigSnapshotBuilder.h"
#include "core/config/ConfigSnapshot.h"

namespace fs = std::filesystem;

class ConfigSnapshotBuilderTest : public ::testing::Test {
protected:
    fs::path tmpToml_;

    void SetUp() override {
        tmpToml_ = fs::temp_directory_path() /
                   ("vkey_snap_test_" + std::to_string(::testing::UnitTest::GetInstance()->random_seed()) + ".toml");
    }
    void TearDown() override { std::error_code ec; fs::remove(tmpToml_, ec); }

    void WriteToml(const std::string& body) {
        std::ofstream(tmpToml_) << body;
    }
};

TEST_F(ConfigSnapshotBuilderTest, EmptyConfigYieldsEmptySnapshot) {
    WriteToml("");
    auto snap = NextKey::ConfigSnapshotBuilder::BuildFromToml(
        tmpToml_, /*excludeApps*/false, /*tsfApps*/false, /*macroEnabled*/false, /*gen*/1);
    ASSERT_NE(snap, nullptr);
    EXPECT_EQ(snap->generation, 1u);
    EXPECT_TRUE(snap->excludedAppSet.empty());
    EXPECT_TRUE(snap->tsfAppSet.empty());
    EXPECT_TRUE(snap->macros.empty());
}

TEST_F(ConfigSnapshotBuilderTest, ExcludedAppsLoadedOnlyWhenEnabled) {
    WriteToml(R"(
[excluded_apps]
apps = ["chrome.exe", "code.exe"]
)");
    auto snapOff = NextKey::ConfigSnapshotBuilder::BuildFromToml(
        tmpToml_, /*excludeApps*/false, false, false, 0);
    EXPECT_TRUE(snapOff->excludedAppSet.empty()) << "feature off → set must be empty";

    auto snapOn = NextKey::ConfigSnapshotBuilder::BuildFromToml(
        tmpToml_, /*excludeApps*/true, false, false, 0);
    EXPECT_EQ(snapOn->excludedAppSet.size(), 2u);
    EXPECT_GT(snapOn->excludedAppSet.count(L"chrome.exe"), 0u);
}

TEST_F(ConfigSnapshotBuilderTest, MacroLoadedOnlyWhenEnabled) {
    WriteToml(R"(
[macros]
"vd" = "ví dụ"
"vk" = "VKey"
)");
    auto snapOff = NextKey::ConfigSnapshotBuilder::BuildFromToml(
        tmpToml_, false, false, /*macroEnabled*/false, 0);
    EXPECT_TRUE(snapOff->macros.empty());

    auto snapOn = NextKey::ConfigSnapshotBuilder::BuildFromToml(
        tmpToml_, false, false, /*macroEnabled*/true, 0);
    EXPECT_EQ(snapOn->macros.size(), 2u);
}

TEST_F(ConfigSnapshotBuilderTest, GenerationPropagated) {
    WriteToml("");
    auto snap = NextKey::ConfigSnapshotBuilder::BuildFromToml(
        tmpToml_, false, false, false, /*gen*/42);
    EXPECT_EQ(snap->generation, 42u);
}
```

- [ ] **Step 3: Add to CMake test target**

Edit `CMakeLists.txt`. Find the `NEXTKEY_TEST_SOURCES` list (location from Step 1). Append:
```cmake
    tests/core/config/ConfigSnapshotBuilderTest.cpp
```

If `tests/core/config/` directory does not yet exist, create it:
```bash
mkdir -p /home/phatmt/code/NexusKey/tests/core/config
```

- [ ] **Step 4: Run test — confirm linker fails (no impl yet)**

```bash
cmake --build build-linux --target VKeyTests 2>&1 | tail -20
```
Expected: link error citing `BuildFromToml`. This is the red phase — proceed to impl.

### Task 1.3: Implement `BuildFromToml` (TDD green)

**Files:**
- Create: `src/core/config/ConfigSnapshotBuilder.cpp`

- [ ] **Step 1: Write the impl**

The body is essentially HookEngine.cpp:3004-3054 with `excludeApps_` / `tsfApps_` / `macroEnabled_` field reads replaced by function params, and the final `configSnapshot_.store(...)` removed (caller publishes).

```cpp
// src/core/config/ConfigSnapshotBuilder.cpp
#include "core/config/ConfigSnapshotBuilder.h"

#include <unordered_map>
#include <unordered_set>
#include <string>
#include <utility>

#include "core/config/ConfigManager.h"
#include "core/config/ConfigSnapshot.h"
#include "core/config/TypingConfig.h"

namespace NextKey::ConfigSnapshotBuilder {

std::shared_ptr<const ConfigSnapshot> BuildFromToml(
    const std::filesystem::path& configPath,
    bool excludeAppsEnabled,
    bool tsfAppsEnabled,
    bool macroEnabled,
    std::uint32_t generation) {

    // App overrides — one TOML pass into 3 typed maps.
    auto overrides = ConfigManager::LoadAppOverrides(configPath);
    std::unordered_map<std::wstring, CodeTable>    encOv;
    std::unordered_map<std::wstring, InputMethod>  imOv;
    std::unordered_map<std::wstring, std::int8_t>  sendOv;
    for (auto& [exe, entry] : overrides) {
        if (entry.encodingOverride >= 0)
            encOv.emplace(exe, static_cast<CodeTable>(entry.encodingOverride));
        if (entry.inputMethod >= 0)
            imOv.emplace(exe, static_cast<InputMethod>(entry.inputMethod));
        if (entry.sendMethod >= 0)
            sendOv.emplace(exe, entry.sendMethod);
    }

    std::unordered_set<std::wstring> excluded;
    if (excludeAppsEnabled) {
        for (auto& app : ConfigManager::LoadAllExcludedApps(configPath))
            excluded.insert(std::move(app));
    }

    std::unordered_set<std::wstring> tsf;
    if (tsfAppsEnabled) {
        for (auto& app : ConfigManager::LoadTsfApps(configPath))
            tsf.insert(std::move(app));
    }

    std::unordered_map<std::wstring, std::wstring> macros;
    if (macroEnabled) {
        macros = ConfigManager::LoadMacros(configPath);
    }

    return std::make_shared<const ConfigSnapshot>(ConfigSnapshot::Build(
        std::move(macros),
        std::move(excluded),
        std::move(tsf),
        std::move(encOv),
        std::move(imOv),
        std::move(sendOv),
        generation));
}

}  // namespace NextKey::ConfigSnapshotBuilder
```

- [ ] **Step 2: Wire into CMake — production targets**

Edit `CMakeLists.txt`. Find both `NextKeyApp` and `NextKeyLite` source lists (both must include the new file — missing one causes `LNK2019` per Rule 11).

Append to both:
```cmake
    src/core/config/ConfigSnapshotBuilder.cpp
```

- [ ] **Step 3: Run test — confirm green**

```bash
cmake --build build-linux --target VKeyTests && ./build-linux/tests/VKeyTests --gtest_filter="ConfigSnapshotBuilderTest.*" 2>&1 | tail -15
```
Expected: 4 PASS.

### Task 1.4: Switch HookEngine to call the lifted function

**Files:**
- Modify: `src/app/system/HookEngine.cpp:3004-3055`

- [ ] **Step 1: Add include**

In `HookEngine.cpp` includes section (group: project headers), add:
```cpp
#include "core/config/ConfigSnapshotBuilder.h"
```

- [ ] **Step 2: Replace function body**

Find the existing function (lines 3004-3055):
```cpp
void HookEngine::RebuildSnapshotFromToml(std::uint32_t generation) {
    const auto configPath = ConfigManager::GetConfigPath();
    // ... 50 LOC of TOML→snapshot building ...
    configSnapshot_.store(std::move(snap), std::memory_order_release);
}
```

Replace the entire body with:
```cpp
void HookEngine::RebuildSnapshotFromToml(std::uint32_t generation) {
    // Side-effect: when excludeApps is off, clear the cached "currently in
    // excluded app" flag so a flag-disable picks up on the next focus check.
    // This is HookEngine runtime state, not snapshot data — keep here, not in
    // ConfigSnapshotBuilder.
    if (!excludeApps_) {
        isExcludedApp_.store(false, std::memory_order_release);
    }

    auto snap = ConfigSnapshotBuilder::BuildFromToml(
        ConfigManager::GetConfigPath(),
        excludeApps_,
        tsfApps_,
        macroEnabled_.load(std::memory_order_acquire),
        generation);
    configSnapshot_.store(std::move(snap), std::memory_order_release);
}
```

- [ ] **Step 3: Build + Linux GTest**

```bash
cmake --build build-linux --target VKeyTests && ./build-linux/tests/VKeyTests 2>&1 | tail -5
```
Expected: all tests pass, count = pre-Phase-1 baseline + 4 (the new ConfigSnapshotBuilder tests).

- [ ] **Step 4: Verify behavior unchanged via diff inspection**

Run: `git diff src/app/system/HookEngine.cpp src/app/system/HookEngine.h`
Eyeball: confirm the only HookEngine change is the function-body replacement + include line. No other state touched.

- [ ] **Step 5: Commit Phase 1**

```bash
git add src/core/config/ConfigSnapshotBuilder.h src/core/config/ConfigSnapshotBuilder.cpp \
        tests/core/config/ConfigSnapshotBuilderTest.cpp \
        src/app/system/HookEngine.cpp CMakeLists.txt
git commit -m "$(cat <<'EOF'
refactor(config): lift RebuildSnapshotFromToml body to ConfigSnapshotBuilder

Pure free function in src/core/config/. Linux-testable seam for TOML→snapshot
building. Zero behavior change — HookEngine keeps the isExcludedApp_ side-
effect (runtime state, not snapshot data).

+4 Linux GTest. Validates: empty config, excluded-apps gating, macro gating,
generation propagation.

Probe slice per docs/plans/2026-05-22-hookengine-degod-probe.md. Phase 2 GATE
decides whether to lift more (e.g. full ConfigApplier class) based on outcome.
EOF
)"
```

### Task 1.5: Windows verification (USER-RUN)

- [ ] **Step 1: Windows build**

User runs:
```bash
powershell.exe -Command "cd '\\wsl.localhost\Ubuntu-24.04\home\phatmt\code\NexusKey\build'; cmake --build . --target VKeyApp --config Debug 2>&1"
```
Expected: clean build, no `LNK2019` (catches missed CMake target).

- [ ] **Step 2: Chaos smoke**

User runs Notepad + Chrome chaos. Both must show no regression vs Main baseline.

- [ ] **Step 3: User reports back**

If PASS → Phase 1 lands; proceed to Phase 2 GATE. If FAIL → triage; do NOT push.

---

## Phase 2 — GATE (decision point, not auto-execute)

**This phase is NOT a coding task.** It is a decision checkpoint.

### Task 2.1: Score Phase 1 outcome

- [ ] **Step 1: Measure outcomes against pre-defined success criteria**

Answer each question with concrete data:

1. **Did the extraction add real test coverage?**
   - Before: 0 unit tests for TOML→snapshot.
   - After: 4 unit tests.
   - Pass criteria: ≥ 3 new tests catch a bug that previously required Windows chaos. → If pass, ✅
   - Or alternatively: Did writing the tests reveal any latent bug (silent-empty when feature off, etc.)? → If pass, ✅

2. **HookEngine LOC delta?**
   - Pre-Phase-1: 4425 LOC (post-Phase-0; Phase 0 is zero-LOC-delta in body).
   - Post-Phase-1 target: ~4380 LOC (~45 LOC out, given the function body shrank from 52 LOC to ~7 LOC + comments).
   - Pass criteria: net reduction ≥ 35 LOC. → Measure actual.

3. **Was the seam clean or did it require ugly workarounds?**
   - Subjective: did the caller-keeps-side-effect pattern feel forced, or natural?
   - Pass criteria: extract did NOT require new accessors / callbacks on HookEngine just to make ConfigSnapshotBuilder work.

4. **Did chaos PASS on Windows?**
   - Pass criteria: Notepad + Chrome chaos pass at baseline rate.

5. **How long did Phase 1 actually take?**
   - Pass criteria: ≤ 1 active day (incl. test writing).
   - Reason: if extracting a 52-LOC self-contained function takes > 1 day, larger extracts are not worth the cost.

- [ ] **Step 2: Apply the decision matrix**

| Outcome | Decision |
|---|---|
| All 5 criteria pass | **Proceed** — write Phase 3 plan to lift `QuickSyncFromSharedState` diff logic as `ConfigDiffer` pure function. Same probe shape; if that also passes, only then consider ConfigApplier as a class. |
| 3-4 criteria pass | **Pause** — land Phase 1, but do NOT plan Phase 3. Revisit at next milestone. The probe paid off but not enough to justify further extraction this cycle. |
| ≤ 2 criteria pass | **Stop** — keep Phase 0 (atomic enum) since it's pure win. Revert Phase 1 if it added friction; or land it but explicitly close the "ConfigApplier extract" line in REFACTOR_STATUS as wontfix. Anh's "không phân mảnh trừ khi hiệu quả" rule decides. |

- [ ] **Step 3: Record decision in REFACTOR_STATUS**

Edit `docs/REFACTOR_STATUS.md`. Append to Section G (Recommended next steps):

```markdown
**De-god probe outcome (2026-05-22 plan):**
- Phase 0 (atomic CodeTable/InputMethod): SHIPPED — commit <SHA>
- Phase 1 (ConfigSnapshotBuilder lift): <PROCEED / PAUSE / STOP> per gate criteria above. <link to commit>
- Phase 3+ (further ConfigApplier extraction): <NOT PLANNED / SCHEDULED / WONTFIX>
```

- [ ] **Step 4: Commit the decision record**

```bash
git add docs/REFACTOR_STATUS.md
git commit -m "docs(refactor): record de-god probe outcome — Phase 1 <verdict>"
```

---

## Test Plan Summary

| Phase | Test gate | Where |
|---|---|---|
| 0 | Linux GTest baseline preserved | `./build-linux/tests/VKeyTests` |
| 0 | Windows MSVC clean build | user-run |
| 0 | Notepad + Chrome chaos PASS | user-run |
| 1 | 4 new ConfigSnapshotBuilder tests PASS | `./build-linux/tests/VKeyTests --gtest_filter="ConfigSnapshotBuilderTest.*"` |
| 1 | Linux GTest baseline + 4 | `./build-linux/tests/VKeyTests` |
| 1 | Windows MSVC clean build | user-run |
| 1 | Notepad + Chrome chaos PASS | user-run |

---

## Rollback Plan

- **Phase 0**: single commit, revert via `git revert <SHA>`. Atomic enum is reversible — the only side-effect is a tiny code-size delta from atomic ops.
- **Phase 1**: single commit, revert via `git revert <SHA>`. New `src/core/config/ConfigSnapshotBuilder.{h,cpp}` files are deleted by the revert; test file orphaned but compiles (nothing references it after revert) — clean up manually if revert sticks.

---

## Glossary (in-codebase term definitions for newcomer executors)

- **Hook hot path**: code reachable from `LowLevelKeyboardProc` callback; must satisfy Rule 11 (no mutex, no malloc, no syscalls beyond `GetKeyState`/`SendInput`). Per-callback budget < 30ms p99.
- **Worker thread**: `MainThreadWorker` background thread that handles deferred work — TOML reload, SharedState diffing — off the hook hot path. Ticks every 200ms.
- **RCU pattern**: `std::atomic<std::shared_ptr<const T>>`. Writers create new T, atomic-store the new shared_ptr. Readers atomic-load → read → drop. Old T stays alive until last reader drops its load. Lock-free.
- **Snapshot**: immutable `ConfigSnapshot` struct (excluded apps, TSF apps, macros, overrides) published via RCU. Hot path readers grab a shared_ptr load and never see partial writes.
- **chaos.toml**: stress test corpus simulating 1ms-paced keystroke sequences across multiple Vietnamese typing patterns and host apps. Located in repo (`tools/chaos.toml` or similar). Run via Windows `run-chaos.ps1`.

---

## Self-Review checks

- ✅ Phase 0 covers all known writer + reader sites of the 3 enum fields (grep commands enforce).
- ✅ Phase 1 lifts a 52-LOC self-contained function — no field cross-cutting issues.
- ✅ Phase 1 preserves the `isExcludedApp_.store(false)` side-effect by keeping it in HookEngine.
- ✅ Both phases have explicit Windows gate (user-run) before merging.
- ✅ Phase 2 is a GATE not a coding task — does not pre-commit to work that may not be warranted.
- ✅ Rollback is single-`git revert` for each phase.
- ✅ CMake updates land in same commit as new sources (Rule 11.4 — both `NextKeyApp` + `NextKeyLite`).
- ✅ MEMORY.md refresh task included (Task 0.7) — addresses 17-day-stale state surfaced during planning.
- ✅ No placeholder steps. All code blocks contain actual content.
