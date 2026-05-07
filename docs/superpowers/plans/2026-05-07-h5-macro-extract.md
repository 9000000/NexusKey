# H5 Macro Extract Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Extract `HookEngine::TryExpandMacro` decision logic (217 LOC at `src/app/system/HookEngine.cpp:3205-3422`) into a Linux-portable, gtest-covered module at `src/core/MacroCase.h`/`.cpp`, with a Win32 case-mapper at `src/app/system/Win32CaseMapper.h`. Behavior must be byte-identical to Main `3257758`.

**Architecture:** Three free helpers (`Macro::Plan`, `Macro::ExpandEscapesForClipboard`, `Macro::BuildSegments`) consolidate all `\n` escape semantics in one Linux-portable file. A `Macro::CaseMapper` abstract class is dependency-injected — production passes `Win32CaseMapper` (wraps `CharUpperBuffW`/`CharLowerBuffW`); tests pass `AsciiCaseMapper`. `HookEngine::TryExpandMacro` shrinks to a ~50 LOC orchestrator that builds the input struct, calls `Plan`, and dispatches via `IOutputInjector` + `ClipboardPaste`.

**Tech Stack:** C++20, GoogleTest 1.14, CMake 3.20+. Linux build via `build-linux/` (gtest only); Windows MSVC build via WSL→PowerShell against `build/`.

**Spec:** `docs/superpowers/specs/2026-05-07-h5-macro-extract-design.md` (commit `ebbdd04`).

**Branch:** `refactor/h5-macro-extract` (off Main `3257758` or later).

---

## File Structure

| File | Action | Responsibility |
|---|---|---|
| `src/core/MacroCase.h` | **Create** | Public interface: `CaseMapper`, `PlanInputs`, `MacroPlan`, `Segment` types + `Plan`/`ExpandEscapesForClipboard`/`BuildSegments` declarations. Linux-portable, zero Win32 deps. |
| `src/core/MacroCase.cpp` | **Create** | Implementation of all three free helpers + file-static `LowerCopy` (3-line `towlower` loop). |
| `src/app/system/Win32CaseMapper.h` | **Create** | Header-only `Win32CaseMapper` class wrapping `CharUpperBuffW`/`CharLowerBuffW`. |
| `tests/MacroCaseTest.cpp` | **Create** | 36 gtest cases across 9 test suites + `AsciiCaseMapper` fixture. |
| `src/app/system/HookEngine.cpp` | **Modify lines 3205-3422** | Replace 217 LOC `TryExpandMacro` body with ~50 LOC orchestrator. Add `#include "core/MacroCase.h"` and `#include "system/Win32CaseMapper.h"`. |
| `CMakeLists.txt` | **Modify lines 53-61, 419-441** | Add `src/core/MacroCase.h`/`.cpp` to `NEXTKEY_CORE_SOURCES`; add `tests/MacroCaseTest.cpp` to `NEXTKEY_TEST_SOURCES`. |

**Final test count:** 1,477 (existing) + 36 (new) = **1,513**.

---

## Task 0: Branch setup and baseline

**Files:** none (git operations only)

- [ ] **Step 1: Confirm clean Main**

```bash
cd /home/phatmt/code/NexusKey
git status --short
git log --oneline -1
```
Expected: working tree may have untracked perf-channeltraits-*.csv / report-channeltraits-*.xml from prior chaos runs (pre-existing, untouched). HEAD at `3257758` or later. No uncommitted changes to tracked files except `tools/run-chaos.ps1` (pre-existing).

- [ ] **Step 2: Create branch off Main**

```bash
git checkout -b refactor/h5-macro-extract
git push -u origin refactor/h5-macro-extract
```
Expected: branch created and pushed.

- [ ] **Step 3: Run baseline test suite to confirm green start**

```bash
cmake -B build-linux -G "Unix Makefiles" -DCMAKE_BUILD_TYPE=Debug
cmake --build build-linux --target NextKeyTests -j
./build-linux/tests/NextKeyTests --gtest_brief=1
```
Expected: `[==========] 1477 tests from N test suites ran. ... [  PASSED  ] 1477 tests.`

- [ ] **Step 4: Note baseline LOC for verification later**

```bash
wc -l src/app/system/HookEngine.cpp
```
Expected: `3582 src/app/system/HookEngine.cpp` (matches `docs/REFACTOR_STATUS.md` snapshot).

---

## Task 1: Create `core/MacroCase.h` skeleton + register in CMake

**Files:**
- Create: `src/core/MacroCase.h`
- Create: `src/core/MacroCase.cpp` (empty stub)
- Create: `tests/MacroCaseTest.cpp` (empty stub with `AsciiCaseMapper`)
- Modify: `CMakeLists.txt`

- [ ] **Step 1: Create `src/core/MacroCase.h`**

```cpp
// NexusKey - Macro expansion decision logic
// Copyright (c) 2024-2026 PhatMT. All rights reserved.
// SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-NexusKey-Commercial
//
// Linux-portable macro expansion decision unit. Extracted from
// HookEngine::TryExpandMacro for unit testability. Win32 case-mapping
// dependency is injected via the CaseMapper interface.

#pragma once

#include "core/config/TypingConfig.h"   // CodeTable enum

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace NextKey::Macro {

/// DI seam for locale-aware uppercase / lowercase. Production wraps
/// CharUpperBuffW / CharLowerBuffW; tests use ASCII-only mappers.
class CaseMapper {
public:
    virtual ~CaseMapper() = default;
    virtual void Upper(wchar_t* buf, std::size_t n) const = 0;
    virtual void Lower(wchar_t* buf, std::size_t n) const = 0;
};

struct PlanInputs {
    const std::wstring& rawMacroBuffer;
    const std::wstring& previousComposition;
    const std::vector<uint8_t>& previousEncodedWidths;
    const std::unordered_map<std::wstring, std::wstring>& macroTable;
    bool macroCrossCommit;
    CodeTable currentCodeTable;
    bool autoCapsEnabled;
    wchar_t triggerChar;
    std::size_t clipboardThreshold;     // = HookEngine kMacroClipboardThreshold (200)
};

struct MacroPlan {
    bool matched{false};
    bool isPartOfMacro{false};          // → ExpandedEatTrigger vs ExpandedPassTrigger
    std::size_t bsCount{0};
    std::wstring expansion;             // post auto-caps; \n escapes still literal
    bool useClipboard{false};
};

/// Decide what (if anything) to expand. Pure function: no I/O, no globals.
[[nodiscard]] MacroPlan Plan(const PlanInputs& in, const CaseMapper& mapper);

/// Convert \n escapes into \r\n for clipboard paste. No other escapes processed.
[[nodiscard]] std::wstring ExpandEscapesForClipboard(std::wstring_view expansion);

/// Single dispatch fragment: either text or a VK_RETURN marker.
struct Segment {
    bool isReturn{false};
    std::wstring text;                  // empty when isReturn == true
};

/// Split expansion at \n boundaries and run per-character ConvertChar
/// for non-Unicode code tables. Returns segments interleaved with returns.
[[nodiscard]] std::vector<Segment> BuildSegments(std::wstring_view expansion,
                                                 CodeTable codeTable);

}  // namespace NextKey::Macro
```

