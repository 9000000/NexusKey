# Path G — G-4 customKeyMap Engine Hook

**Status:** Design approved 2026-05-07. Awaiting implementation plan.
**Sprint:** 3 / Path G (refactor + Phonotactics + custom keymap layer)
**Predecessors:** G-1 (Phonotactics), G-2 (DI + namespace), G-3 (TypingAction enum + dispatch).
**Successors:** G-5 (Sciter UI + per-user `keymap_<name>.toml` files), G-6 (Unikey/EVKey import-export).
**Brainstorm:** `_bmad-output/brainstorming/brainstorming-session-2026-05-07-0250.md` (Idea #11).

---

## 1. Goal

Add a per-key user override layer to the input dispatch pipeline. After G-4, the engine accepts a remap from `TypingConfig` and applies it before the static Telex/VNI rules in `ClassifyKey`. With an empty remap, behavior is byte-identical to G-3.6 (current Main).

**Non-goals (explicitly deferred):**
- Sciter UI for editing keymaps → G-5.
- TOML schema and per-user `keymap_<name>.toml` files → G-5.
- ConfigManager loading the field from disk → G-5.
- Unikey / EVKey format import-export → G-6.

G-4 is a pure engine-side hook. ConfigManager remains untouched. The new field defaults to all-`None` so all 1450 existing GTests pass unchanged.

---

## 2. Requirements

### 2.1 Functional

- F1: `TypingConfig` exposes a `customKeyMap` field of type `std::array<TypingAction, 128>` (ASCII-indexed).
- F2: When `customKeyMap[key] != TypingAction::None`, the engine dispatches that action regardless of mode and regardless of what `ClassifyKey(key, mode)` would return ("user override always wins").
- F3: When `customKeyMap[key] == TypingAction::None`, the engine falls through to `ClassifyKey(key, IsTelexMode(), IsVniMode())` (current behavior).
- F4: Override is mode-agnostic — the same `customKeyMap` applies in Telex, VNI, SimpleTelex, and Combined modes. (Per Idea #11, the active keymap inherently belongs to one parent input method; mode arbitration happens at keymap-selection time, not dispatch time.)
- F5: The existing `isVniDigitSequence` literal-digit guard (line 236 of `TypingEngine.cpp`) still applies after override resolution. Number sequences like `E747` remain literal even if `'7'` is remapped.
- F6: Non-ASCII keys (`lower >= 128`) bypass the override branch entirely and go straight to `ClassifyKey`.
- F7: Override lookup is case-insensitive — the existing `towlower(c) → lower` upstream of dispatch already canonicalizes case.

### 2.2 Non-functional

- NF1: Zero behavior change when `customKeyMap` is default-initialized (all `None`).
- NF2: Hot-path cost of override check: bounds compare + indexed load + sentinel compare ≤ 3 instructions before fallback. No allocation, no hash, no branch on mode.
- NF3: Memory: +128 bytes per `TypingConfig` instance. Live count is 1–2 → ≤ 256 B total. Within Path G's "tiny lookup tables" budget.
- NF4: `TypingConfig` remains trivially copyable (POD-style). The `std::array<TypingAction, 128>` member preserves this.
- NF5: `TypingAction.h` stays dependency-free (existing comment at `TypingAction.h:8-9`). No new includes.
- NF6: All 30+ existing call sites that construct or copy `TypingConfig` (engine ctor, `EngineFactory`, dialogs, tests) compile unchanged because the new field is default-initialized.

---

## 3. Design

### 3.1 Data structure

`TypingConfig.h` gains one field and one include:

```cpp
#include "core/engine/TypingAction.h"   // already dependency-free
#include <array>

struct TypingConfig {
    // ... 25 existing fields unchanged ...
    std::vector<std::wstring> spellExclusions;

    /// Per-key user override. Index by ASCII code (`towlower(c)` of the
    /// keystroke). Default-initialized to all `TypingAction::None`, which
    /// the dispatcher treats as "no override → fall through to
    /// `ClassifyKey`". G-4 introduces the field and dispatch hook only;
    /// G-5 will populate it from per-user keymap files.
    std::array<TypingAction, 128> customKeyMap{};

    TypingConfig() = default;
};
```

**Why `std::array<TypingAction, 128>`** (settled during brainstorming):

| Constraint | Array satisfies |
|---|---|
| YAGNI: Telex/VNI bind ASCII only | ✅ No futureproofing cost for non-ASCII |
| Hot-path performance | ✅ O(1) indexed load, no hash |
| Trivially copyable / POD | ✅ Array of `uint8_t` enum |
| Default = "no override" | ✅ Zero-init = all `None` |
| Backward compat (additive-only) | ✅ Default ctor unchanged |

Rejected: `std::unordered_map<wchar_t, TypingAction>` (heap, hash, non-trivial copy), `std::vector<TypingAction>` (size-invariant maintenance, empty-check guard).

### 3.2 Sentinel semantics

`TypingAction::None` (G-3.1) already means "literal char, no IME action." The override layer reuses this:

- `customKeyMap[key] == None` ⟺ "no override at this key" — fall through to `ClassifyKey`.
- `customKeyMap[key] == ToneAcute` ⟺ "user remapped key to dấu sắc" — use directly.

This unifies presence detection with action vocabulary; no parallel `bool hasOverride[128]` is needed.

### 3.3 Dispatch hook

The single insertion point is in `TypingEngine.cpp::PushChar`, replacing the current G-3.2 classification at `TypingEngine.cpp:235`:

```cpp
// G-3.2 (current):
TypingAction action = ClassifyKey(lower, IsTelexMode(), IsVniMode());
if (isVniDigitSequence) action = TypingAction::None;
```

becomes:

```cpp
// G-4: User override layer above static rules.
TypingAction action;
if (lower < 128 &&
    config_.customKeyMap[static_cast<uint8_t>(lower)] != TypingAction::None) {
    action = config_.customKeyMap[static_cast<uint8_t>(lower)];
} else {
    action = ClassifyKey(lower, IsTelexMode(), IsVniMode());
}
if (isVniDigitSequence) action = TypingAction::None;
```

**Properties:**

- **Layered, not folded:** `ClassifyKey` stays pure (`TypingAction.h:54` comment "Pure function — no state lookup, no side effects" remains true). Override is a separate concern resolved at the call site.
- **Pure when empty:** Default-init `customKeyMap` → first branch always false → byte-identical fallback path.
- **`isVniDigitSequence` post-applies:** Line 236 keeps its semantics. Disambiguation safety preserved.
- **No mode coupling:** Override does not consult `IsTelexMode()`/`IsVniMode()` — keymap belongs to one parent method (set elsewhere when keymap is selected, G-5).

### 3.4 What does NOT change

- `ClassifyKey` signature in `TypingAction.h` — zero churn on the 10 G-3.1 ClassifyKey GTests.
- All 6 `Handle*` modifier handlers (`HandleHornInsert`, `HandleAdjacentCircumflex`, `HandleHornW`, `HandleStrokeD`, `HandleVniHorn`, `HandleVniCircumflex`, `HandleVniBreve`) — they receive `action` opaquely.
- `Tone requestedTone = ActionToTone(action)` (`TypingEngine.cpp:254`) — works with override-derived `action` seamlessly.
- `ConfigManager.{h,cpp}` — no schema change in G-4.
- All existing 1450 GTest cases — no behavior change when `customKeyMap` is default-empty.

### 3.5 Files touched

| File | Change | Net LOC |
|---|---|---|
| `src/core/config/TypingConfig.h` | +`std::array<TypingAction, 128> customKeyMap{}` field, +2 includes | +5 |
| `src/core/engine/TypingEngine.cpp` | Replace 2-line dispatch at line 235 with 6-line if/else | +4 |
| `tests/CustomKeyMapTest.cpp` | NEW — engine-level GTests | +≈200 |
| `CMakeLists.txt` | Add new test source to `NEXTKEY_TEST_SOURCES` | +1 |

Total production code change: **+9 LOC** across two files. `TypingAction.h`, `ConfigManager.{h,cpp}`, all engine handlers, all dialogs untouched.

---

## 4. Test Plan

New file: `tests/CustomKeyMapTest.cpp`. All tests run via `TypingEngine` directly (Linux-buildable, no Windows/TSF dependency).

**Run:** `./build-linux/tests/NextKeyTests --gtest_filter="CustomKeyMapTest.*"`

### Test groups

| ID | Group | Cases | Verifies |
|---|---|---|---|
| G1 | Default-empty preserves existing behavior | `DefaultEmptyMatchesTelex`, `DefaultEmptyMatchesVni`, `DefaultEmptyMatchesCombined` | `customKeyMap{}` → composed output identical to G-3.6 baseline for representative input strings |
| G2 | Replace built-in (user-wins precedence) | `RemapTelexSToToneHook`, `RemapVniDigit1ToClearTone` | `cfg.customKeyMap['s'] = ToneHook` in Telex → "as" composes to "ả" not "á" |
| G3 | Add new key (gap-fill) | `MapQToClearTone_TelexMode`, `MapQToToneAcute_VniMode` | Keys that `ClassifyKey` returns `None` for now dispatch real actions |
| G4 | ASCII boundary | `NonAsciiKeyFallsBackToClassifyKey` | `lower >= 128` defensively skips override branch |
| G5 | `isVniDigitSequence` interaction | `OverrideOnDigitYieldsLiteralInDigitSequence` | `'7' → ToneHook` in VNI: "E747" still produces literal digits (post-apply guard wins) |
| G6 | Sentinel semantics | `CustomMapNoneFallsThroughToClassifyKey` | Explicitly setting a key to `None` is identical to leaving it default |
| G7 | All-actions smoke | `EveryTypingActionRoundtrips` (parametric) | For each of the 17 non-`None` `TypingAction` values, `'q' → action` produces output equivalent to the action firing through the regular dispatch. (`None` is the sentinel; G6 covers that case.) |

### Test count target

≈12–15 GTest cases. Total after G-4: **1450 + ≈13 ≈ 1463 cases**.

### Regression guarantee

Test group G1 is the structural guarantee that all 1450 prior cases keep passing — they run with `TypingConfig{}` (default ctor), which initializes `customKeyMap` to all `None`, which makes the new branch a never-taken no-op.

---

## 5. Backward Compatibility

- **TypingConfig serialization:** No TOML changes in G-4. Existing config files load identically.
- **TypingConfig consumers:** All current call sites construct/copy `TypingConfig` by value. Default-init of the new array field is automatic — no source change required at any of the 30+ call sites.
- **EngineFactory & TypingEngine ctors:** Unchanged. Engine receives `TypingConfig` by const-ref or value as today.
- **Existing GTests:** All 1450 pass with `customKeyMap` default-empty. G1 test group asserts this structurally.

---

## 6. Out of Scope (Path G milestones G-5+)

- **G-5 — Sciter UI + keymap files:** Dialog for editing per-key bindings; per-user `keymap_<name>.toml` files following the existing 7-file split pattern; active-method selector that chooses which keymap loads into `TypingConfig.customKeyMap`; conflict warnings when two keys map to the same action.
- **G-6 — Import/export:** Unikey config (`S=DAU_SAC F=DAU_HUYEN ...`) and EVKey format compatibility, mapping their key codes into `TypingAction` values.

---

## 7. Risks & Mitigations

| Risk | Likelihood | Mitigation |
|---|---|---|
| Test G1 group fails due to a subtle dispatch difference | Low | Default-empty array → first branch is unreachable; bytewise-equivalent code path |
| `TypingConfig` size growth breaks ABI in some niche caller | Very low | All callers are intra-project; no DLL exports `TypingConfig` by ABI |
| Future G-5 wants per-mode maps | Medium | Brainstorm Idea #11 already settled "1 keymap = 1 parent method" — selection happens at the keymap layer, not the dispatch layer; current array<128> remains correct |
| Hot-path bounds check costs measurable time | Very low | `lower < 128` is a single compare; whole branch is well-predicted (default-empty users hit always-false) |

---

## 8. Acceptance Criteria

G-4 is complete when **all** of the following hold:

1. `TypingConfig` has the `customKeyMap` field of type `std::array<TypingAction, 128>` with default-init to all `None`.
2. `TypingEngine::PushChar` consults `customKeyMap` before `ClassifyKey` per the dispatch hook in §3.3.
3. All 1450 pre-G-4 GTests pass on Linux unchanged.
4. New `CustomKeyMapTest.cpp` adds ≥ 12 cases covering groups G1–G7, all passing.
5. Total Linux GTest count ≥ 1463 PASS.
6. No source change in `ConfigManager.{h,cpp}`, `TypingAction.h`, or any engine handler.
7. Windows MSVC build clean (verified locally by user).

---

## 9. References

- Brainstorm session: `_bmad-output/brainstorming/brainstorming-session-2026-05-07-0250.md`
- Path G handoff: `HANDOFF.md` (top section, "2026-05-07 — Sprint 3 Path G G-3 COMPLETE")
- Predecessor PRs: #134 (G-1..G-3.2), #135 (G-3.3..G-3.4), #136 (G-3.5), #137 (G-3.6)
- TypingAction enum: `src/core/engine/TypingAction.h`
- Current dispatch site: `src/core/engine/TypingEngine.cpp:235`
- Coding rules: `docs/CODING_RULES/`
