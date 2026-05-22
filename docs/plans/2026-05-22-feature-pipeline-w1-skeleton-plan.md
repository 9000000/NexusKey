# Feature Pipeline Wave 1 — Skeleton Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Land the Feature Pipeline framework skeleton — types, interfaces, Brain coordinator, OutputChannel, Gate predicates — as additive new files in `src/core/brain/`. No behaviour change, no HookEngine wiring (Wave 2 task). Establishes the contract Wave 2-4 features will plug into.

**Architecture:** Brain owns a registry of `IFeature` per `Stage` (PreEngine / Engine / PostEngine). For each keystroke, Brain (1) builds `KeyContext`, (2) evaluates gates once, (3) dispatches features in stage × priority order, gate-filtered, (4) flushes intents to OutputChannel. State stays in engine; Brain reads via `ICompositionSession` view. Wave 1 ships the empty registry — no features registered, no HookEngine call site. Existing behaviour byte-identical (Brain code is linked but uncalled).

**Tech Stack:** C++20, GoogleTest, namespace `NextKey::Brain`. Builds via CMake `VKeyCore` static lib + `VKeyTests` executable. Linux gtest-able (no Win32 dependencies in `src/core/brain/`).

**Branch:** `feat/feature-pipeline-w1-skeleton` based on `feat/architecture-review-v3.1` (commit `b82ffe4`).

**Design doc:** `docs/plans/2026-05-22-feature-pipeline-framework-design.md` (commits `9579ff2` + `b82ffe4`).

---

## File Structure

**New files (all additive):**

```
src/core/brain/
  Stage.h                  # enum class Stage { PreEngine, Engine, PostEngine }
  Result.h                 # enum class Result { Pass, Handled, Veto }
  GateMask.h               # GateId enum + GateMask bitmask helpers
  Intent.h                 # std::variant<Backspace, Text, Reinject>
  IntentSink.h             # interface — features emit through this
  KeyContext.h             # POD — vk, char, mods, session view ref
  ICompositionSession.h    # read-only view interface over engine state
  IFeature.h               # the plugin interface
  IGate.h                  # gate predicate interface
  gates/
    EnglishBiasGate.h      # Wave 1 shell — reads global flag (no behaviour change)
    SpellCheckGate.h       # Wave 1 shell
    ToneEscapeGate.h       # Wave 1 shell
  Brain.h                  # registry + dispatch loop
  Brain.cpp
  OutputChannel.h          # intent sink that batches into IOutputInjector (stub in W1)
  OutputChannel.cpp

tests/brain/
  EnumsTest.cpp            # Stage / Result / GateMask basic checks
  IntentTest.cpp           # variant construction + holder semantics
  IntentSinkTest.cpp       # recording sink behaviour
  GateMaskCompositionTest.cpp
  BrainRegistryTest.cpp    # Register / FeaturesAtStage / unregister
  BrainDispatchTest.cpp    # stage ordering, priority, Handled stops stage, Veto skips remaining stages
  BrainGateFilterTest.cpp  # gate-blocked feature isn't called
  OutputChannelTest.cpp    # intent batching, flush ordering
```

**Modified files:** only `CMakeLists.txt` (add new sources to `VKeyCore` and `VKeyTests`).

**Untouched:** `src/app/system/HookEngine.cpp`, `src/core/engine/*`, all existing tests. Brain code is linked but uncalled — zero risk of behaviour drift.

---

## Task 1: Foundation enums — Stage, Result, GateMask

**Files:**
- Create: `src/core/brain/Stage.h`
- Create: `src/core/brain/Result.h`
- Create: `src/core/brain/GateMask.h`
- Create: `tests/brain/EnumsTest.cpp`

- [ ] **Step 1.1: Write failing test for Stage / Result / GateMask values**

Create `tests/brain/EnumsTest.cpp`:

```cpp
// tests/brain/EnumsTest.cpp
#include <gtest/gtest.h>
#include "core/brain/Stage.h"
#include "core/brain/Result.h"
#include "core/brain/GateMask.h"

using namespace NextKey::Brain;

TEST(Stage, ThreeOrderedStages) {
    EXPECT_LT(static_cast<int>(Stage::PreEngine),  static_cast<int>(Stage::Engine));
    EXPECT_LT(static_cast<int>(Stage::Engine),     static_cast<int>(Stage::PostEngine));
}

TEST(Stage, AllStagesCount) {
    EXPECT_EQ(kStageCount, 3u);
}

TEST(Result, ThreeVariants) {
    Result r1 = Result::Pass;
    Result r2 = Result::Handled;
    Result r3 = Result::Veto;
    EXPECT_NE(r1, r2);
    EXPECT_NE(r2, r3);
    EXPECT_NE(r1, r3);
}

TEST(GateMask, EmptyMaskBlocksNothing) {
    GateMask m = 0u;
    EXPECT_FALSE(GateMaskHas(m, GateId::EnglishBias));
}

TEST(GateMask, SetSingleBitAndQuery) {
    GateMask m = GateMaskFor(GateId::EnglishBias);
    EXPECT_TRUE(GateMaskHas(m, GateId::EnglishBias));
    EXPECT_FALSE(GateMaskHas(m, GateId::SpellCheck));
}

TEST(GateMask, ComposeMultiple) {
    GateMask m = GateMaskFor(GateId::EnglishBias) | GateMaskFor(GateId::SpellCheck);
    EXPECT_TRUE(GateMaskHas(m, GateId::EnglishBias));
    EXPECT_TRUE(GateMaskHas(m, GateId::SpellCheck));
    EXPECT_FALSE(GateMaskHas(m, GateId::ToneEscape));
}
```

- [ ] **Step 1.2: Run test to verify it fails (no headers yet)**

```bash
cd build-linux && cmake --build . --target VKeyTests 2>&1 | tail -5
```

Expected: compile error — `core/brain/Stage.h: No such file or directory`.

- [ ] **Step 1.3: Create `src/core/brain/Stage.h`**