- [ ] **Step 2: Create `src/core/MacroCase.cpp` stub**

```cpp
// NexusKey - Macro expansion decision logic implementation
// Copyright (c) 2024-2026 PhatMT. All rights reserved.
// SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-NexusKey-Commercial

#include "core/MacroCase.h"

#include "core/engine/CodeTableConverter.h"

#include <cwctype>

namespace NextKey::Macro {

namespace {
[[nodiscard]] std::wstring LowerCopy(std::wstring s) {
    for (auto& c : s) c = static_cast<wchar_t>(std::towlower(c));
    return s;
}
}  // namespace

MacroPlan Plan(const PlanInputs& in, const CaseMapper& mapper) {
    (void)in; (void)mapper; (void)LowerCopy;
    return {};   // stub — implemented in Task 4
}

std::wstring ExpandEscapesForClipboard(std::wstring_view expansion) {
    (void)expansion;
    return {};   // stub — implemented in Task 2
}

std::vector<Segment> BuildSegments(std::wstring_view expansion, CodeTable codeTable) {
    (void)expansion; (void)codeTable;
    return {};   // stub — implemented in Task 3
}

}  // namespace NextKey::Macro
```

- [ ] **Step 3: Create `tests/MacroCaseTest.cpp` stub with `AsciiCaseMapper`**

```cpp
// NexusKey - Macro expansion decision unit tests
// SPDX-License-Identifier: GPL-3.0-only

#include <gtest/gtest.h>
#include "core/MacroCase.h"

namespace NextKey::Macro {
namespace {

struct AsciiCaseMapper final : CaseMapper {
    void Upper(wchar_t* buf, std::size_t n) const override {
        for (std::size_t i = 0; i < n; ++i)
            if (buf[i] >= L'a' && buf[i] <= L'z') buf[i] = buf[i] - L'a' + L'A';
    }
    void Lower(wchar_t* buf, std::size_t n) const override {
        for (std::size_t i = 0; i < n; ++i)
            if (buf[i] >= L'A' && buf[i] <= L'Z') buf[i] = buf[i] - L'A' + L'a';
    }
};

TEST(MacroCaseSmokeTest, StubLinks) {
    AsciiCaseMapper mapper;
    std::wstring raw, prev;
    std::vector<uint8_t> widths;
    std::unordered_map<std::wstring, std::wstring> table;
    PlanInputs in{raw, prev, widths, table, false, CodeTable::Unicode, false, L' ', 200};
    auto plan = Plan(in, mapper);
    EXPECT_FALSE(plan.matched);   // stub returns default-constructed MacroPlan
}

}  // namespace
}  // namespace NextKey::Macro
```

- [ ] **Step 4: Add `MacroCase.h`/`.cpp` to `NEXTKEY_CORE_SOURCES`**

Edit `CMakeLists.txt` between lines 57-61. Replace:

```cmake
    src/core/Strings.h
    src/core/Strings.cpp
    src/core/ipc/SharedState.h
    src/core/ipc/SharedConstants.h
)
```

with:

```cmake
    src/core/Strings.h
    src/core/Strings.cpp
    src/core/MacroCase.h
    src/core/MacroCase.cpp
    src/core/ipc/SharedState.h
    src/core/ipc/SharedConstants.h
)
```

- [ ] **Step 5: Add `MacroCaseTest.cpp` to `NEXTKEY_TEST_SOURCES`**

Edit `CMakeLists.txt` line 435. Replace:

```cmake
    tests/MacroPrefixTest.cpp
```

with:

```cmake
    tests/MacroPrefixTest.cpp
    tests/MacroCaseTest.cpp
```

- [ ] **Step 6: Verify Linux build compiles**

```bash
cmake --build build-linux --target NextKeyTests -j 2>&1 | tail -10
./build-linux/tests/NextKeyTests --gtest_filter="MacroCaseSmokeTest.*"
```
Expected: build succeeds; one test runs; `[  PASSED  ] 1 test.`

- [ ] **Step 7: Commit**

```bash
git add src/core/MacroCase.h src/core/MacroCase.cpp tests/MacroCaseTest.cpp CMakeLists.txt
git commit -m "refactor(H5): scaffold core/MacroCase + Win32CaseMapper interface

Add empty Plan/ExpandEscapesForClipboard/BuildSegments stubs and the
CaseMapper abstract class. AsciiCaseMapper test fixture in place.
CMake registration added (NEXTKEY_CORE_SOURCES + NEXTKEY_TEST_SOURCES).
Smoke test confirms linkage. No behavior change yet."
```

---

## Task 2: Implement `Macro::ExpandEscapesForClipboard` + 5 tests

**Files:**
- Modify: `src/core/MacroCase.cpp` (replace `ExpandEscapesForClipboard` stub)
- Modify: `tests/MacroCaseTest.cpp` (add `ClipboardEscapesTest` suite)

- [ ] **Step 1: Write the failing tests**

Append to `tests/MacroCaseTest.cpp` after the `MacroCaseSmokeTest` (still inside the anonymous namespace):

```cpp
TEST(ClipboardEscapesTest, EmptyInput) {
    EXPECT_EQ(ExpandEscapesForClipboard(L""), L"");
}

TEST(ClipboardEscapesTest, NoEscapes) {
    EXPECT_EQ(ExpandEscapesForClipboard(L"hello world"), L"hello world");
}

TEST(ClipboardEscapesTest, SingleNewlineEscape) {
    EXPECT_EQ(ExpandEscapesForClipboard(L"line1\\nline2"), L"line1\r\nline2");
}

TEST(ClipboardEscapesTest, MultipleNewlinesEscape) {
    EXPECT_EQ(ExpandEscapesForClipboard(L"a\\nb\\nc"), L"a\r\nb\r\nc");
}

TEST(ClipboardEscapesTest, LiteralBackslashFollowedByNonN) {
    // \\t is NOT an escape — only \n is recognized. \t passes through verbatim.
    EXPECT_EQ(ExpandEscapesForClipboard(L"a\\tb"), L"a\\tb");
}
```

- [ ] **Step 2: Run tests to verify they fail**

```bash
cmake --build build-linux --target NextKeyTests -j 2>&1 | tail -5
./build-linux/tests/NextKeyTests --gtest_filter="ClipboardEscapesTest.*"
```
Expected: 4 of 5 fail (`EmptyInput` happens to pass because stub returns `L""`).

