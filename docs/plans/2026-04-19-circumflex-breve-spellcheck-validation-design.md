# Circumflex & Breve Free-Marking Spell-Check Validation

**Date:** 2026-04-19
**Status:** Design approved, ready for implementation

## Problem

Typing `gacha` produces `gâch` instead of `gacha`. Same issue with `gachw` → `găch`.

Root cause: free-marking (`aa→â`) and W-modifier (P7 `a→ă`) apply the diacritic across intervening consonants without validating that the resulting syllable is a valid Vietnamese word.

The bug generalizes: `bacha → bâch`, `macha → mâch`, `pacha → pâch`, etc. No Vietnamese syllable has the `âch` or `ăch` coda pattern, so the engine should reject these transformations.

## Scope

Fix the general class of bugs, not a narrow `gach` blacklist. Applies when:
- Spell-check is ON
- Free-marking / W-modifier crosses ≥1 consonant between target vowel and end of buffer
- Result would fail `SpellCheck::Validate`

Out of scope:
- Adjacent `aa/ee/oo` (no consonants crossed) — core Telex behavior, unchanged.
- Tone keys (s/f/r/x/j/z) — separate pipeline, already gated by English Protection.
- Spell-check OFF — existing coda heuristic at `TypingEngine.cpp:639-651` handles that path.

## Design

### Helper

```cpp
// src/core/engine/TypingEngine.cpp (private member)
bool TypingEngine::WouldBeValidSyllable(size_t targetIdx, Modifier newMod) {
    if (!config_.spellCheckEnabled) return true;
    Modifier saved = states_[targetIdx].mod;
    states_[targetIdx].mod = newMod;
    auto result = SpellCheck::Validate(states_.data(), states_.size(), config_.allowZwjf);
    states_[targetIdx].mod = saved;
    return result != SpellCheck::Result::Invalid;
}
```

### Call sites

1. **Circumflex same-vowel free-marking** (`TypingEngine.cpp:622-655`).
   Add a new branch: when `!needsValidation && consonantsCrossed >= 1 && config_.spellCheckEnabled`, tentatively validate via helper. If invalid → `return false` (key typed literal).

2. **W-modifier P5/P6/P7** (`TypingEngine.cpp:846-868`).
   For each priority target (`uIdx/targetU`, `oIdx`, `aIdx`), if there is any non-vowel state between target and end of buffer, validate via helper. If invalid → `return false`.

Return value flows back to `PushChar` dispatch at `TypingEngine.cpp:356`, which falls through to `ProcessChar(c)` — typing the modifier key as a literal character.

### Preserve existing behavior

- Escape paths (existing `Modifier::Circumflex/Breve/Horn` on target + same key to escape) run **before** the new validation guard. No change.
- `needsValidation=true` cross-vowel branch already has spell-check validation (`TypingEngine.cpp:633-638`). No change.
- Adjacent `aa→â` path at `TypingEngine.cpp:551-574` — no consonants crossed, no validation added.

## Impact Matrix

### Should still work (no regression)

| Input | Output | Why |
|---|---|---|
| `tieng` + e | tiêng | Valid syllable → helper passes |
| `cau` + a | câu | `needsValidation=true` path (vowel crossing) |
| `ban` + w | băn | Valid syllable |
| `an` + w | ăn | Valid syllable |
| `hoac` + w | hoăc | Valid |
| `gach` + s | gách | Tone path, not touched |
| `gach` + j | gạch | Tone path, not touched |

### Should be fixed

| Input | Before | After |
|---|---|---|
| `gacha` | gâch | gacha |
| `gachw` | găch | gachw |
| `bacha` | bâch | bacha |
| `macha` | mâch | macha |
| `pacha` | pâch | pacha |

## Test Plan

Add to `tests/TelexEngineTest.cpp` (fixture `TelexEngineTest`, spell-check ON):

```cpp
TEST_F(TelexEngineTest, GachaNotMarkedCircumflex) {
    TypeString(*engine_, L"gacha");
    EXPECT_EQ(engine_->Peek(), L"gacha");
}

TEST_F(TelexEngineTest, GachWNotMarkedBreve) {
    TypeString(*engine_, L"gachw");
    EXPECT_EQ(engine_->Peek(), L"gachw");
}

TEST_F(TelexEngineTest, GachSStillGivesSacTone) {
    TypeString(*engine_, L"gachs");
    EXPECT_EQ(engine_->Peek(), L"gách");
}

TEST_F(TelexEngineTest, GachJStillGivesNangTone) {
    TypeString(*engine_, L"gachj");
    EXPECT_EQ(engine_->Peek(), L"gạch");
}

TEST_F(TelexEngineTest, TiengStillFreeMarks) {
    TypeString(*engine_, L"tienge");
    EXPECT_EQ(engine_->Peek(), L"tiêng");
}

TEST_F(TelexEngineTest, BanwStillGivesBreve) {
    TypeString(*engine_, L"banw");
    EXPECT_EQ(engine_->Peek(), L"băn");
}

TEST_F(TelexEngineTest, BachaNotMarkedCircumflex) {
    TypeString(*engine_, L"bacha");
    EXPECT_EQ(engine_->Peek(), L"bacha");
}

TEST_F(TelexEngineTest, MachaNotMarkedCircumflex) {
    TypeString(*engine_, L"macha");
    EXPECT_EQ(engine_->Peek(), L"macha");
}
```

Also run the full `TelexEngineTest.*` and `FeatureOptionsTest.*` suites to catch regressions.

## Tradeoffs

- **Cost:** Up to 4 extra `SpellCheck::Validate` calls per keystroke in worst case (1 circumflex + 3 W-modifier). Validate is a table lookup — negligible.
- **Rollback:** No revert needed if a rare word breaks — users add to `spellExclusions` as escape hatch.
- **Coverage:** Does nothing when spell-check is OFF. Existing OFF-path coda heuristic (single-consonant check) stays as-is.

## Implementation Checklist

1. Add `WouldBeValidSyllable` helper to `TypingEngine` (header + source).
2. Wire into circumflex same-vowel branch (`TypingEngine.cpp:~632`).
3. Wire into W-modifier P5 (`~846`), P6 (`~857`), P7 (`~864`).
4. Add tests listed above.
5. Build Linux tests, run full suite.
6. Manual smoke test on Windows hook path with `gacha`, `bacha`, `gachw`, plus the non-regression set.
