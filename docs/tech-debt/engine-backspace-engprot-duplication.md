# ~~Tech Debt: English Protection Recalculation Duplication~~ RESOLVED

> **Resolved 2026-03-18**: Extracted into `RecalcEnglishBias<>()` template in
> `src/core/engine/EngineHelpers.h`. All 6 duplicate blocks replaced.

## Location
- `src/core/engine/TelexEngine.cpp` — `Backspace()` (3 copies)
- `src/core/engine/VniEngine.cpp` — `Backspace()` (3 copies)

## Description

The English Protection recalculation block is duplicated across 3 early-return branches in each engine's `Backspace()`:
1. Quick start consonant undo (ph→f)
2. Quick consonant undo (ng→nn) — *added 2026-03-12*
3. Normal backspace

```cpp
// This block appears 6 times total (3×2 engines):
if (config_.spellCheckEnabled) {
    if (states_.size() < 2) {
        engProt_.Reset();
    } else {
        engProt_.Reset();
        CheckEnglishBias(states_.data(), states_.size(), engProt_);
    }
}
```

## Simplification

The inner `if/else` always calls `engProt_.Reset()` — only `CheckEnglishBias` is conditional:

```cpp
engProt_.Reset();
if (config_.spellCheckEnabled && states_.size() >= 2) {
    CheckEnglishBias(states_.data(), states_.size(), engProt_);
}
```

## Suggested Fix

Extract into a private method on each engine, or restructure `Backspace()` to use a single cleanup block at the end instead of early returns. The `ProcessChar` signature differs between engines (`TelexEngine::ProcessChar(wchar_t)` vs `VniEngine::ProcessChar(wchar_t, size_t)`), so a shared template in `EngineHelpers.h` is not practical for the full backspace logic.

## Priority
Low — no functional impact, purely readability.