- [ ] **Step 3: Implement `ExpandEscapesForClipboard`**

In `src/core/MacroCase.cpp`, replace the stub with:

```cpp
std::wstring ExpandEscapesForClipboard(std::wstring_view expansion) {
    std::wstring out;
    out.reserve(expansion.size());
    for (std::size_t i = 0; i < expansion.size(); ++i) {
        if (expansion[i] == L'\\' && i + 1 < expansion.size() && expansion[i + 1] == L'n') {
            out += L"\r\n";
            ++i;
        } else {
            out += expansion[i];
        }
    }
    return out;
}
```

- [ ] **Step 4: Run tests to verify pass**

```bash
cmake --build build-linux --target NextKeyTests -j 2>&1 | tail -3
./build-linux/tests/NextKeyTests --gtest_filter="ClipboardEscapesTest.*"
```
Expected: `[  PASSED  ] 5 tests.`

- [ ] **Step 5: Commit**

```bash
git add src/core/MacroCase.cpp tests/MacroCaseTest.cpp
git commit -m "refactor(H5): implement ExpandEscapesForClipboard + 5 tests

Pure single-pass loop; \\n becomes \\r\\n, all other chars pass through
verbatim. Linux-only verification — no Windows code touched."
```

---

## Task 3: Implement `Macro::BuildSegments` + 6 tests

**Files:**
- Modify: `src/core/MacroCase.cpp`
- Modify: `tests/MacroCaseTest.cpp`

- [ ] **Step 1: Write failing tests**

Append to `tests/MacroCaseTest.cpp`:

```cpp
TEST(BuildSegmentsTest, EmptyExpansion) {
    auto segs = BuildSegments(L"", CodeTable::Unicode);
    EXPECT_TRUE(segs.empty());
}

TEST(BuildSegmentsTest, PureTextNoNewline) {
    auto segs = BuildSegments(L"hello", CodeTable::Unicode);
    ASSERT_EQ(segs.size(), 1u);
    EXPECT_FALSE(segs[0].isReturn);
    EXPECT_EQ(segs[0].text, L"hello");
}

TEST(BuildSegmentsTest, TextNewlineText) {
    auto segs = BuildSegments(L"line1\\nline2", CodeTable::Unicode);
    ASSERT_EQ(segs.size(), 3u);
    EXPECT_FALSE(segs[0].isReturn); EXPECT_EQ(segs[0].text, L"line1");
    EXPECT_TRUE(segs[1].isReturn);  EXPECT_EQ(segs[1].text, L"");
    EXPECT_FALSE(segs[2].isReturn); EXPECT_EQ(segs[2].text, L"line2");
}

TEST(BuildSegmentsTest, LeadingNewline) {
    auto segs = BuildSegments(L"\\nhello", CodeTable::Unicode);
    ASSERT_EQ(segs.size(), 2u);
    EXPECT_TRUE(segs[0].isReturn);
    EXPECT_FALSE(segs[1].isReturn); EXPECT_EQ(segs[1].text, L"hello");
}

TEST(BuildSegmentsTest, NonUnicodeSingleByteEncoding) {
    // 'à' (U+00E0) under TCVN3 maps to 0xB5 (single byte).
    // Verify ConvertChar is invoked and result has the encoded byte, not 'à'.
    auto segs = BuildSegments(L"à", CodeTable::TCVN3);
    ASSERT_EQ(segs.size(), 1u);
    EXPECT_FALSE(segs[0].isReturn);
    ASSERT_EQ(segs[0].text.size(), 1u);
    EXPECT_NE(segs[0].text[0], L'à');               // must have been converted
    EXPECT_EQ(static_cast<uint16_t>(segs[0].text[0]), 0x00B5);
}

TEST(BuildSegmentsTest, NonUnicodeMultiUnitEncoding) {
    // 'á' (U+00E1) under VNIWindows maps to encoded value 0xF961 → 2 units:
    // units[0] = LOBYTE = 0x61 ('a'), units[1] = HIBYTE = 0xF9 (tone marker).
    // Both units must be appended to the segment text in order.
    auto segs = BuildSegments(L"á", CodeTable::VNIWindows);
    ASSERT_EQ(segs.size(), 1u);
    EXPECT_FALSE(segs[0].isReturn);
    ASSERT_EQ(segs[0].text.size(), 2u);
    EXPECT_EQ(static_cast<uint16_t>(segs[0].text[0]), 0x0061);
    EXPECT_EQ(static_cast<uint16_t>(segs[0].text[1]), 0x00F9);
}
```

- [ ] **Step 2: Run tests to verify failure**

```bash
cmake --build build-linux --target NextKeyTests -j 2>&1 | tail -3
./build-linux/tests/NextKeyTests --gtest_filter="BuildSegmentsTest.*"
```
Expected: 5 of 6 fail (`EmptyExpansion` passes because stub returns `{}`).

- [ ] **Step 3: Implement `BuildSegments`**

Replace the stub in `src/core/MacroCase.cpp`:

```cpp
std::vector<Segment> BuildSegments(std::wstring_view expansion, CodeTable codeTable) {
    std::vector<Segment> segments;
    Segment cur{};
    auto flush = [&]() {
        if (!cur.text.empty()) { segments.push_back(std::move(cur)); cur = {}; }
    };
    for (std::size_t i = 0; i < expansion.size(); ++i) {
        if (expansion[i] == L'\\' && i + 1 < expansion.size() && expansion[i + 1] == L'n') {
            flush();
            segments.push_back(Segment{true, {}});
            ++i;
            continue;
        }
        if (codeTable != CodeTable::Unicode) {
            auto enc = CodeTableConverter::ConvertChar(expansion[i], codeTable);
            cur.text += enc.units[0];
            if (enc.count == 2) cur.text += enc.units[1];
        } else {
            cur.text += expansion[i];
        }
    }
    flush();
    return segments;
}
```

- [ ] **Step 4: Run tests to verify pass**

```bash
cmake --build build-linux --target NextKeyTests -j 2>&1 | tail -3
./build-linux/tests/NextKeyTests --gtest_filter="BuildSegmentsTest.*"
```
Expected: `[  PASSED  ] 6 tests.`

- [ ] **Step 5: Verify TCVN3 byte expectation**

If the TCVN3 byte assertion in `NonUnicodeAppliesConvertChar` fails, do NOT loosen the assertion — investigate `CodeTableConverter::ConvertChar(L'à', CodeTable::TCVN3)` separately. The byte `0xB5` is per the existing TCVN3 encoding table; if it's wrong, fix the test value to match `ConvertChar` behavior, not the converter.

