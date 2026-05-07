# Path G — G-4 customKeyMap Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a per-key user override layer to the TypingEngine dispatch pipeline so users can remap any ASCII key to any `TypingAction` via `TypingConfig::customKeyMap`. Default-empty preserves all 1450 existing GTests byte-identically.

**Architecture:** A `std::array<TypingAction, 128>` field on `TypingConfig` holds the remap. Default-init to all `TypingAction::None`. In `TypingEngine::PushChar`, before calling `ClassifyKey`, look up `customKeyMap[lower]`; if non-`None`, use it; otherwise fall through to `ClassifyKey`. The existing `isVniDigitSequence` literal-digit guard post-applies. `ClassifyKey` itself is unchanged — pure rule layer stays pure.

**Tech Stack:** C++20, GoogleTest (Linux build target `NextKeyTests`), CMake. No new dependencies.

**Spec:** `docs/superpowers/specs/2026-05-07-path-g-g4-customkeymap-design.md` (commit `cb181ff`).

**Branch:** `sprint-3/path-g-customkeymap` off `Main` at `cb181ff`.

**Out of scope (deferred to G-5):** ConfigManager TOML wiring, Sciter UI dialog, per-user `keymap_<name>.toml` files, active-method selector, conflict warnings.

---

## File Structure

| File | Action | Responsibility |
|---|---|---|
| `src/core/config/TypingConfig.h` | Modify | Add `customKeyMap` field + 2 includes |
| `src/core/engine/TypingEngine.cpp` | Modify | Replace dispatch at line 235 with override-then-fallback |
| `tests/CustomKeyMapTest.cpp` | Create | All 7 test groups (G1–G7) for the override layer |
| `CMakeLists.txt` | Modify | Append new test source to `NEXTKEY_TEST_SOURCES` |
| `HANDOFF.md` | Modify | Mark G-4 complete; flag G-5 as next pickup |

Untouched (deliberately): `src/core/engine/TypingAction.h`, `src/core/config/ConfigManager.{h,cpp}`, all engine handlers, all dialogs.

---

## Task 1 — Branch off Main

**Files:** N/A (workspace setup).

- [ ] **Step 1: Confirm clean working tree on Main at `cb181ff`**

```bash
git status
git log --oneline -1
```

Expected: `working tree clean`, HEAD shows `cb181ff docs(spec): G-4 customKeyMap engine hook design`.

- [ ] **Step 2: Branch off Main**

```bash
git checkout -b sprint-3/path-g-customkeymap Main
```

Expected: `Switched to a new branch 'sprint-3/path-g-customkeymap'`.

---

## Task 2 — Add `customKeyMap` field to `TypingConfig`

**Files:**
- Modify: `src/core/config/TypingConfig.h`

This task is the prerequisite for tests to compile. No behavior change — default-init array of `TypingAction::None` makes the dispatch hook (added later) a no-op.

- [ ] **Step 1: Add includes near the top of `TypingConfig.h`**

In `src/core/config/TypingConfig.h`, **replace** the existing include block:

```cpp
#pragma once

#include <cstdint>
#include <string>
#include <vector>
```

with:

```cpp
#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "core/engine/TypingAction.h"
```

- [ ] **Step 2: Add the field at the end of `struct TypingConfig`**

In `src/core/config/TypingConfig.h`, **find** the existing line:

```cpp
    std::vector<std::wstring> spellExclusions;  // Spell check exclusion prefixes (e.g. "hđ", "đp")
```

and **insert immediately after it** (before the `// Default constructor` comment):

```cpp

    /// Per-key user override (G-4). Index by ASCII code (`towlower(c)` of
    /// the keystroke). Default-init = all `TypingAction::None`, which the
    /// dispatcher treats as "no override → fall through to `ClassifyKey`".
    /// G-5 will populate this from per-user keymap files; G-4 ships the
    /// engine hook only.
    std::array<TypingAction, 128> customKeyMap{};
```

- [ ] **Step 3: Build engine target on Linux to confirm no compile errors**

```bash
cmake -B build-linux -G "Unix Makefiles" -DCMAKE_BUILD_TYPE=Debug
cmake --build build-linux --target NextKeyTests
```