```cpp
// src/core/brain/Stage.h
//
// Stage enum for Feature Pipeline dispatch ordering.
// Spec: docs/plans/2026-05-22-feature-pipeline-framework-design.md §3
#pragma once

#include <cstddef>

namespace NextKey::Brain {

enum class Stage : unsigned char {
    PreEngine  = 0,  // commit-undo, macro, ESC restore — runs BEFORE engine processes the key
    Engine     = 1,  // engine.PushChar (eventually wraps TypingEngine call as a feature)
    PostEngine = 2,  // backward-edit, passthrough — emits intents based on engine result
};

inline constexpr std::size_t kStageCount = 3u;

}  // namespace NextKey::Brain
```

- [ ] **Step 1.4: Create `src/core/brain/Result.h`**

```cpp
// src/core/brain/Result.h
//
// Per-feature dispatch outcome. Brain reads this to decide whether to
// call the next feature in the stage / next stage / stop entirely.
#pragma once

namespace NextKey::Brain {

enum class Result : unsigned char {
    Pass    = 0,  // feature not relevant to this key — try next feature
    Handled = 1,  // feature emitted intents, stop dispatching this stage
    Veto    = 2,  // feature short-circuits — skip remaining stages too
};

}  // namespace NextKey::Brain
```

- [ ] **Step 1.5: Create `src/core/brain/GateMask.h`**

```cpp
// src/core/brain/GateMask.h
//
// Bitmask of gates a feature requires to be "allowed" (gate.IsBlocked()==false).
// Brain evaluates all gates once per keystroke, then filters features by mask
// before calling them. Pattern D resolution in the design doc — features stop
// re-checking gates inside their body; brain enforces.
#pragma once

#include <cstdint>

namespace NextKey::Brain {

enum class GateId : unsigned char {
    EnglishBias = 0,  // skip transformations when EnglishBias detects English context
    SpellCheck  = 1,  // skip tone/free-marking when current syllable is invalid
    ToneEscape  = 2,  // skip transformations after an escape gesture in the same word

    kCount = 3,
};

using GateMask = std::uint32_t;

[[nodiscard]] inline constexpr GateMask GateMaskFor(GateId id) noexcept {
    return GateMask{1u} << static_cast<unsigned>(id);
}

[[nodiscard]] inline constexpr bool GateMaskHas(GateMask mask, GateId id) noexcept {
    return (mask & GateMaskFor(id)) != 0u;
}

}  // namespace NextKey::Brain
```

- [ ] **Step 1.6: Add files to CMakeLists.txt and tests glob**

In `CMakeLists.txt`, find the `NEXTKEY_CORE_SOURCES` list (line ~59 onward) and append:

```cmake
    src/core/brain/Stage.h
    src/core/brain/Result.h
    src/core/brain/GateMask.h
```

In the `VKeyTests` test sources (search for `tests/` glob or explicit list), append:

```cmake
    tests/brain/EnumsTest.cpp
```

- [ ] **Step 1.7: Run test to verify it passes**

```bash
cd build-linux && cmake .. -G "Unix Makefiles" -DCMAKE_BUILD_TYPE=Debug && cmake --build . --target VKeyTests
./tests/VKeyTests --gtest_filter="Stage.*:Result.*:GateMask.*"
```

Expected: 6 tests PASS.

- [ ] **Step 1.8: Commit**

```bash
git add src/core/brain/Stage.h src/core/brain/Result.h src/core/brain/GateMask.h \
        tests/brain/EnumsTest.cpp CMakeLists.txt
git commit -m "feat(brain): W1.1 — foundation enums (Stage, Result, GateMask)

First slice of the Feature Pipeline framework skeleton. Pure types,
no behaviour change. Spec: docs/plans/2026-05-22-feature-pipeline-framework-design.md"
```

---

## Task 2: Intent variant + IntentSink interface

**Files:**
- Create: `src/core/brain/Intent.h`
- Create: `src/core/brain/IntentSink.h`
- Create: `tests/brain/IntentTest.cpp`
- Create: `tests/brain/IntentSinkTest.cpp`

- [ ] **Step 2.1: Write failing test for Intent variant**

Create `tests/brain/IntentTest.cpp`:

```cpp
// tests/brain/IntentTest.cpp
#include <gtest/gtest.h>
#include <variant>
#include <string>
#include "core/brain/Intent.h"

using namespace NextKey::Brain;

TEST(Intent, BackspaceHoldsCount) {
    Intent i = Intents::Backspace{ .count = 2 };
    ASSERT_TRUE(std::holds_alternative<Intents::Backspace>(i));
    EXPECT_EQ(std::get<Intents::Backspace>(i).count, 2u);
}

TEST(Intent, TextHoldsWstring) {
    Intent i = Intents::Text{ .text = L"ê" };
    ASSERT_TRUE(std::holds_alternative<Intents::Text>(i));
    EXPECT_EQ(std::get<Intents::Text>(i).text, std::wstring{L"ê"});
}

TEST(Intent, ReinjectHoldsVk) {
    Intent i = Intents::Reinject{ .vk = 0x45 };
    ASSERT_TRUE(std::holds_alternative<Intents::Reinject>(i));
    EXPECT_EQ(std::get<Intents::Reinject>(i).vk, 0x45u);
}
```

- [ ] **Step 2.2: Write failing test for IntentSink (recording impl for test)**

Create `tests/brain/IntentSinkTest.cpp`:

```cpp
// tests/brain/IntentSinkTest.cpp
#include <gtest/gtest.h>
#include <vector>
#include "core/brain/Intent.h"
#include "core/brain/IntentSink.h"

using namespace NextKey::Brain;

namespace {

class RecordingSink final : public IntentSink {
public:
    void Emit(Intent i) override { intents_.push_back(std::move(i)); }
    const std::vector<Intent>& Intents() const noexcept { return intents_; }
private:
    std::vector<Intent> intents_;
};

}  // namespace

TEST(IntentSink, RecordsEmittedIntentsInOrder) {
    RecordingSink sink;
    sink.Emit(Intents::Backspace{ .count = 2 });
    sink.Emit(Intents::Text{ .text = L"ê" });
    sink.Emit(Intents::Reinject{ .vk = 0x45 });

    ASSERT_EQ(sink.Intents().size(), 3u);
    EXPECT_TRUE(std::holds_alternative<Intents::Backspace>(sink.Intents()[0]));
    EXPECT_TRUE(std::holds_alternative<Intents::Text>(sink.Intents()[1]));
    EXPECT_TRUE(std::holds_alternative<Intents::Reinject>(sink.Intents()[2]));
}
```