- [ ] **Step 6: Commit**

```bash
git add src/core/MacroCase.cpp tests/MacroCaseTest.cpp
git commit -m "refactor(H5): implement BuildSegments + 6 tests

Splits expansion at \\n boundaries into ordered (text|return) segments.
Non-Unicode code tables run per-char ConvertChar. Empty-expansion edge
case returns empty vector; leading/trailing newline edge cases
covered. Linux-only verification."
```

---

## Task 4: Implement `Macro::Plan` (lift-and-shift) + 22 tests

**Files:**
- Modify: `src/core/MacroCase.cpp` (replace `Plan` stub)
- Modify: `tests/MacroCaseTest.cpp` (add 4 test suites)

`Plan` is a near line-for-line port of `HookEngine.cpp:3206-3331`. We lift the impl first, then write tests against it.

### Task 4a: Port `Plan` implementation

- [ ] **Step 1: Replace `Plan` stub**

In `src/core/MacroCase.cpp`, replace the `Plan` stub with the full impl below. This is a direct port of `HookEngine::TryExpandMacro` lines 3206-3331 with member accesses (`rawMacroBuffer_`, etc.) replaced by `in.<field>`, and `CharUpperBuffW`/`CharLowerBuffW` replaced by `mapper.Upper`/`mapper.Lower`:

```cpp
MacroPlan Plan(const PlanInputs& in, const CaseMapper& mapper) {
    MacroPlan plan{};

    std::wstring lowerKey = LowerCopy(in.rawMacroBuffer);
    bool matchedExact = false;
    bool matchedViaComposition = false;

    // Priority 1: full buffer (raw exact, fall back to lowered)
    auto it = in.macroTable.find(in.rawMacroBuffer);
    if (it != in.macroTable.end()) {
        matchedExact = true;
    } else {
        it = in.macroTable.find(lowerKey);
    }
    if (it != in.macroTable.end() && in.triggerChar > L' ') {
        plan.isPartOfMacro = true;
    }

    // Priority 2: buffer without trigger char (e.g. "btw" from "btw.")
    if (it == in.macroTable.end() && in.triggerChar > L' ' &&
        lowerKey.size() > 1 && lowerKey.back() == in.triggerChar) {
        std::wstring rawWithoutTrigger =
            in.rawMacroBuffer.substr(0, in.rawMacroBuffer.size() - 1);
        it = in.macroTable.find(rawWithoutTrigger);
        if (it != in.macroTable.end()) {
            matchedExact = true;
        } else {
            it = in.macroTable.find(lowerKey.substr(0, lowerKey.size() - 1));
        }
    }

    // Priority 3/4: composition-based matches stay case-insensitive only.
    if (it == in.macroTable.end() && !in.previousComposition.empty()) {
        std::wstring compKey = LowerCopy(in.previousComposition);
        if (in.triggerChar > L' ') {
            it = in.macroTable.find(compKey + in.triggerChar);
            if (it != in.macroTable.end()) {
                plan.isPartOfMacro = true;
                matchedViaComposition = true;
            }
        }
        if (it == in.macroTable.end()) {
            it = in.macroTable.find(compKey);
            if (it != in.macroTable.end()) matchedViaComposition = true;
        }
    }
    if (it == in.macroTable.end()) return plan;   // matched: false
    plan.matched = true;

    // Backspace count = on-screen characters that need erasing.
    if (matchedViaComposition) {
        if (in.currentCodeTable != CodeTable::Unicode) {
            plan.bsCount = 0;
            for (auto w : in.previousEncodedWidths) plan.bsCount += w;
        } else {
            plan.bsCount = in.previousComposition.size();
        }
    } else if (in.macroCrossCommit) {
        plan.bsCount = in.rawMacroBuffer.size();
        if (in.triggerChar > L' ' && plan.bsCount > 0) --plan.bsCount;
    } else if (!in.previousComposition.empty()) {
        if (in.currentCodeTable != CodeTable::Unicode) {
            plan.bsCount = 0;
            for (auto w : in.previousEncodedWidths) plan.bsCount += w;
        } else {
            plan.bsCount = in.previousComposition.size();
        }
    } else {
        plan.bsCount = in.rawMacroBuffer.size();
        if (in.triggerChar > L' ' && plan.bsCount > 0) --plan.bsCount;
    }

    // Auto-capitalize expansion to match typed case.
    plan.expansion = it->second;
    if (in.autoCapsEnabled && !matchedExact && !matchedViaComposition &&
        !in.rawMacroBuffer.empty() && !plan.expansion.empty()) {
        std::wstring expansionLower = plan.expansion;
        if (!expansionLower.empty()) {
            mapper.Lower(expansionLower.data(), expansionLower.size());
        }
        const bool expansionAllLower = (expansionLower == plan.expansion);
        if (expansionAllLower) {
            bool allUpper = true;
            bool anyAlpha = false;
            for (auto c : in.rawMacroBuffer) {
                if (!std::iswalpha(c)) continue;
                anyAlpha = true;
                if (!std::iswupper(c)) { allUpper = false; break; }
            }
            allUpper = allUpper && anyAlpha && in.rawMacroBuffer.size() > 1;
            const bool firstUpper = std::iswupper(in.rawMacroBuffer[0]) != 0;
            if (allUpper) {
                // Skip \n escape: uppercasing 'n' breaks newline detection downstream.
                for (std::size_t i = 0; i < plan.expansion.size(); ++i) {
                    if (plan.expansion[i] == L'\\' && i + 1 < plan.expansion.size() &&
                        plan.expansion[i + 1] == L'n') {
                        ++i;
                    } else {
                        mapper.Upper(&plan.expansion[i], 1);
                    }
                }
            } else if (firstUpper) {
                mapper.Upper(&plan.expansion[0], 1);
            }
        }
    }

    // Clipboard threshold: Unicode + size > threshold.
    plan.useClipboard = (in.currentCodeTable == CodeTable::Unicode &&
                         plan.expansion.size() > in.clipboardThreshold);

    return plan;
}
```

- [ ] **Step 2: Build to verify it compiles**

```bash
cmake --build build-linux --target NextKeyTests -j 2>&1 | tail -5
```
Expected: build succeeds. (Smoke test from Task 1 may now fail because `Plan` returns non-default values for some inputs — that's OK; we'll delete the smoke test in Step 3 of the next sub-task.)

### Task 4b: Match-priorities tests

- [ ] **Step 1: Replace smoke test, add helpers and 9 match tests**

Edit `tests/MacroCaseTest.cpp`. Replace the `MacroCaseSmokeTest` block with the helpers and tests below. Keep the `AsciiCaseMapper` declaration above:

```cpp
namespace {

// Build a default-everything PlanInputs given just the things the test cares about.
struct PlanFixture {
    AsciiCaseMapper mapper;
    std::wstring raw;
    std::wstring prevComp;
    std::vector<uint8_t> widths;
    std::unordered_map<std::wstring, std::wstring> table;
    bool crossCommit = false;
    CodeTable codeTable = CodeTable::Unicode;
    bool autoCaps = false;
    wchar_t trigger = L' ';
    std::size_t threshold = 200;

    [[nodiscard]] MacroPlan Run() const {
        PlanInputs in{raw, prevComp, widths, table, crossCommit, codeTable,
                      autoCaps, trigger, threshold};
        return Plan(in, mapper);
    }
};

TEST(PlanMatchPrioritiesTest, FullBufferExact) {
    PlanFixture f;
    f.table[L"BTW"] = L"by the way";
    f.raw = L"BTW";
    f.trigger = L' ';
    auto p = f.Run();
    EXPECT_TRUE(p.matched);
    EXPECT_EQ(p.expansion, L"by the way");
    EXPECT_FALSE(p.isPartOfMacro);   // trigger == L' ' is not "> L' '"
}

TEST(PlanMatchPrioritiesTest, FullBufferLowerFallback) {
    PlanFixture f;
    f.table[L"btw"] = L"by the way";
    f.raw = L"BTW";
    f.trigger = L'.';
    auto p = f.Run();
    // No exact match for "BTW.", lowered "btw." also misses.
    // Priority 2: try "BTW" exact (miss), then "btw" lower → hit.
    EXPECT_TRUE(p.matched);
    EXPECT_EQ(p.expansion, L"by the way");
}

TEST(PlanMatchPrioritiesTest, BufferWithoutTriggerExact) {
    PlanFixture f;
    f.table[L"BTW"] = L"by the way";
    f.raw = L"BTW.";
    f.trigger = L'.';
    auto p = f.Run();
    EXPECT_TRUE(p.matched);
    EXPECT_TRUE(p.isPartOfMacro);     // trigger '.' > L' '
}

TEST(PlanMatchPrioritiesTest, BufferWithoutTriggerLower) {
    PlanFixture f;
    f.table[L"btw"] = L"by the way";
    f.raw = L"btw.";
    f.trigger = L'.';
    auto p = f.Run();
    EXPECT_TRUE(p.matched);
    EXPECT_TRUE(p.isPartOfMacro);
}

TEST(PlanMatchPrioritiesTest, PrevCompositionWithTrigger) {
    PlanFixture f;
    f.table[L"chao."] = L"xin chao";
    f.raw = L".";
    f.prevComp = L"CHAO";
    f.trigger = L'.';
    auto p = f.Run();
    EXPECT_TRUE(p.matched);
    EXPECT_TRUE(p.isPartOfMacro);
}

TEST(PlanMatchPrioritiesTest, PrevCompositionWithoutTrigger) {
    PlanFixture f;
    f.table[L"chao"] = L"xin chao";
    f.raw = L"";
    f.prevComp = L"CHAO";
    f.trigger = L' ';
    auto p = f.Run();
    EXPECT_TRUE(p.matched);
    EXPECT_FALSE(p.isPartOfMacro);
}

TEST(PlanMatchPrioritiesTest, NoMatchReturnsFalse) {
    PlanFixture f;
    f.table[L"btw"] = L"by the way";
    f.raw = L"xyz";
    f.trigger = L' ';
    auto p = f.Run();
    EXPECT_FALSE(p.matched);
}

TEST(PlanStoredKeyCaseRuleTest, UppercaseKeyRejectsLowercaseTyping) {
    // Uppercase-key contract: only exact case matches.
    PlanFixture f;
    f.table[L"BTW"] = L"BY THE WAY";
    f.raw = L"btw";
    f.trigger = L' ';
    auto p = f.Run();
    EXPECT_FALSE(p.matched);
}

TEST(PlanStoredKeyCaseRuleTest, LowercaseKeyAcceptsAnyCase) {
    PlanFixture f;
    f.table[L"btw"] = L"by the way";
    for (auto* probe : {L"btw", L"BTW", L"Btw", L"bTw"}) {
        f.raw = probe;
        f.trigger = L' ';
        auto p = f.Run();
        EXPECT_TRUE(p.matched) << "probe = " << std::string(probe, probe + 3);
    }
}
```

- [ ] **Step 2: Build and run**

```bash
cmake --build build-linux --target NextKeyTests -j 2>&1 | tail -5
./build-linux/tests/NextKeyTests --gtest_filter="PlanMatchPrioritiesTest.*:PlanStoredKeyCaseRuleTest.*"
```
Expected: `[  PASSED  ] 9 tests.`

### Task 4c: BS-count tests

- [ ] **Step 1: Add 5 bs-count tests**

Append to `tests/MacroCaseTest.cpp` (still inside the anonymous namespace):

```cpp
TEST(PlanBsCountTest, MatchedViaCompositionUnicode) {
    PlanFixture f;
    f.table[L"chao"] = L"xin chao";
    f.prevComp = L"chao";
    f.raw = L"";
    f.trigger = L' ';
    f.codeTable = CodeTable::Unicode;
    auto p = f.Run();
    EXPECT_TRUE(p.matched);
    EXPECT_EQ(p.bsCount, 4u);
}

TEST(PlanBsCountTest, MatchedViaCompositionTcvn3Widths) {
    PlanFixture f;
    f.table[L"chao"] = L"xin chao";
    f.prevComp = L"chao";   // Unicode 4 chars
    f.raw = L"";
    f.trigger = L' ';
    f.codeTable = CodeTable::TCVN3;
    f.widths = {1, 1, 2, 1};   // 5 bytes total under encoding
    auto p = f.Run();
    EXPECT_TRUE(p.matched);
    EXPECT_EQ(p.bsCount, 5u);
}

TEST(PlanBsCountTest, CrossCommitWithTrigger) {
    PlanFixture f;
    f.table[L"a.i"] = L"artificial intelligence";
    f.raw = L"a.i.";        // includes trigger
    f.crossCommit = true;
    f.trigger = L'.';
    auto p = f.Run();
    EXPECT_TRUE(p.matched);
    EXPECT_EQ(p.bsCount, 3u);   // 4 - 1 (trigger)
}

TEST(PlanBsCountTest, CrossCommitWithoutTrigger) {
    PlanFixture f;
    f.table[L"a.i"] = L"artificial intelligence";
    f.raw = L"a.i";
    f.crossCommit = true;
    f.trigger = L' ';
    auto p = f.Run();
    EXPECT_TRUE(p.matched);
    EXPECT_EQ(p.bsCount, 3u);   // trigger == ' ', no decrement
}

TEST(PlanBsCountTest, DefaultBranchWithTrigger) {
    PlanFixture f;
    f.table[L"btw"] = L"by the way";
    f.raw = L"btw.";
    f.trigger = L'.';
    auto p = f.Run();
    EXPECT_TRUE(p.matched);
    EXPECT_EQ(p.bsCount, 3u);   // 4 - 1 (trigger), default branch
}
```