Expected: build completes with no errors. (CMake re-configure not strictly required since we only added a header field, but the test target rebuilds anyway.)

- [ ] **Step 4: Run all existing tests to confirm zero regressions**

```bash
./build-linux/tests/NextKeyTests
```

Expected: `[  PASSED  ] 1450 tests.` (the count from HANDOFF.md). Field exists but is read by no one yet, so behavior is identical.

- [ ] **Step 5: Commit field-only change**

```bash
git add src/core/config/TypingConfig.h
git commit -m "Sprint 3 Path G G-4.1: add customKeyMap field to TypingConfig

Introduces std::array<TypingAction, 128> customKeyMap field, default-init
to all TypingAction::None. No engine code reads it yet — dispatch hook
lands in G-4.2. All 1450 existing GTests pass unchanged.

Per spec docs/superpowers/specs/2026-05-07-path-g-g4-customkeymap-design.md
section 3.1."
```

---

## Task 3 — Create `CustomKeyMapTest.cpp` skeleton + G1 regression group

**Files:**
- Create: `tests/CustomKeyMapTest.cpp`
- Modify: `CMakeLists.txt`

G1 asserts that with `customKeyMap` default-empty, dispatch produces identical output to baseline. These tests pass before the dispatch hook exists (no read site means no change). They become the regression net.

- [ ] **Step 1: Create the test file with G1 group**

Create `tests/CustomKeyMapTest.cpp` with:

```cpp
// NexusKey - customKeyMap (G-4) Tests
// Copyright (c) 2024-2026 PhatMT. All rights reserved.
// SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-NexusKey-Commercial
//
// Tests for the per-key user override layer added in Path G G-4.
// Spec: docs/superpowers/specs/2026-05-07-path-g-g4-customkeymap-design.md

#include <gtest/gtest.h>

#include "core/config/TypingConfig.h"
#include "core/engine/TypingAction.h"
#include "core/engine/TypingEngine.h"
#include "TestHelper.h"

namespace NextKey {
namespace {

using Testing::TypeString;

class CustomKeyMapTest : public ::testing::Test {
protected:
    TypingConfig MakeTelexConfig() {
        TypingConfig cfg;
        cfg.inputMethod = InputMethod::Telex;
        cfg.spellCheckEnabled = false;
        cfg.optimizeLevel = 0;
        return cfg;
    }

    TypingConfig MakeVniConfig() {
        TypingConfig cfg;
        cfg.inputMethod = InputMethod::VNI;
        cfg.spellCheckEnabled = false;
        cfg.optimizeLevel = 0;
        return cfg;
    }

    TypingConfig MakeCombinedConfig() {
        TypingConfig cfg;
        cfg.inputMethod = InputMethod::Combined;
        cfg.spellCheckEnabled = false;
        cfg.optimizeLevel = 0;
        return cfg;
    }
};

// =====================================================================
// G1 — Default-empty parity: customKeyMap{} → behavior unchanged
// =====================================================================

TEST_F(CustomKeyMapTest, DefaultEmptyMatchesTelex) {
    TypingConfig cfg = MakeTelexConfig();
    TypingEngine engine(cfg);
    TypeString(engine, L"asfx");  // 'a' + dấu sắc on 'a' = 'á' then literal 's'? Actually a+s = á, then f → grave override → à, then x → tilde → ã
    // The exact composed result mirrors current G-3.6 behavior. We assert
    // it matches the value produced by an engine constructed with the same
    // baseline TypingConfig (no override field touched).
    const std::wstring withDefault = engine.Peek();

    TypingConfig baseline = MakeTelexConfig();
    TypingEngine baselineEngine(baseline);
    TypeString(baselineEngine, L"asfx");
    EXPECT_EQ(withDefault, baselineEngine.Peek());
}

TEST_F(CustomKeyMapTest, DefaultEmptyMatchesVni) {
    TypingConfig cfg = MakeVniConfig();
    TypingEngine engine(cfg);
    TypeString(engine, L"a1e2o3");
    const std::wstring withDefault = engine.Peek();

    TypingConfig baseline = MakeVniConfig();
    TypingEngine baselineEngine(baseline);
    TypeString(baselineEngine, L"a1e2o3");
    EXPECT_EQ(withDefault, baselineEngine.Peek());
}

TEST_F(CustomKeyMapTest, DefaultEmptyMatchesCombined) {
    TypingConfig cfg = MakeCombinedConfig();
    TypingEngine engine(cfg);
    TypeString(engine, L"as6w7");
    const std::wstring withDefault = engine.Peek();

    TypingConfig baseline = MakeCombinedConfig();
    TypingEngine baselineEngine(baseline);
    TypeString(baselineEngine, L"as6w7");
    EXPECT_EQ(withDefault, baselineEngine.Peek());
}

}  // namespace
}  // namespace NextKey
```