- [ ] **Step 2.3: Run tests to verify they fail (no headers yet)**

```bash
cd build-linux && cmake --build . --target VKeyTests 2>&1 | tail -5
```

Expected: compile error — `core/brain/Intent.h: No such file or directory`.

- [ ] **Step 2.4: Create `src/core/brain/Intent.h`**

```cpp
// src/core/brain/Intent.h
//
// Output intent — features emit one or more Intents per Handled keystroke.
// Brain accumulates intents into the OutputChannel which serializes them
// into a single Win32 SendInput batch (plus injector-specific quirks).
// Features NEVER call SendInput directly. Pattern B resolution in design.
//
// `Intent` is a std::variant alias so callers can use std::holds_alternative
// / std::get / std::visit directly without member-template gymnastics.
#pragma once

#include <cstdint>
#include <string>
#include <variant>

namespace NextKey::Brain {

namespace Intents {
    struct Backspace { unsigned count; };
    struct Text      { std::wstring text; };
    struct Reinject  { std::uint16_t vk; };
}

using Intent = std::variant<Intents::Backspace, Intents::Text, Intents::Reinject>;

}  // namespace NextKey::Brain
```

- [ ] **Step 2.5: Create `src/core/brain/IntentSink.h`**

```cpp
// src/core/brain/IntentSink.h
//
// Sink interface that features write into. Production impl is OutputChannel
// (wraps IOutputInjector). Test impl is RecordingSink that buffers intents.
#pragma once

#include "core/brain/Intent.h"

namespace NextKey::Brain {

class IntentSink {
public:
    virtual ~IntentSink() = default;
    virtual void Emit(Intent intent) = 0;
};

}  // namespace NextKey::Brain
```

- [ ] **Step 2.6: Append to CMakeLists**

Append to `NEXTKEY_CORE_SOURCES`:
```cmake
    src/core/brain/Intent.h
    src/core/brain/IntentSink.h
```

Append to `VKeyTests` sources:
```cmake
    tests/brain/IntentTest.cpp
    tests/brain/IntentSinkTest.cpp
```

- [ ] **Step 2.7: Run tests to verify they pass**

```bash
cd build-linux && cmake --build . --target VKeyTests && ./tests/VKeyTests --gtest_filter="Intent*"
```

Expected: 4 tests PASS.

- [ ] **Step 2.8: Commit**

```bash
git add src/core/brain/Intent.h src/core/brain/IntentSink.h \
        tests/brain/IntentTest.cpp tests/brain/IntentSinkTest.cpp \
        CMakeLists.txt
git commit -m "feat(brain): W1.2 — Intent variant + IntentSink interface

Pattern B resolution — features emit intents through a single sink
instead of calling SendInput directly. RecordingSink (test helper)
inlined in IntentSinkTest. OutputChannel (real sink) lands in W1.7."
```

---

## Task 3: KeyContext + ICompositionSession view

**Files:**
- Create: `src/core/brain/ICompositionSession.h`
- Create: `src/core/brain/KeyContext.h`

- [ ] **Step 3.1: Create `src/core/brain/ICompositionSession.h`**

```cpp
// src/core/brain/ICompositionSession.h
//
// Read-only view over engine composition state. Features access engine
// data through this interface — never directly via TypingEngine fields.
// Wave 1 ships interface only; Wave 2 wires a concrete impl that wraps
// HookEngine's engine_ + rawInput_ + commitStack_ via const refs.
#pragma once

#include <string_view>

namespace NextKey::Brain {

class ICompositionSession {
public:
    virtual ~ICompositionSession() = default;

    // The text currently rendered to the foreground app (post backward-edit
    // sync). Equivalent to today's previousComposition_.
    [[nodiscard]] virtual std::wstring_view PreviousRendered() const noexcept = 0;

    // What the engine would render after processing the current key. Equivalent
    // to today's engine_->Peek() after PushChar. Empty if no engine state.
    [[nodiscard]] virtual std::wstring_view EngineRendered() const noexcept = 0;

    // Raw input chars typed in the current word (pre-transformation).
    // Equivalent to today's rawInput_ in HookEngine. Used by ESC restore-raw,
    // English bias detection, commit-undo replay.
    [[nodiscard]] virtual std::wstring_view RawInput() const noexcept = 0;
};

}  // namespace NextKey::Brain
```

- [ ] **Step 3.2: Create `src/core/brain/KeyContext.h`**

```cpp
// src/core/brain/KeyContext.h
//
// Per-keystroke context handed to every feature. Built by Brain at the top
// of HandleKey, then passed by const ref through the dispatch loop. POD-like
// — no allocation, no virtual calls in construction.
#pragma once

#include <cstdint>
#include "core/brain/ICompositionSession.h"

namespace NextKey::Brain {

struct KeyContext {
    std::uint16_t            vk;            // Win32 VK code, e.g. 'A'=0x41
    wchar_t                  keyChar;       // resolved char (after CapsLock/Shift), 0 if non-alpha
    bool                     shift;
    bool                     capsLock;
    bool                     ctrl;
    bool                     alt;
    bool                     win;

    const ICompositionSession& session;     // const view, lifetime tied to HookEngine

    // Optional: re-inject vk after Handled — set by PostEngine features (e.g.
    // BackwardEdit) when they need to deliver the original keystroke alongside
    // their BS+text intents. 0 = no reinject. PreEngine features must leave 0.
    std::uint16_t            reinjectVk = 0;
};

}  // namespace NextKey::Brain
```

- [ ] **Step 3.3: Append to CMakeLists**

Append to `NEXTKEY_CORE_SOURCES`:
```cmake
    src/core/brain/ICompositionSession.h
    src/core/brain/KeyContext.h
```

- [ ] **Step 3.4: Build to verify headers compile**

```bash
cd build-linux && cmake --build . --target VKeyTests 2>&1 | tail -5
```