- [ ] **Step 2: Build and run**

```bash
cmake --build build-linux --target NextKeyTests -j 2>&1 | tail -3
./build-linux/tests/NextKeyTests --gtest_filter="PlanBsCountTest.*"
```
Expected: `[  PASSED  ] 5 tests.`

### Task 4d: Auto-caps tests

- [ ] **Step 1: Add 8 auto-caps tests**

Append to `tests/MacroCaseTest.cpp`:

```cpp
TEST(PlanAutoCapsDecisionTest, AutoCapsDisabledNoTransform) {
    PlanFixture f;
    f.table[L"btw"] = L"by the way";
    f.raw = L"BTW";
    f.trigger = L'.';
    f.autoCaps = false;
    auto p = f.Run();
    EXPECT_TRUE(p.matched);
    EXPECT_EQ(p.expansion, L"by the way");   // unchanged
}

TEST(PlanAutoCapsDecisionTest, MatchedExactSuppressesAutoCaps) {
    PlanFixture f;
    f.table[L"BTW"] = L"by the way";   // uppercase key (exact-match contract)
    f.raw = L"BTW";
    f.trigger = L'.';
    f.autoCaps = true;
    auto p = f.Run();
    EXPECT_TRUE(p.matched);
    EXPECT_EQ(p.expansion, L"by the way");   // matchedExact → no transform
}

TEST(PlanAutoCapsDecisionTest, MatchedViaCompositionSuppressesAutoCaps) {
    PlanFixture f;
    f.table[L"chao"] = L"xin chao";
    f.prevComp = L"CHAO";
    f.raw = L"";
    f.trigger = L' ';
    f.autoCaps = true;
    auto p = f.Run();
    EXPECT_TRUE(p.matched);
    EXPECT_EQ(p.expansion, L"xin chao");   // composition match → no transform
}

TEST(PlanAutoCapsDecisionTest, ExpansionHasUppercaseSuppressesTransform) {
    PlanFixture f;
    f.table[L"omw"] = L"On My Way";   // expansion not all-lower
    f.raw = L"OMW";
    f.trigger = L'.';
    f.autoCaps = true;
    auto p = f.Run();
    EXPECT_TRUE(p.matched);
    EXPECT_EQ(p.expansion, L"On My Way");
}

TEST(PlanAutoCapsDecisionTest, AllUpperRawTransformsToAllUpperExpansion) {
    PlanFixture f;
    f.table[L"omw"] = L"on my way";
    f.raw = L"OMW";
    f.trigger = L'.';
    f.autoCaps = true;
    auto p = f.Run();
    EXPECT_TRUE(p.matched);
    EXPECT_EQ(p.expansion, L"ON MY WAY");
}

TEST(PlanAutoCapsDecisionTest, FirstUpperRawTransformsToFirstUpperExpansion) {
    PlanFixture f;
    f.table[L"omw"] = L"on my way";
    f.raw = L"Omw";
    f.trigger = L'.';
    f.autoCaps = true;
    auto p = f.Run();
    EXPECT_TRUE(p.matched);
    EXPECT_EQ(p.expansion, L"On my way");
}

TEST(PlanAutoCapsEscapeTest, AllUpperSkipsBackslashN) {
    PlanFixture f;
    f.table[L"sig"] = L"name\\nemail";   // \n escape inside expansion
    f.raw = L"SIG";
    f.trigger = L'.';
    f.autoCaps = true;
    auto p = f.Run();
    EXPECT_TRUE(p.matched);
    // 'n' in '\n' must remain lowercase; everything else uppercase.
    EXPECT_EQ(p.expansion, L"NAME\\nEMAIL");
}

TEST(PlanAutoCapsEscapeTest, AllUpperPlainNStillUppercases) {
    PlanFixture f;
    f.table[L"hi"] = L"hello name";   // plain 'n' (no leading backslash)
    f.raw = L"HI";
    f.trigger = L'.';
    f.autoCaps = true;
    auto p = f.Run();
    EXPECT_TRUE(p.matched);
    EXPECT_EQ(p.expansion, L"HELLO NAME");
}
```

- [ ] **Step 2: Build and run**

```bash
cmake --build build-linux --target NextKeyTests -j 2>&1 | tail -3
./build-linux/tests/NextKeyTests --gtest_filter="PlanAutoCapsDecisionTest.*:PlanAutoCapsEscapeTest.*"
```
Expected: `[  PASSED  ] 8 tests.`

### Task 4e: useClipboard threshold tests

- [ ] **Step 1: Add 3 useClipboard tests**

Append to `tests/MacroCaseTest.cpp`:

```cpp
TEST(PlanUseClipboardTest, UnicodeAboveThresholdSetsClipboardTrue) {
    PlanFixture f;
    std::wstring big(250, L'x');   // > threshold (200)
    f.table[L"big"] = big;
    f.raw = L"big";
    f.trigger = L' ';
    f.codeTable = CodeTable::Unicode;
    f.threshold = 200;
    auto p = f.Run();
    EXPECT_TRUE(p.matched);
    EXPECT_TRUE(p.useClipboard);
}

TEST(PlanUseClipboardTest, UnicodeAtOrBelowThresholdKeepsSendInput) {
    PlanFixture f;
    std::wstring small(200, L'x');   // == threshold; uses ">" not ">="
    f.table[L"small"] = small;
    f.raw = L"small";
    f.trigger = L' ';
    f.codeTable = CodeTable::Unicode;
    f.threshold = 200;
    auto p = f.Run();
    EXPECT_TRUE(p.matched);
    EXPECT_FALSE(p.useClipboard);
}

TEST(PlanUseClipboardTest, NonUnicodeAlwaysSendInputRegardlessOfSize) {
    PlanFixture f;
    std::wstring big(500, L'x');
    f.table[L"big"] = big;
    f.raw = L"big";
    f.trigger = L' ';
    f.codeTable = CodeTable::TCVN3;
    f.threshold = 200;
    auto p = f.Run();
    EXPECT_TRUE(p.matched);
    EXPECT_FALSE(p.useClipboard);   // non-Unicode → always SendInput
}
```

- [ ] **Step 2: Build and run all 36 new tests**