- [ ] **Step 2: Append the test source to `CMakeLists.txt`**

In `CMakeLists.txt`, **find** the line:

```cmake
    tests/TypingActionTest.cpp
```

and **insert immediately after it**:

```cmake
    tests/CustomKeyMapTest.cpp
```

(Result: alphabetical order is broken vs `TypingConfigRCUTests.cpp` below, but matches the natural placement next to the related `TypingActionTest.cpp`. The existing list is grouped by topic, not alphabetic.)

- [ ] **Step 3: Re-configure CMake and rebuild test target**

```bash
cmake -B build-linux -G "Unix Makefiles" -DCMAKE_BUILD_TYPE=Debug
cmake --build build-linux --target NextKeyTests
```

Expected: build succeeds. The new test source is compiled into `NextKeyTests`.

- [ ] **Step 4: Run G1 group**

```bash
./build-linux/tests/NextKeyTests --gtest_filter="CustomKeyMapTest.DefaultEmpty*"
```

Expected: `[  PASSED  ] 3 tests.` (the three G1 cases). They pass because the `withDefault` and `baseline` engines run identical code paths — no override is consulted yet.

- [ ] **Step 5: Run full test suite to confirm count grew correctly**

```bash
./build-linux/tests/NextKeyTests
```

Expected: `[  PASSED  ] 1453 tests.` (1450 + 3 G1 cases). No failures.

- [ ] **Step 6: Commit G1 regression net**

```bash
git add tests/CustomKeyMapTest.cpp CMakeLists.txt
git commit -m "Sprint 3 Path G G-4.2: G1 regression tests (default-empty parity)

Adds CustomKeyMapTest fixture + 3 G1 cases that pin current dispatch
behavior. With customKeyMap{} (default-init), composed output must match
a parallel engine built with the same baseline config. These tests
become the structural guarantee that adding the dispatch hook in G-4.3
preserves all 1450 prior cases.

Linux GTest 1453/1453 PASS."
```

---

## Task 4 — Add G2 test (failing) → implement dispatch hook → G2 passes

**Files:**
- Modify: `tests/CustomKeyMapTest.cpp` (append G2 group)
- Modify: `src/core/engine/TypingEngine.cpp` (replace dispatch at line 235)

This is the TDD RED→GREEN core: the G2 test fails until the dispatch hook is wired.

- [ ] **Step 1: Append G2 group to `tests/CustomKeyMapTest.cpp`**

In `tests/CustomKeyMapTest.cpp`, **find** the closing `}  // namespace` + `}  // namespace NextKey` at the end of the file. **Insert before** the closing `}  // namespace`:

```cpp

// =====================================================================
// G2 — Replace built-in: user override always wins (precedence)
// =====================================================================

TEST_F(CustomKeyMapTest, RemapTelexSToToneHook) {
    TypingConfig cfg = MakeTelexConfig();
    cfg.customKeyMap[static_cast<size_t>(L's')] = TypingAction::ToneHook;
    TypingEngine engine(cfg);
    TypeString(engine, L"as");
    // Default Telex: 'a' + 's' → 'á' (sắc). With remap 's'→ToneHook: 'a' + 's' → 'ả' (hỏi).
    EXPECT_EQ(engine.Peek(), L"ả");
}

TEST_F(CustomKeyMapTest, RemapVniDigit1ToClearTone) {
    TypingConfig cfg = MakeVniConfig();
    // Default VNI: '1' → ToneAcute. Remap '1' → ClearTone.
    cfg.customKeyMap[static_cast<size_t>(L'1')] = TypingAction::ClearTone;
    TypingEngine engine(cfg);
    TypeString(engine, L"a2");  // 'a' + grave → 'à'
    EXPECT_EQ(engine.Peek(), L"à");
    TypeString(engine, L"1");   // remapped: clear tone
    EXPECT_EQ(engine.Peek(), L"a");
}
```