Expected: build succeeds (no test for these yet — they're consumed by Task 4+).

- [ ] **Step 3.5: Commit**

```bash
git add src/core/brain/ICompositionSession.h src/core/brain/KeyContext.h CMakeLists.txt
git commit -m "feat(brain): W1.3 — KeyContext + ICompositionSession view

Pattern E resolution scaffolding — composition session is the single
read-only view over engine state. Features receive KeyContext per
keystroke; no direct access to HookEngine fields. Concrete impl in W2."
```

---

## Task 4: IFeature interface

**Files:**
- Create: `src/core/brain/IFeature.h`

- [ ] **Step 4.1: Create `src/core/brain/IFeature.h`**

```cpp
// src/core/brain/IFeature.h
//
// The plugin interface. Each feature declares its stage, priority, and the
// gate-mask it requires to be unblocked. Brain dispatches features in
// (stage, priority) order, skipping any with at least one required gate
// raised. Result tells Brain whether to continue, stop this stage, or
// veto remaining stages.
#pragma once

#include "core/brain/Stage.h"
#include "core/brain/Result.h"
#include "core/brain/GateMask.h"
#include "core/brain/KeyContext.h"
#include "core/brain/IntentSink.h"

namespace NextKey::Brain {

class IFeature {
public:
    virtual ~IFeature() = default;

    // Static metadata — must return the same value for the lifetime of the
    // feature instance. Brain caches these at Register() time.
    [[nodiscard]] virtual Stage    FeatureStage() const noexcept = 0;
    [[nodiscard]] virtual int      Priority()     const noexcept = 0;
    [[nodiscard]] virtual GateMask Requires()     const noexcept = 0;

    // Per-keystroke entry point. May emit zero or more intents via sink.
    // Brain calls Try only when this feature's `Requires()` is satisfied
    // by the current gates evaluation.
    [[nodiscard]] virtual Result Try(const KeyContext& ctx, IntentSink& sink) = 0;
};

}  // namespace NextKey::Brain
```

- [ ] **Step 4.2: Append to CMakeLists**

```cmake
    src/core/brain/IFeature.h
```

- [ ] **Step 4.3: Build to verify**

```bash
cd build-linux && cmake --build . --target VKeyTests 2>&1 | tail -5
```

Expected: build succeeds.

- [ ] **Step 4.4: Commit**

```bash
git add src/core/brain/IFeature.h CMakeLists.txt
git commit -m "feat(brain): W1.4 — IFeature interface

Plugin contract: stage, priority, gate requirements, and Try() entry.
Brain caches static metadata at Register() time; Try() runs per-keystroke
gate-filtered."
```

---

## Task 5: IGate interface + 3 concrete gate shells

**Files:**
- Create: `src/core/brain/IGate.h`
- Create: `src/core/brain/gates/EnglishBiasGate.h`
- Create: `src/core/brain/gates/SpellCheckGate.h`
- Create: `src/core/brain/gates/ToneEscapeGate.h`

Concrete gates in Wave 1 are **shells** — they read no global state, always return `false` (unblocked). Wave 2 will wire them to actual EngProt / spellCheckDisabled_ / toneEscaped_ flags inside HookEngine. The shell exists so Brain dispatch can be tested end-to-end without Win32 deps.

- [ ] **Step 5.1: Create `src/core/brain/IGate.h`**

```cpp
// src/core/brain/IGate.h
//
// Gate predicate — Brain queries every registered gate once per keystroke,
// builds the cumulative GateMask of *raised* gates, then filters features
// by Requires() vs raised mask. Features never check gates inside Try().
#pragma once

#include "core/brain/GateMask.h"
#include "core/brain/KeyContext.h"

namespace NextKey::Brain {

class IGate {
public:
    virtual ~IGate() = default;
    [[nodiscard]] virtual GateId Id()                           const noexcept = 0;
    [[nodiscard]] virtual bool   IsRaised(const KeyContext&)    const noexcept = 0;
};

}  // namespace NextKey::Brain
```

- [ ] **Step 5.2: Create the 3 gate shells**

`src/core/brain/gates/EnglishBiasGate.h`:

```cpp
// src/core/brain/gates/EnglishBiasGate.h
//
// Wave 1 shell — always unblocked. Wave 2 wires this to the engine's
// engProt_.bias == LanguageBias::HardEnglish detection.
#pragma once

#include "core/brain/IGate.h"

namespace NextKey::Brain {

class EnglishBiasGate final : public IGate {
public:
    [[nodiscard]] GateId Id() const noexcept override { return GateId::EnglishBias; }
    [[nodiscard]] bool   IsRaised(const KeyContext&) const noexcept override { return false; }
};

}  // namespace NextKey::Brain
```

`src/core/brain/gates/SpellCheckGate.h`:

```cpp
// src/core/brain/gates/SpellCheckGate.h
//
// Wave 1 shell — always unblocked. Wave 2 wires this to TypingEngine's
// spellCheckDisabled_ flag.
#pragma once

#include "core/brain/IGate.h"

namespace NextKey::Brain {

class SpellCheckGate final : public IGate {
public:
    [[nodiscard]] GateId Id() const noexcept override { return GateId::SpellCheck; }
    [[nodiscard]] bool   IsRaised(const KeyContext&) const noexcept override { return false; }
};

}  // namespace NextKey::Brain
```

`src/core/brain/gates/ToneEscapeGate.h`:

```cpp
// src/core/brain/gates/ToneEscapeGate.h
//
// Wave 1 shell — always unblocked. Wave 2 wires this to TypingEngine's
// toneEscaped_ flag (set by all modifier/tone escape paths).
#pragma once

#include "core/brain/IGate.h"

namespace NextKey::Brain {

class ToneEscapeGate final : public IGate {
public:
    [[nodiscard]] GateId Id() const noexcept override { return GateId::ToneEscape; }
    [[nodiscard]] bool   IsRaised(const KeyContext&) const noexcept override { return false; }
};

}  // namespace NextKey::Brain
```

- [ ] **Step 5.3: Append to CMakeLists**

```cmake
    src/core/brain/IGate.h
    src/core/brain/gates/EnglishBiasGate.h
    src/core/brain/gates/SpellCheckGate.h
    src/core/brain/gates/ToneEscapeGate.h
```

- [ ] **Step 5.4: Build to verify**

```bash
cd build-linux && cmake --build . --target VKeyTests 2>&1 | tail -5
```

Expected: build succeeds.

- [ ] **Step 5.5: Commit**

```bash
git add src/core/brain/IGate.h src/core/brain/gates/ CMakeLists.txt
git commit -m "feat(brain): W1.5 — IGate interface + 3 gate shells

EnglishBiasGate, SpellCheckGate, ToneEscapeGate. Wave 1 shells always
return IsRaised()==false; Wave 2 wires real flag reads. Pattern D
resolution: features no longer check gate state inside their body."
```

---

## Task 6: Brain class — registry + dispatch loop

**Files:**
- Create: `src/core/brain/Brain.h`
- Create: `src/core/brain/Brain.cpp`
- Create: `tests/brain/BrainRegistryTest.cpp`
- Create: `tests/brain/BrainDispatchTest.cpp`
- Create: `tests/brain/BrainGateFilterTest.cpp`

- [ ] **Step 6.1: Write failing tests for Brain registry**

Create `tests/brain/BrainRegistryTest.cpp`:

```cpp
// tests/brain/BrainRegistryTest.cpp
#include <gtest/gtest.h>
#include "core/brain/Brain.h"
#include "core/brain/IFeature.h"

using namespace NextKey::Brain;

namespace {

class NoOpFeature final : public IFeature {
public:
    NoOpFeature(Stage s, int p) : stage_{s}, prio_{p} {}
    [[nodiscard]] Stage    FeatureStage() const noexcept override { return stage_; }
    [[nodiscard]] int      Priority()     const noexcept override { return prio_; }
    [[nodiscard]] GateMask Requires()     const noexcept override { return 0u; }
    [[nodiscard]] Result   Try(const KeyContext&, IntentSink&) override { return Result::Pass; }
private:
    Stage stage_;
    int   prio_;
};

}  // namespace

TEST(BrainRegistry, EmptyRegistryReturnsZeroFeaturesAtEveryStage) {
    Brain brain;
    EXPECT_EQ(brain.FeatureCountAtStage(Stage::PreEngine),  0u);
    EXPECT_EQ(brain.FeatureCountAtStage(Stage::Engine),     0u);
    EXPECT_EQ(brain.FeatureCountAtStage(Stage::PostEngine), 0u);
}

TEST(BrainRegistry, RegisterPutsFeatureInDeclaredStage) {
    Brain brain;
    auto f = std::make_unique<NoOpFeature>(Stage::Engine, 10);
    brain.Register(std::move(f));
    EXPECT_EQ(brain.FeatureCountAtStage(Stage::PreEngine),  0u);
    EXPECT_EQ(brain.FeatureCountAtStage(Stage::Engine),     1u);
    EXPECT_EQ(brain.FeatureCountAtStage(Stage::PostEngine), 0u);
}

TEST(BrainRegistry, MultipleFeaturesAtSameStageKeepsAll) {
    Brain brain;
    brain.Register(std::make_unique<NoOpFeature>(Stage::PreEngine, 10));
    brain.Register(std::make_unique<NoOpFeature>(Stage::PreEngine, 20));
    brain.Register(std::make_unique<NoOpFeature>(Stage::PreEngine, 5));
    EXPECT_EQ(brain.FeatureCountAtStage(Stage::PreEngine), 3u);
}
```

- [ ] **Step 6.2: Write failing tests for Brain dispatch ordering**

Create `tests/brain/BrainDispatchTest.cpp`:

```cpp
// tests/brain/BrainDispatchTest.cpp
#include <gtest/gtest.h>
#include <vector>
#include <string>
#include "core/brain/Brain.h"
#include "core/brain/IFeature.h"
#include "core/brain/ICompositionSession.h"

using namespace NextKey::Brain;

namespace {

// Records the order features are called by tagging itself in a shared vector.
class TaggingFeature final : public IFeature {
public:
    TaggingFeature(std::string tag, Stage s, int prio,
                   Result r, std::vector<std::string>& log)
        : tag_{std::move(tag)}, stage_{s}, prio_{prio}, result_{r}, log_{log} {}
    [[nodiscard]] Stage    FeatureStage() const noexcept override { return stage_; }
    [[nodiscard]] int      Priority()     const noexcept override { return prio_; }
    [[nodiscard]] GateMask Requires()     const noexcept override { return 0u; }
    [[nodiscard]] Result   Try(const KeyContext&, IntentSink&) override {
        log_.push_back(tag_);
        return result_;
    }
private:
    std::string  tag_;
    Stage        stage_;
    int          prio_;
    Result       result_;
    std::vector<std::string>& log_;
};

class FakeSession final : public ICompositionSession {
public:
    [[nodiscard]] std::wstring_view PreviousRendered() const noexcept override { return {}; }
    [[nodiscard]] std::wstring_view EngineRendered()   const noexcept override { return {}; }
    [[nodiscard]] std::wstring_view RawInput()         const noexcept override { return {}; }
};

class NullSink final : public IntentSink {
public:
    void Emit(Intent) override {}
};

KeyContext makeCtx(const ICompositionSession& session) {
    return KeyContext{
        .vk = 0x41, .keyChar = L'a',
        .shift = false, .capsLock = false, .ctrl = false, .alt = false, .win = false,
        .session = session, .reinjectVk = 0,
    };
}

}  // namespace

TEST(BrainDispatch, StagesRunInOrder_PreEngineThenEngineThenPostEngine) {
    std::vector<std::string> log;
    Brain brain;
    brain.Register(std::make_unique<TaggingFeature>("post", Stage::PostEngine, 10, Result::Pass, log));
    brain.Register(std::make_unique<TaggingFeature>("pre",  Stage::PreEngine,  10, Result::Pass, log));
    brain.Register(std::make_unique<TaggingFeature>("eng",  Stage::Engine,     10, Result::Pass, log));

    FakeSession session;
    NullSink sink;
    brain.HandleKey(makeCtx(session), sink);

    ASSERT_EQ(log.size(), 3u);
    EXPECT_EQ(log[0], "pre");
    EXPECT_EQ(log[1], "eng");
    EXPECT_EQ(log[2], "post");
}

TEST(BrainDispatch, WithinStageRunsLowerPriorityFirst) {
    std::vector<std::string> log;
    Brain brain;
    brain.Register(std::make_unique<TaggingFeature>("p20", Stage::PreEngine, 20, Result::Pass, log));
    brain.Register(std::make_unique<TaggingFeature>("p5",  Stage::PreEngine,  5, Result::Pass, log));
    brain.Register(std::make_unique<TaggingFeature>("p10", Stage::PreEngine, 10, Result::Pass, log));

    FakeSession session;
    NullSink sink;
    brain.HandleKey(makeCtx(session), sink);

    ASSERT_EQ(log.size(), 3u);
    EXPECT_EQ(log[0], "p5");
    EXPECT_EQ(log[1], "p10");
    EXPECT_EQ(log[2], "p20");
}

TEST(BrainDispatch, HandledStopsCurrentStageButRunsLaterStages) {
    std::vector<std::string> log;
    Brain brain;
    brain.Register(std::make_unique<TaggingFeature>("pre-first",  Stage::PreEngine, 10, Result::Handled, log));
    brain.Register(std::make_unique<TaggingFeature>("pre-second", Stage::PreEngine, 20, Result::Pass,    log));
    brain.Register(std::make_unique<TaggingFeature>("post",       Stage::PostEngine, 10, Result::Pass,   log));

    FakeSession session;
    NullSink sink;
    brain.HandleKey(makeCtx(session), sink);

    ASSERT_EQ(log.size(), 2u);
    EXPECT_EQ(log[0], "pre-first");
    EXPECT_EQ(log[1], "post");
}

TEST(BrainDispatch, VetoSkipsAllRemainingStages) {
    std::vector<std::string> log;
    Brain brain;
    brain.Register(std::make_unique<TaggingFeature>("pre",  Stage::PreEngine,  10, Result::Veto,    log));
    brain.Register(std::make_unique<TaggingFeature>("eng",  Stage::Engine,     10, Result::Pass,    log));
    brain.Register(std::make_unique<TaggingFeature>("post", Stage::PostEngine, 10, Result::Pass,    log));

    FakeSession session;
    NullSink sink;
    brain.HandleKey(makeCtx(session), sink);

    ASSERT_EQ(log.size(), 1u);
    EXPECT_EQ(log[0], "pre");
}
```

- [ ] **Step 6.3: Write failing tests for gate filter**

Create `tests/brain/BrainGateFilterTest.cpp`:

```cpp
// tests/brain/BrainGateFilterTest.cpp
#include <gtest/gtest.h>
#include <vector>
#include <string>
#include "core/brain/Brain.h"
#include "core/brain/IFeature.h"
#include "core/brain/IGate.h"
#include "core/brain/ICompositionSession.h"

using namespace NextKey::Brain;

namespace {

class TaggingFeature final : public IFeature {
public:
    TaggingFeature(std::string tag, GateMask requires_mask, std::vector<std::string>& log)
        : tag_{std::move(tag)}, requires_{requires_mask}, log_{log} {}
    [[nodiscard]] Stage    FeatureStage() const noexcept override { return Stage::Engine; }
    [[nodiscard]] int      Priority()     const noexcept override { return 10; }
    [[nodiscard]] GateMask Requires()     const noexcept override { return requires_; }
    [[nodiscard]] Result   Try(const KeyContext&, IntentSink&) override {
        log_.push_back(tag_);
        return Result::Pass;
    }
private:
    std::string tag_;
    GateMask    requires_;
    std::vector<std::string>& log_;
};

class FixedGate final : public IGate {
public:
    FixedGate(GateId id, bool raised) : id_{id}, raised_{raised} {}
    [[nodiscard]] GateId Id() const noexcept override { return id_; }
    [[nodiscard]] bool   IsRaised(const KeyContext&) const noexcept override { return raised_; }
private:
    GateId id_;
    bool   raised_;
};

class FakeSession final : public ICompositionSession {
public:
    [[nodiscard]] std::wstring_view PreviousRendered() const noexcept override { return {}; }
    [[nodiscard]] std::wstring_view EngineRendered()   const noexcept override { return {}; }
    [[nodiscard]] std::wstring_view RawInput()         const noexcept override { return {}; }
};

class NullSink final : public IntentSink {
public:
    void Emit(Intent) override {}
};

KeyContext makeCtx(const ICompositionSession& session) {
    return KeyContext{
        .vk = 0x41, .keyChar = L'a',
        .shift = false, .capsLock = false, .ctrl = false, .alt = false, .win = false,
        .session = session, .reinjectVk = 0,
    };
}

}  // namespace

TEST(BrainGateFilter, FeatureWithRequiresZeroAlwaysRuns) {
    std::vector<std::string> log;
    Brain brain;
    brain.RegisterGate(std::make_unique<FixedGate>(GateId::EnglishBias, /*raised=*/true));
    brain.Register(std::make_unique<TaggingFeature>("always", /*requires=*/0u, log));

    FakeSession session;
    NullSink sink;
    brain.HandleKey(makeCtx(session), sink);

    ASSERT_EQ(log.size(), 1u);
    EXPECT_EQ(log[0], "always");
}

TEST(BrainGateFilter, FeatureRequiringRaisedGateIsSkipped) {
    std::vector<std::string> log;
    Brain brain;
    brain.RegisterGate(std::make_unique<FixedGate>(GateId::EnglishBias, /*raised=*/true));
    brain.Register(std::make_unique<TaggingFeature>("blocked", GateMaskFor(GateId::EnglishBias), log));

    FakeSession session;
    NullSink sink;
    brain.HandleKey(makeCtx(session), sink);

    EXPECT_EQ(log.size(), 0u);
}

TEST(BrainGateFilter, FeatureRequiringUnraisedGateRuns) {
    std::vector<std::string> log;
    Brain brain;
    brain.RegisterGate(std::make_unique<FixedGate>(GateId::EnglishBias, /*raised=*/false));
    brain.Register(std::make_unique<TaggingFeature>("ok", GateMaskFor(GateId::EnglishBias), log));

    FakeSession session;
    NullSink sink;
    brain.HandleKey(makeCtx(session), sink);

    ASSERT_EQ(log.size(), 1u);
    EXPECT_EQ(log[0], "ok");
}
```

- [ ] **Step 6.4: Create `src/core/brain/Brain.h`**

```cpp
// src/core/brain/Brain.h
//
// The coordinator. Owns the feature registry per Stage + the gate registry.
// HandleKey is the per-keystroke entry: evaluates gates, dispatches features
// in (stage, priority) order, filtered by GateMask. Stops a stage on Handled,
// stops all stages on Veto.
//
// Wave 1: Brain is linked but not called from HookEngine — registry stays
// empty in production paths. Tests construct Brain instances directly.
#pragma once

#include <array>
#include <memory>
#include <vector>
#include "core/brain/Stage.h"
#include "core/brain/Result.h"
#include "core/brain/GateMask.h"
#include "core/brain/IFeature.h"
#include "core/brain/IGate.h"
#include "core/brain/KeyContext.h"
#include "core/brain/IntentSink.h"

namespace NextKey::Brain {

class Brain {
public:
    Brain();
    ~Brain();

    Brain(const Brain&)            = delete;
    Brain& operator=(const Brain&) = delete;

    // Take ownership; brain sorts features by Priority() at Register time.
    void Register(std::unique_ptr<IFeature> feature);
    void RegisterGate(std::unique_ptr<IGate> gate);

    // Per-keystroke dispatch.
    void HandleKey(const KeyContext& ctx, IntentSink& sink);

    // Test introspection.
    [[nodiscard]] std::size_t FeatureCountAtStage(Stage s) const noexcept;
    [[nodiscard]] std::size_t GateCount() const noexcept;

private:
    GateMask EvaluateGates(const KeyContext& ctx) const;

    std::array<std::vector<std::unique_ptr<IFeature>>, kStageCount> features_;
    std::vector<std::unique_ptr<IGate>>                              gates_;
};

}  // namespace NextKey::Brain
```

- [ ] **Step 6.5: Create `src/core/brain/Brain.cpp`**

```cpp
// src/core/brain/Brain.cpp
#include "core/brain/Brain.h"

#include <algorithm>

namespace NextKey::Brain {

Brain::Brain()  = default;
Brain::~Brain() = default;

void Brain::Register(std::unique_ptr<IFeature> feature) {
    const auto stage = feature->FeatureStage();
    auto& bucket = features_[static_cast<std::size_t>(stage)];
    bucket.push_back(std::move(feature));
    std::sort(bucket.begin(), bucket.end(),
              [](const std::unique_ptr<IFeature>& a, const std::unique_ptr<IFeature>& b) {
                  return a->Priority() < b->Priority();
              });
}

void Brain::RegisterGate(std::unique_ptr<IGate> gate) {
    gates_.push_back(std::move(gate));
}

GateMask Brain::EvaluateGates(const KeyContext& ctx) const {
    GateMask raised = 0u;
    for (const auto& g : gates_) {
        if (g->IsRaised(ctx)) raised |= GateMaskFor(g->Id());
    }
    return raised;
}

void Brain::HandleKey(const KeyContext& ctx, IntentSink& sink) {
    const GateMask raised = EvaluateGates(ctx);

    for (std::size_t s = 0; s < kStageCount; ++s) {
        auto& bucket = features_[s];
        for (auto& feature : bucket) {
            // Skip if any required gate is raised.
            if ((feature->Requires() & raised) != 0u) continue;

            const Result r = feature->Try(ctx, sink);
            if (r == Result::Handled) break;            // stop this stage
            if (r == Result::Veto)    return;           // stop all stages
            // Result::Pass — continue to next feature
        }
    }
}

std::size_t Brain::FeatureCountAtStage(Stage s) const noexcept {
    return features_[static_cast<std::size_t>(s)].size();
}

std::size_t Brain::GateCount() const noexcept {
    return gates_.size();
}

}  // namespace NextKey::Brain
```

- [ ] **Step 6.6: Append to CMakeLists**

Append to `NEXTKEY_CORE_SOURCES`:
```cmake
    src/core/brain/Brain.h
    src/core/brain/Brain.cpp
```

Append to `VKeyTests` sources:
```cmake
    tests/brain/BrainRegistryTest.cpp
    tests/brain/BrainDispatchTest.cpp
    tests/brain/BrainGateFilterTest.cpp
```

- [ ] **Step 6.7: Run tests**

```bash
cd build-linux && cmake --build . --target VKeyTests
./tests/VKeyTests --gtest_filter="Brain*"
```

Expected: 10 tests PASS (3 registry, 4 dispatch, 3 gate-filter).

- [ ] **Step 6.8: Commit**

```bash
git add src/core/brain/Brain.h src/core/brain/Brain.cpp \
        tests/brain/BrainRegistryTest.cpp tests/brain/BrainDispatchTest.cpp \
        tests/brain/BrainGateFilterTest.cpp CMakeLists.txt
git commit -m "feat(brain): W1.6 — Brain class + dispatch loop

Registry + (stage × priority) dispatch with gate filter. Pattern C+D
resolution: ordering manifest replaces if/else chains; gates filter
features upstream instead of features re-checking flags. Registry is
empty in production paths (HookEngine wiring lands in W2)."
```

---

## Task 7: OutputChannel — IntentSink production impl

**Files:**
- Create: `src/core/brain/OutputChannel.h`
- Create: `src/core/brain/OutputChannel.cpp`
- Create: `tests/brain/OutputChannelTest.cpp`

Wave 1 ships OutputChannel as a **batching collector**. It implements `IntentSink`, accumulates intents in order, and exposes `Flush()` that **returns** the accumulated batch. Wave 2 will swap Flush for "call `IOutputInjector::Replace(bsCount, text)` + SendKey(reinjectVk)". For now, the batch is observable so tests can verify ordering — and HookEngine integration doesn't exist yet.

- [ ] **Step 7.1: Write failing test**

Create `tests/brain/OutputChannelTest.cpp`:

```cpp
// tests/brain/OutputChannelTest.cpp
#include <gtest/gtest.h>
#include "core/brain/OutputChannel.h"

using namespace NextKey::Brain;

TEST(OutputChannel, EmitAccumulatesInOrder) {
    OutputChannel ch;
    ch.Emit(Intents::Backspace{ .count = 2 });
    ch.Emit(Intents::Text{ .text = L"ê" });
    ch.Emit(Intents::Reinject{ .vk = 0x45 });

    auto batch = ch.TakeBatch();
    ASSERT_EQ(batch.size(), 3u);
    EXPECT_TRUE(std::holds_alternative<Intents::Backspace>(batch[0]));
    EXPECT_EQ(std::get<Intents::Backspace>(batch[0]).count, 2u);
    EXPECT_TRUE(std::holds_alternative<Intents::Text>(batch[1]));
    EXPECT_EQ(std::get<Intents::Text>(batch[1]).text, std::wstring{L"ê"});
    EXPECT_TRUE(std::holds_alternative<Intents::Reinject>(batch[2]));
    EXPECT_EQ(std::get<Intents::Reinject>(batch[2]).vk, 0x45u);
}

TEST(OutputChannel, TakeBatchEmptiesInternalBuffer) {
    OutputChannel ch;
    ch.Emit(Intents::Backspace{ .count = 1 });
    auto first = ch.TakeBatch();
    auto second = ch.TakeBatch();
    EXPECT_EQ(first.size(), 1u);
    EXPECT_EQ(second.size(), 0u);
}
```

- [ ] **Step 7.2: Create `src/core/brain/OutputChannel.h`**

```cpp
// src/core/brain/OutputChannel.h
//
// Production IntentSink — accumulates feature-emitted intents per keystroke,
// then flushes as a single batch. Wave 1 exposes the batch via TakeBatch()
// for tests. Wave 2 will replace TakeBatch with FlushToInjector(IOutputInjector&)
// that issues `Replace(bsCount, text)` + `SendKey(reinjectVk)` in one go.
//
// Pattern B resolution: features never call SendInput; OutputChannel is the
// single serialization point.
#pragma once

#include <vector>
#include "core/brain/Intent.h"
#include "core/brain/IntentSink.h"

namespace NextKey::Brain {

class OutputChannel final : public IntentSink {
public:
    OutputChannel()  = default;
    ~OutputChannel() = default;

    OutputChannel(const OutputChannel&)            = delete;
    OutputChannel& operator=(const OutputChannel&) = delete;

    void Emit(Intent intent) override;

    // Consumes the current batch. After this call, the channel is empty.
    [[nodiscard]] std::vector<Intent> TakeBatch();

    [[nodiscard]] bool Empty() const noexcept { return batch_.empty(); }

private:
    std::vector<Intent> batch_;
};

}  // namespace NextKey::Brain
```

- [ ] **Step 7.3: Create `src/core/brain/OutputChannel.cpp`**

```cpp
// src/core/brain/OutputChannel.cpp
#include "core/brain/OutputChannel.h"

#include <utility>

namespace NextKey::Brain {

void OutputChannel::Emit(Intent intent) {
    batch_.push_back(std::move(intent));
}

std::vector<Intent> OutputChannel::TakeBatch() {
    return std::exchange(batch_, std::vector<Intent>{});
}

}  // namespace NextKey::Brain
```

- [ ] **Step 7.4: Append to CMakeLists**

Append to `NEXTKEY_CORE_SOURCES`:
```cmake
    src/core/brain/OutputChannel.h
    src/core/brain/OutputChannel.cpp
```

Append to `VKeyTests` sources:
```cmake
    tests/brain/OutputChannelTest.cpp
```

- [ ] **Step 7.5: Run test**

```bash
cd build-linux && cmake --build . --target VKeyTests
./tests/VKeyTests --gtest_filter="OutputChannel.*"
```

Expected: 2 tests PASS.

- [ ] **Step 7.6: Commit**

```bash
git add src/core/brain/OutputChannel.h src/core/brain/OutputChannel.cpp \
        tests/brain/OutputChannelTest.cpp CMakeLists.txt
git commit -m "feat(brain): W1.7 — OutputChannel intent batcher

Production IntentSink: accumulates intents per keystroke, exposes
TakeBatch() for inspection. Wave 2 will replace TakeBatch with
FlushToInjector(IOutputInjector&) that calls Replace+SendKey."
```

---

## Task 8: Final verification — run full test suite + lint

**Files:** none new. This is the integration gate.

- [ ] **Step 8.1: Full clean build**

```bash
cd /home/phatmt/code/NexusKey/.claude/worktrees/feat-pipeline-w1-skeleton
rm -rf build-linux
cmake -B build-linux -G "Unix Makefiles" -DCMAKE_BUILD_TYPE=Debug
cmake --build build-linux --target VKeyTests -j$(nproc)
```

Expected: clean build with no warnings touching `src/core/brain/`. Existing warnings unchanged.

- [ ] **Step 8.2: Full test suite**

```bash
./build-linux/tests/VKeyTests
```

Expected: all existing tests still pass (1903+ from feat/architecture-review-v3.1 baseline) + ~25 new Brain/Intent/Output/Enums tests. Total ~1928 PASS, 0 FAIL.

- [ ] **Step 8.3: Verify HookEngine.cpp is untouched**

```bash
git diff feat/architecture-review-v3.1 -- src/app/system/HookEngine.cpp
```

Expected: empty diff — Wave 1 must not touch HookEngine. If anything appears, revert.

- [ ] **Step 8.4: Verify production binary unchanged**

```bash
git diff feat/architecture-review-v3.1 -- src/core/engine/ src/app/output/ src/tsf/
```

Expected: empty diff — Wave 1 is additive only.

- [ ] **Step 8.5: Final integration commit (if anything dangling)**

Most likely nothing — each task already committed. If anything remains:

```bash
git status
# If any leftovers, commit with: chore(brain): W1.8 — final integration
```

- [ ] **Step 8.6: Verify branch ready for review**

```bash
git log feat/architecture-review-v3.1..HEAD --oneline
```

Expected: 7 commits — W1.1 through W1.7.

---

## Done definition for Wave 1

- ✅ 7 commits on `feat/feature-pipeline-w1-skeleton`
- ✅ ~12 new files in `src/core/brain/` + ~8 test files in `tests/brain/`
- ✅ `Brain` class compiles and dispatches in tests
- ✅ All gates / features / intents / sessions are interface-defined
- ✅ 3 gate shells return `IsRaised()==false` (no behaviour change)
- ✅ HookEngine.cpp, TypingEngine.cpp, EngineController.cpp — untouched
- ✅ All existing tests pass + ~25 new tests pass
- ✅ Brain registry stays empty in production (linked but uncalled)
- ✅ Linux gtest-compatible (no Win32 deps in `src/core/brain/`)

Wave 2 next:
1. Concrete `CompositionSession` impl that wraps `HookEngine::engine_` + `rawInput_` + `commitStack_`.
2. Real gates (EnglishBiasGate reads engProt_, etc.).
3. `BackwardEditFeature` plugin replacing `ReplaceComposition`.
4. Wire `Brain.HandleKey` into `HookEngine::ProcessKeyDown`.
5. Verify chaos 55/55 + `hiệu→hiêj` regression test.