```bash
cmake --build build-linux --target NextKeyTests -j 2>&1 | tail -3
./build-linux/tests/NextKeyTests --gtest_filter="ClipboardEscapesTest.*:BuildSegmentsTest.*:PlanMatchPrioritiesTest.*:PlanStoredKeyCaseRuleTest.*:PlanBsCountTest.*:PlanAutoCapsDecisionTest.*:PlanAutoCapsEscapeTest.*:PlanUseClipboardTest.*"
```
Expected: `[==========] 36 tests from 8 test suites ran. ... [  PASSED  ] 36 tests.`

- [ ] **Step 3: Run full test suite to confirm no regression**

```bash
./build-linux/tests/NextKeyTests --gtest_brief=1 2>&1 | tail -5
```
Expected: `[==========] 1513 tests from N test suites ran. ... [  PASSED  ] 1513 tests.`

- [ ] **Step 4: Commit**

```bash
git add src/core/MacroCase.cpp tests/MacroCaseTest.cpp
git commit -m "refactor(H5): implement Macro::Plan + 22 tests, total 36 new gtests

Lift-and-shift port of HookEngine::TryExpandMacro lines 3206-3331.
Member accesses replaced by PlanInputs fields; CharUpperBuffW /
CharLowerBuffW replaced by injected CaseMapper. File-static LowerCopy
helper avoids cross-layer dep on app/helpers/AppHelpers.h::ToLowerAscii.

Tests: 9 match priorities + 5 bs-count + 8 auto-caps + 3 useClipboard
threshold = 25 Plan tests on top of 11 helper tests = 36 new total.
Full suite: 1513/1513 PASS Linux."
```

---

## Task 5: Create `app/system/Win32CaseMapper.h`

**Files:**
- Create: `src/app/system/Win32CaseMapper.h`

This is Windows-only — we won't be able to verify the build on Linux. But the file is trivial and self-contained.

- [ ] **Step 1: Create the header**

```cpp
// NexusKey - Win32 locale-aware case mapper for Macro::CaseMapper
// Copyright (c) 2024-2026 PhatMT. All rights reserved.
// SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-NexusKey-Commercial
//
// Production CaseMapper that wraps CharUpperBuffW / CharLowerBuffW.
// These are locale-aware so Vietnamese diacritics ('ô' ↔ 'Ô') flip
// correctly, unlike towupper/towlower which only handle ASCII under
// the C locale.

#pragma once

#include <windows.h>

#include "core/MacroCase.h"

namespace NextKey {

class Win32CaseMapper final : public Macro::CaseMapper {
public:
    void Upper(wchar_t* buf, std::size_t n) const override {
        ::CharUpperBuffW(buf, static_cast<DWORD>(n));
    }
    void Lower(wchar_t* buf, std::size_t n) const override {
        ::CharLowerBuffW(buf, static_cast<DWORD>(n));
    }
};

}  // namespace NextKey
```

- [ ] **Step 2: Verify the file is in place (no build to do — header-only, used by HookEngine.cpp in Task 6)**

```bash
ls -la src/app/system/Win32CaseMapper.h
```
Expected: file present.

- [ ] **Step 3: Commit**

```bash
git add src/app/system/Win32CaseMapper.h
git commit -m "refactor(H5): add Win32CaseMapper header for production CaseMapper

Header-only adapter wrapping CharUpperBuffW / CharLowerBuffW. Used by
HookEngine in the next commit. Co-located with HookEngine.cpp under
app/system/ — Win32 dependency stays inside the app layer."
```

---

## Task 6: Refactor `HookEngine::TryExpandMacro` to use `Macro::Plan`

**Files:**
- Modify: `src/app/system/HookEngine.cpp` lines 3205-3422 (and add 2 includes near the top)

This is the key replacement. Linux build cannot verify it (HookEngine is Win32-only); Windows MSVC build via WSL→PowerShell is required.

- [ ] **Step 1: Add includes near the top of `HookEngine.cpp`**

Locate the existing include block (search for `#include "core/`). Add these two lines beside the other `core/` and `system/` includes (preserve existing alphabetical/logical grouping):

```cpp
#include "core/MacroCase.h"
#include "system/Win32CaseMapper.h"
```

- [ ] **Step 2: Replace the body of `TryExpandMacro`**

In `src/app/system/HookEngine.cpp`, locate `HookEngine::MacroResult HookEngine::TryExpandMacro(wchar_t triggerChar) {` (line 3205 today). Replace the entire function body — opening brace through closing brace at line 3422 — with:

```cpp
HookEngine::MacroResult HookEngine::TryExpandMacro(wchar_t triggerChar) {
    Win32CaseMapper mapper;
    Macro::PlanInputs inputs{
        rawMacroBuffer_, previousComposition_, previousEncodedWidths_,
        macroTable_,
        macroCrossCommit_,
        currentCodeTable_,
        autoCapsMacro_.load(std::memory_order_acquire),
        triggerChar,
        kMacroClipboardThreshold,
    };
    auto plan = Macro::Plan(inputs, mapper);
    if (!plan.matched) return MacroResult::NoMatch;

    auto inj = injector_.load(std::memory_order_acquire);

    if (plan.useClipboard) {
        if (plan.bsCount > 0) {
            sending_ = true;
            if (!inj->Replace(plan.bsCount, std::wstring_view{})) {
                HOOK_LOG(L"  TryExpandMacro[clipboard]: BS injector reported partial delivery");
            }
            sending_ = false;
            RecordSynthDispatch();
        }
        auto clipText = Macro::ExpandEscapesForClipboard(plan.expansion);
        ClipboardPaste(clipText);
        HOOK_LOG(L"  TryExpandMacro: clipboard paste %zu chars (raw %zu)",
                 clipText.size(), plan.expansion.size());
    } else {
        sending_ = true;
        std::size_t pendingBs = plan.bsCount;
        for (const auto& s : Macro::BuildSegments(plan.expansion, currentCodeTable_)) {
            if (s.isReturn) {
                if (pendingBs > 0) {
                    if (!inj->Replace(pendingBs, std::wstring_view{})) {
                        HOOK_LOG(L"  TryExpandMacro[send]: injector reported partial delivery");
                    }
                    pendingBs = 0;
                }
                inj->SendKey(VK_RETURN);
            } else if (!s.text.empty()) {
                if (!inj->Replace(pendingBs, std::wstring_view(s.text))) {
                    HOOK_LOG(L"  TryExpandMacro[send]: injector reported partial delivery");
                }
                pendingBs = 0;
            }
        }
        if (pendingBs > 0) {
            if (!inj->Replace(pendingBs, std::wstring_view{})) {
                HOOK_LOG(L"  TryExpandMacro[send]: injector reported partial delivery");
            }
        }
        sending_ = false;
        RecordSynthDispatch();
    }

    ClearWordState();
    CancelCommitUndo();
    return plan.isPartOfMacro ? MacroResult::ExpandedEatTrigger
                              : MacroResult::ExpandedPassTrigger;
}
```