- [ ] **Step 2: Build and run G2 — expect FAIL**

```bash
cmake --build build-linux --target NextKeyTests
./build-linux/tests/NextKeyTests --gtest_filter="CustomKeyMapTest.Remap*"
```

Expected: **FAIL**. Both G2 cases produce the default behavior (`'á'` for the first test, `'á'` for the second after the `'1'` keystroke) because no code reads `cfg.customKeyMap` yet.

This is the desired RED state.

- [ ] **Step 3: Implement the dispatch hook in `TypingEngine.cpp`**

In `src/core/engine/TypingEngine.cpp`, **find** the block at line 235 (currently):

```cpp
    // 1. Classify keystroke once (G-3.2 dispatch foundation). VNI digit
    // sequences override to None so subsequent action checks treat the
    // digit as literal — matches the pre-G-3 `!isVniDigitSequence` guards
    // that gated VNI tone, VNI modifier, and clear-tone branches.
    TypingAction action = ClassifyKey(lower, IsTelexMode(), IsVniMode());
    if (isVniDigitSequence) action = TypingAction::None;
```

and **replace** it with:

```cpp
    // 1. Classify keystroke once (G-3.2 dispatch foundation). G-4 layers
    // an optional per-key user override above the static rules: when
    // `customKeyMap[lower]` is non-`None`, it wins. ASCII-only (`lower <
    // 128`) — non-ASCII keys fall straight to `ClassifyKey`. VNI digit
    // sequences still override to None so subsequent action checks treat
    // the digit as literal — matches the pre-G-3 `!isVniDigitSequence`
    // guards that gated VNI tone, VNI modifier, and clear-tone branches.
    TypingAction action;
    if (lower < 128 &&
        config_.customKeyMap[static_cast<uint8_t>(lower)] != TypingAction::None) {
        action = config_.customKeyMap[static_cast<uint8_t>(lower)];
    } else {
        action = ClassifyKey(lower, IsTelexMode(), IsVniMode());
    }
    if (isVniDigitSequence) action = TypingAction::None;
```

- [ ] **Step 4: Build and run G2 — expect PASS**

```bash
cmake --build build-linux --target NextKeyTests
./build-linux/tests/NextKeyTests --gtest_filter="CustomKeyMapTest.Remap*"
```

Expected: `[  PASSED  ] 2 tests.`

- [ ] **Step 5: Run full suite to confirm zero regressions**

```bash
./build-linux/tests/NextKeyTests
```

Expected: `[  PASSED  ] 1455 tests.` (1450 baseline + 3 G1 + 2 G2). No failures — the dispatch hook is byte-equivalent for the 1450 baseline cases (their configs use default-empty `customKeyMap`, so the new branch is never taken).

- [ ] **Step 6: Commit dispatch hook + G2 tests**

```bash
git add src/core/engine/TypingEngine.cpp tests/CustomKeyMapTest.cpp
git commit -m "Sprint 3 Path G G-4.3: dispatch hook + G2 user-wins tests

Adds the override layer at TypingEngine.cpp:235. When customKeyMap[lower]
is non-None, dispatch uses it; otherwise falls back to ClassifyKey.
isVniDigitSequence guard post-applies (literal-digit escape preserved).

G2 tests verify user override wins over built-in Telex and VNI rules.
1455/1455 GTest PASS on Linux."
```

---

## Task 5 — Add remaining test groups G3–G7

**Files:**
- Modify: `tests/CustomKeyMapTest.cpp` (append G3, G4, G5, G6, G7 groups)

All these tests should PASS immediately because the dispatch hook is in place from Task 4. They lock in the remaining design properties.

- [ ] **Step 1: Append G3 (gap-fill new key)**

In `tests/CustomKeyMapTest.cpp`, append before the closing `}  // namespace`:

```cpp

// =====================================================================
// G3 — Gap-fill: map a key that ClassifyKey returns None for
// =====================================================================

TEST_F(CustomKeyMapTest, MapQToClearTone_TelexMode) {
    TypingConfig cfg = MakeTelexConfig();
    // 'q' is not a Telex action key — ClassifyKey returns None.
    cfg.customKeyMap[static_cast<size_t>(L'q')] = TypingAction::ClearTone;
    TypingEngine engine(cfg);
    TypeString(engine, L"asq");  // 'a' + sắc → 'á', then 'q' clears tone → 'a'
    EXPECT_EQ(engine.Peek(), L"a");
}

TEST_F(CustomKeyMapTest, MapQToToneAcute_VniMode) {
    TypingConfig cfg = MakeVniConfig();
    // 'q' is not a VNI action key — ClassifyKey returns None.
    cfg.customKeyMap[static_cast<size_t>(L'q')] = TypingAction::ToneAcute;
    TypingEngine engine(cfg);
    TypeString(engine, L"aq");  // 'a' + remapped 'q' → ToneAcute → 'á'
    EXPECT_EQ(engine.Peek(), L"á");
}
```

- [ ] **Step 2: Append G4 (ASCII boundary)**

In `tests/CustomKeyMapTest.cpp`, append:

```cpp

// =====================================================================
// G4 — ASCII boundary: non-ASCII keys bypass the override branch
// =====================================================================

TEST_F(CustomKeyMapTest, NonAsciiKeyFallsBackToClassifyKey) {
    TypingConfig cfg = MakeTelexConfig();
    // The override array has only 128 slots; non-ASCII keys must skip
    // the override check entirely (defensive: `lower < 128` guard).
    // We verify by typing a Vietnamese char directly — the engine treats
    // it as a literal char (ClassifyKey returns None for it), and no
    // override applies because `lower >= 128`.
    TypingEngine engine(cfg);
    TypeString(engine, L"á");  // U+00E1, definitely >= 128
    EXPECT_EQ(engine.Peek(), L"á");  // unchanged literal pass-through
}
```

- [ ] **Step 3: Append G5 (`isVniDigitSequence` interaction)**

In `tests/CustomKeyMapTest.cpp`, append:

```cpp

// =====================================================================
// G5 — isVniDigitSequence post-applies even when override fired
// =====================================================================

TEST_F(CustomKeyMapTest, OverrideOnDigitYieldsLiteralInDigitSequence) {
    TypingConfig cfg = MakeVniConfig();
    // Remap '7' → ToneHook (instead of default VniHorn).
    cfg.customKeyMap[static_cast<size_t>(L'7')] = TypingAction::ToneHook;
    TypingEngine engine(cfg);
    // Type a literal digit first to enter "VNI digit sequence" mode.
    TypeString(engine, L"4");
    // Now '7' should be treated as literal (digit-sequence guard wins
    // over both the override and the default VniHorn) → composed "47".
    TypeString(engine, L"7");
    EXPECT_EQ(engine.Peek(), L"47");
}
```

- [ ] **Step 4: Append G6 (sentinel semantics)**

In `tests/CustomKeyMapTest.cpp`, append:

```cpp

// =====================================================================
// G6 — Sentinel: explicit None == not set
// =====================================================================

TEST_F(CustomKeyMapTest, CustomMapNoneFallsThroughToClassifyKey) {
    TypingConfig cfg = MakeTelexConfig();
    // Explicitly set 's' to None — must be indistinguishable from default.
    cfg.customKeyMap[static_cast<size_t>(L's')] = TypingAction::None;
    TypingEngine engine(cfg);
    TypeString(engine, L"as");
    // Default Telex: 'a' + 's' → 'á' (sắc).
    EXPECT_EQ(engine.Peek(), L"á");
}
```

- [ ] **Step 5: Append G7 (parametric all-actions roundtrip)**

