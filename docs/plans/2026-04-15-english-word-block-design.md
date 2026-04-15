# English Word Block — Raw Prefix Check

**Date**: 2026-04-15
**Scope**: TelexEngine only (VNI unaffected — tone/modifier keys are digits)
**Condition**: spell check ON (`config_.spellCheckEnabled`)

## Problem

Tone/modifier keys fire BEFORE spell check validates the result. At the intermediate
step, the buffer is still valid Vietnamese, so spell check doesn't intervene.

| Word | Keystroke | Issue |
|------|-----------|-------|
| pass | p-a-**s** | `s` fires sắc on "pa" → "pá" |
| guess | g-u-e-**s** | `s` fires sắc on "gue" → "gúe" |
| power | p-o-**w** | `w` fires horn on "o" → "pơ" |
| upward | u-p-**w** | `w` fires horn on "u" → "ưp" |

## Solution — Approach A: Raw-Input Prefix Check

Check `rawInput_` suffix BEFORE tone/modifier fires. If matched, treat key as literal.
After literal insertion, spell check catches the English pattern and blocks the rest.

### Rules

| rawInput\_ suffix | Block | Words covered |
|-------------------|-------|---------------|
| `"pas"` | tone s (sắc) | pass, passing, passive, password, passion, passenger |
| `"gues"` | tone s (sắc) | guess, guessing |
| `"pow"` | modifier w | power, powerful, powder |
| `"upw"` | modifier w (horn) | upward, upload, update, upon |

### New functions in EnglishProtection.h

```cpp
[[nodiscard]] inline bool IsBlockedEnglishTone(
    const wchar_t* raw, size_t len) noexcept;

[[nodiscard]] inline bool IsBlockedEnglishModifier(
    const wchar_t* raw, size_t len) noexcept;
```

### Integration in TelexEngine.cpp PushChar

**Tone gate** — after escape check, before existing English Protection:
- `spellCheckEnabled && IsBlockedEnglishTone(rawInput_)` → check exclusion override → `asLiteral()`

**Modifier gate** — added to `blockModifiers` computation:
- `spellCheckEnabled && IsBlockedEnglishModifier(rawInput_)` → sets `blockModifiers = true`
- Existing `WouldModifierKeyMatchExclusion` provides exclusion override

### Override

User adds name (e.g. "pá") to spell exclusion list → existing exclusion mechanisms override block.

### After block fires

Key becomes literal char → spell check re-validates → Invalid (English coda) →
`spellCheckDisabled_ = true` → all subsequent tone/modifier blocked automatically.

## Test Plan

Add to `EnglishProtectionTest` fixture:
- `EnglishBlock_Pass` — "pass" → no sắc on "pa"
- `EnglishBlock_Guess` — "guess" → no sắc on "gue"
- `EnglishBlock_Power` — "power" → no horn on "po"
- `EnglishBlock_Upward` — "upward" → no horn on "up", tone still works on "up"
- `EnglishBlock_PassExclOverride` — "pá" in exclusion → sắc allowed
- `EnglishBlock_UpToneStillWorks` — "úp" (u-p-s) → sắc still fires (only horn blocked)