- [ ] **Step 3: Verify `HookEngine.cpp` LOC dropped**

```bash
wc -l src/app/system/HookEngine.cpp
```
Expected: drop of ~165 LOC (217 LOC body removed, ~50 LOC orchestrator added) → from 3582 to ~3417, plus 2 lines for the new `#include`s. Concrete expectation: `3419 ± 5`.

- [ ] **Step 4: Build on Windows (from WSL)**

```bash
powershell.exe -Command "cd '\\wsl.localhost\Ubuntu-24.04\home\phatmt\code\NexusKey\build'; cmake --build . --target NexusKey --config Debug 2>&1" | tail -20
```
Expected: build succeeds, zero `/WX` warnings.

If the user has not yet generated the Windows CMake configuration, they need to do so first (one-time):

```bash
powershell.exe -Command "cd '\\wsl.localhost\Ubuntu-24.04\home\phatmt\code\NexusKey'; cmake -B build -G 'Visual Studio 17 2022' -A x64"
```

- [ ] **Step 5: Run Linux tests once more (still 1,513 PASS)**

```bash
./build-linux/tests/NextKeyTests --gtest_brief=1 2>&1 | tail -3
```
Expected: `[==========] 1513 tests ... [  PASSED  ] 1513 tests.` (HookEngine.cpp itself is Win32-only; not in the Linux test binary, so this rerun is a sanity check that the new includes don't break NextKeyCore.)

- [ ] **Step 6: Commit**

```bash
git add src/app/system/HookEngine.cpp
git commit -m "refactor(H5): replace TryExpandMacro body with Macro::Plan dispatcher

HookEngine::TryExpandMacro shrinks from 217 LOC to ~50 LOC. All decision
logic (match priorities, bs-count, auto-caps, clipboard threshold) and
\\n escape semantics now live in core/MacroCase. HookEngine remains the
sole owner of injector dispatch, sending_ flag, RecordSynthDispatch,
ClearWordState, and CancelCommitUndo — unchanged contracts.

Behavior byte-identical to Main 3257758 by line-for-line port.
Manual chaos smoke: see Task 7."
```

---

## Task 7: Manual Windows smoke + final verification

**Files:** none (verification only)

- [ ] **Step 1: Run Linux full suite for the record**

```bash
./build-linux/tests/NextKeyTests --gtest_brief=1 2>&1 | tail -5
```
Expected: `[  PASSED  ] 1513 tests.`

- [ ] **Step 2: User runs chaos smoke on Windows (cannot be done by agent)**

The user runs from PowerShell (or asks Claude to surface the command — it cannot be executed remotely):

```powershell
pwsh tools/run-chaos.ps1 -Host Notepad
```

Expected: edit-distance verdict matches the most recent Main baseline for that host. If `report-channeltraits-notepad.xml` shows new failures vs the prior chaos run on Main, STOP — the refactor introduced a regression.

Alternative quick manual check: the user types a known macro in Notepad and verifies expansion + capitalization + multi-line behavior.

- [ ] **Step 3: Push the branch**

```bash
git push origin refactor/h5-macro-extract
```

- [ ] **Step 4: Open PR**

```bash
gh pr create --title "refactor(H5): extract macro expansion to core/MacroCase" --body "$(cat <<'EOF'
## Summary
- Extract HookEngine::TryExpandMacro decision logic (217 LOC) into Linux-portable core/MacroCase.h/.cpp with three free helpers (Plan, ExpandEscapesForClipboard, BuildSegments)
- Add Win32CaseMapper.h adapter for production; AsciiCaseMapper for tests
- HookEngine::TryExpandMacro shrinks to ~50 LOC orchestrator
- 36 new gtest cases on Linux (1,477 → 1,513)

## Spec
- `docs/superpowers/specs/2026-05-07-h5-macro-extract-design.md` (commit ebbdd04)
- Cut 1.5: all `\n` escape semantics consolidated in MacroCase; HookEngine owns injector dispatch only

## Test plan
- [ ] Linux: 1,513/1,513 PASS via `./build-linux/tests/NextKeyTests`
- [ ] Windows MSVC build clean (no /WX warnings)
- [ ] Manual chaos run on Notepad host shows no edit-distance regression vs Main
- [ ] Manual smoke: type a macro with auto-caps + multi-line expansion, verify behavior unchanged

## Refs
- Closes H5 in `docs/REFACTOR_STATUS.md` §C
- Predecessor: H3 (#140), H7 (#139)
- Successor: H1 (ProcessKeyDown decompose)
EOF
)"
```

- [ ] **Step 5: After merge, refresh `docs/REFACTOR_STATUS.md`**

This is a follow-up doc commit on Main, separate from the PR (per the established pattern in §C):
- Move row H5 from §C to §A with the merge SHA + 2026-05-07 (or merge date).
- Update vital signs: HookEngine.cpp LOC drops by ~165; gtest count rises to 1,513.
- Update §G recommended next: H5 done → H1 ProcessKeyDown decompose is the next item.

---

## Self-Review Checklist (executed pre-publish)

**Spec coverage** — every spec requirement maps to a task:
- F1 (signature unchanged) → Task 6 Step 2 (function signature kept verbatim)
- F2 (Plan return contract) → Task 4a + tests Task 4b/4c/4d/4e
- F3 (ExpandEscapesForClipboard) → Task 2
- F4 (BuildSegments) → Task 3
- F5 (auto-caps rules) → Task 4a Step 1 (port) + Task 4d (tests)
- F6 (match priorities) → Task 4a + Task 4b
- F7 (bs-count five branches) → Task 4a + Task 4c
- F8 (CaseMapper interface) → Task 1 Step 1 + Task 5
- NF1 (Linux portability) → Task 1 Step 6 (Linux build); Task 4e Step 3 (full test run)
- NF2 (zero behavior change) → Task 6 Step 4 (Windows build) + Task 7 Step 2 (chaos)
- NF3 (hot-path cost) → architectural; verified by lift-and-shift fidelity
- NF4 (no Windows includes in MacroCase.h) → Task 1 Step 1 (only `<windows>`-free includes)
- NF5 (Win32CaseMapper.h location) → Task 5
- NF6 (36 cases / 1,513 total) → Task 4e Step 3
- NF7 (≥150 LOC drop) → Task 6 Step 3

**Placeholders** — none. Every code step has the actual code.

**Type consistency** — `MacroPlan`, `PlanInputs`, `Segment`, `CaseMapper` types used identically across Tasks 1-6. `Macro::Plan`, `Macro::ExpandEscapesForClipboard`, `Macro::BuildSegments` referenced by the same names everywhere.

**Scope** — single PR, single branch, focused on H5 backlog item. Total effort estimate per spec §1: 3.5-4.5h.