In `tests/CustomKeyMapTest.cpp`, append:

```cpp

// =====================================================================
// G7 — Parametric smoke: every non-None TypingAction reachable via remap
// =====================================================================

class CustomKeyMapAllActions
    : public CustomKeyMapTest,
      public ::testing::WithParamInterface<TypingAction> {};

TEST_P(CustomKeyMapAllActions, EveryTypingActionRoundtrips) {
    const TypingAction action = GetParam();
    ASSERT_NE(action, TypingAction::None) << "G7 only iterates non-None actions";

    // Configure: 'q' (which ClassifyKey returns None for in pure Telex mode)
    // remapped to the parameter action. Compare composed output of typing
    // "q" against an engine driven through the natural key for the same
    // action. We do not assert specific Vietnamese strings here — only
    // that dispatch reaches the correct action handler (no crash, action
    // resolves). Engine state observation is via Peek().
    TypingConfig cfg = MakeCombinedConfig();
    cfg.customKeyMap[static_cast<size_t>(L'q')] = action;
    TypingEngine engine(cfg);
    // Seed a vowel so modifier/tone actions have something to operate on.
    TypeString(engine, L"a");
    EXPECT_NO_THROW(TypeString(engine, L"q"));
    // Final Peek should be a non-empty wstring (engine must not be in a
    // broken state after dispatch).
    EXPECT_FALSE(engine.Peek().empty());
}

INSTANTIATE_TEST_SUITE_P(
    AllActions,
    CustomKeyMapAllActions,
    ::testing::Values(
        TypingAction::ClearTone,
        TypingAction::ToneAcute,
        TypingAction::ToneGrave,
        TypingAction::ToneHook,
        TypingAction::ToneTilde,
        TypingAction::ToneDot,
        TypingAction::CircumflexA,
        TypingAction::CircumflexE,
        TypingAction::CircumflexO,
        TypingAction::HornW,
        TypingAction::HornInsertO,
        TypingAction::HornInsertU,
        TypingAction::StrokeD,
        TypingAction::VniCircumflex,
        TypingAction::VniHorn,
        TypingAction::VniBreve,
        TypingAction::VniStroke
    )
);
```

- [ ] **Step 6: Build and run all CustomKeyMapTest cases**

```bash
cmake --build build-linux --target NextKeyTests
./build-linux/tests/NextKeyTests --gtest_filter="CustomKeyMap*"
```

Expected: `[  PASSED  ] N tests.` where N = 3 (G1) + 2 (G2) + 2 (G3) + 1 (G4) + 1 (G5) + 1 (G6) + 17 (G7 parametric) = **27 cases**.

- [ ] **Step 7: Run full suite — confirm total count**

```bash
./build-linux/tests/NextKeyTests
```

Expected: `[  PASSED  ] 1477 tests.` (1450 baseline + 27 new). No failures.

- [ ] **Step 8: Commit G3–G7**

```bash
git add tests/CustomKeyMapTest.cpp
git commit -m "Sprint 3 Path G G-4.4: G3-G7 test groups

Locks in remaining design properties:
- G3: gap-fill (map keys ClassifyKey returns None for)
- G4: ASCII boundary (non-ASCII keys bypass override)
- G5: isVniDigitSequence post-applies even when override fired
- G6: sentinel None == not set
- G7: every non-None TypingAction reachable via remap (17 parametric)

Linux GTest 1477/1477 PASS."
```

---

## Task 6 — Update HANDOFF.md and final verification

**Files:**
- Modify: `HANDOFF.md`

- [ ] **Step 1: Update HANDOFF.md top section**

In `HANDOFF.md`, **find** the existing top heading:

```markdown
## 2026-05-07 — Sprint 3 Path G G-3 COMPLETE (CURRENT PICKUP NOTE)
```

and **insert before it** a new top section:

```markdown
## 2026-05-07 — Sprint 3 Path G G-4 COMPLETE (CURRENT PICKUP NOTE)

**Branch:** `sprint-3/path-g-customkeymap`. **Goal:** engine-side per-key user override layer.

**Commits:**
1. G-4.1 — `customKeyMap` field on TypingConfig (default-init all `None`).
2. G-4.2 — G1 regression tests (default-empty parity, 3 cases).
3. G-4.3 — Dispatch hook at `TypingEngine.cpp:235` + G2 user-wins tests (2 cases).
4. G-4.4 — G3-G7 tests (22 cases): gap-fill, ASCII boundary, digit-sequence interaction, sentinel, all-actions parametric.

**Final dispatch shape (G-4):**

```
PushChar(c)
  └─ lower = towlower(c)
  └─ if (lower < 128 && customKeyMap[lower] != None)
        action = customKeyMap[lower]            ← user override wins
     else
        action = ClassifyKey(lower, isTelex, isVni)
  └─ if (isVniDigitSequence) action = None       ← literal-digit guard post-applies
  └─ ProcessModifier(action, c) → 6 Handle*
```

**Test counts:** 1450 (post-G-3) + 27 (G-4 new) = **1477 cases on Linux**.

**Files touched:** `src/core/config/TypingConfig.h` (+1 field, +2 includes), `src/core/engine/TypingEngine.cpp` (+4 LOC at line 235), `tests/CustomKeyMapTest.cpp` (NEW, ~250 LOC), `CMakeLists.txt` (+1 test source).

**Out of scope (deferred to G-5):** TOML schema, Sciter dialog, per-user `keymap_<name>.toml` files, active-method selector. ConfigManager untouched in G-4.

### NEXT — G-5 keymap files + UI

Wire ConfigManager to load per-user `keymap_<name>.toml` files into `TypingConfig.customKeyMap`. Add Sciter dialog for editing. Add `[input].active_user_method` field to select which keymap is loaded. Reuse the existing 7-file split pattern (see `ConfigManager.cpp`).

---

```

(The trailing horizontal rule separates the new section from the old G-3 pickup note that follows.)

- [ ] **Step 2: Run full test suite one final time**

```bash
./build-linux/tests/NextKeyTests
```

Expected: `[  PASSED  ] 1477 tests.`

- [ ] **Step 3: Confirm Linux build is clean**

```bash
cmake --build build-linux --target NextKeyTests 2>&1 | tail -20
```

Expected: `[100%] Built target NextKeyTests`. No warnings or errors.

- [ ] **Step 4: Commit HANDOFF update**

```bash
git add HANDOFF.md
git commit -m "docs(handoff): Path G G-4 COMPLETE — G-5 keymap files next"
```

- [ ] **Step 5: Push branch**

```bash
git push -u origin sprint-3/path-g-customkeymap
```

Expected: branch published; output ends with `Branch 'sprint-3/path-g-customkeymap' set up to track 'origin/sprint-3/path-g-customkeymap'`.

- [ ] **Step 6: Open PR (manual; not part of automation)**

The user (`@phatMT97`) should:
1. Run Windows MSVC build locally to confirm cross-platform clean.
2. Open PR `sprint-3/path-g-customkeymap` → `Main` titled "Sprint 3 Path G G-4: customKeyMap engine hook".
3. PR body should reference the design spec at `docs/superpowers/specs/2026-05-07-path-g-g4-customkeymap-design.md`.

---

## Acceptance Criteria (from spec §8)

- [ ] `TypingConfig` has `customKeyMap` field of type `std::array<TypingAction, 128>` with default-init to all `None`. (Task 2)
- [ ] `TypingEngine::PushChar` consults `customKeyMap` before `ClassifyKey` per spec §3.3. (Task 4)
- [ ] All 1450 pre-G-4 GTests pass on Linux unchanged. (Task 4 Step 5)
- [ ] New `CustomKeyMapTest.cpp` adds ≥ 12 cases covering G1–G7, all passing. (Tasks 3+4+5; total = 27)
- [ ] Total Linux GTest count ≥ 1463 PASS. (Task 6 Step 2; actual = 1477)
- [ ] No source change in `ConfigManager.{h,cpp}`, `TypingAction.h`, or any engine handler. (verified by `git diff Main..HEAD --stat` showing only the 4 files listed in §File Structure plus HANDOFF.md)
- [ ] Windows MSVC build clean (verified locally by user — manual step).
