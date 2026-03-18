# NexusKey Changes — English Detection & Multi-word Backward

## Overview

Based on user feedback about Vietnamese/English typing experience:
1. English detection now works independently of spell check (always ON)
2. Tone escape (ss, ff, etc.) disables Vietnamese composition for the rest of the word
3. Multi-word backward: backspace through multiple committed words to re-edit them
4. Settings dialog now shows by default on startup

---

## 1. English Detection — Always Active

**Problem:** English detection (blocking tones for "class", "brown", etc.) only worked when spell check was enabled. Users who disabled spell check (to allow abbreviations like "mtrường") lost all English protection.

**Fix:** Removed `config_.spellCheckEnabled` guard from English bias checks. English detection now runs independently.

### Files changed:
- `src/core/engine/TelexEngine.cpp` — Ungated 3 English bias checks in PushChar + 2 in Backspace
- `src/core/engine/VniEngine.cpp` — Same 3+2 changes
- `src/core/engine/EnglishProtection.h` — Updated header comment

### How it works:
- **Spell check** gates: syllable validation (`spellCheckDisabled_`), auto-restore
- **English detection** gates: HardEnglish (impossible clusters), SoftEnglish (ambiguous y+vowel)
- Both can work simultaneously or independently

---

## 2. Tone Escape — Disables Vietnamese for Rest of Word

**Problem:** Typing "dashboard" (d-a-s-s-h-b-o-a-r-d) — user pressed `ss` to cancel the tone on 'a', but the engine still applied 'r' as Hook tone and 'dd' as đ modifier. The `engProt_.bias` was stuck at `Vietnamese` after the first tone application.

**Fix (2 parts):**

### A. Bias reset after escape
After tone escape, `RecalcEnglishBias()` re-evaluates from scratch instead of staying `Vietnamese`.

### B. Persistent `toneEscaped_` flag
When user presses same tone key twice (ss, ff, rr, xx, jj):
- `toneEscaped_` flag is set to `true`
- All subsequent tone keys → treated as literal characters
- All subsequent modifier keys (aa→â, dd→đ, w→ơ, etc.) → treated as literal
- Flag cleared on: `Reset()` (new word) or `Backspace()` (user can undo)

### Files changed:
- `src/core/engine/TelexEngine.h` — Added `toneEscaped_` field
- `src/core/engine/TelexEngine.cpp` — Set flag in ProcessTone escape, check in PushChar gates, clear in Reset/Backspace
- `src/core/engine/VniEngine.h` — Same field
- `src/core/engine/VniEngine.cpp` — Same logic

### Example:
```
"dashboard": d-a-s(tone)-s(escape)-h-b-o-a-r-d
Before: đẩhbo (mangled — r applied as tone, dd as đ, aa as â)
After:  dashboard (all chars literal after ss escape)
```

---

## 3. Code Deduplication — RecalcEnglishBias Helper

**Problem:** 4 identical English protection recalculation blocks (2 in TelexEngine, 2 in VniEngine).

**Fix:** Extracted to `RecalcEnglishBias<>()` template in `EngineHelpers.h`.

### Files changed:
- `src/core/engine/EngineHelpers.h` — Added `RecalcEnglishBias()` template
- `src/core/engine/TelexEngine.cpp` — 2 blocks → 2 calls
- `src/core/engine/VniEngine.cpp` — 2 blocks → 2 calls

---

## 4. Multi-word Backward Stack (HookEngine)

**Problem:** After committing "test" with space and typing "ca", backspacing through "ca" and the space couldn't restore "test" for re-editing. The single-entry undo history was cleared when the new word started.

**Fix:** Replace single `lastCommitted*` fields with a `CommitEntry` struct + `commitStack_` (LIFO, max 3 entries).

### Data structure:
```cpp
struct CommitEntry {
    std::vector<wchar_t> history;   // User keystrokes for replay
    std::wstring text;              // What was on screen when committed
    std::vector<uint8_t> widths;    // Encoded widths for non-Unicode code tables
};
std::vector<CommitEntry> commitStack_;  // Max kMaxCommitStack (3)
```

### State machine (enhanced):
```
Commit "test" + space → push to stack, state 1
Type "ca"             → state 0 (cleared on new char — safety net)
BS×2 (delete "ca")    → engine empty → state 1 (lazy re-entry from HandleBackspace)
BS                    → state 2 (deletes space)
BS                    → replay "test" + backspace → "tes"
Continue BS...        → engine empty → state 1 (stack has more? continue chain)
```

### Key design decisions:
- **Lines 535-538 KEPT** — state clears on new char (prevents accidental replay)
- **Lazy state 1** — `HandleBackspace()` sets state 1 when engine empty + stack non-empty
- **Skip push for:** auto-restored words, quick consonant words, empty history
- **Stack cleared on:** focus change, mouse click, mode toggle, `ResetComposition()`

### Files changed:
- `src/app/system/HookEngine.h` — `CommitEntry` struct, `commitStack_`, `pushedToStack_`
- `src/app/system/HookEngine.cpp` — CommitComposition (push), HandleBackspace (lazy state 1), ReplayCommittedChars (pop from stack), ResetComposition (clear stack)

---

## 5. Settings Dialog Default ON

**Change:** `showOnStartup` default changed from `false` to `true`.

**File:** `src/core/SystemConfig.h` line 29

---

## Tests

11 new tests added in `EnglishDetectionNoSpellCheckTest`:
- HardReject clusters (FL, CL, BR, SP, GL) block tones/modifiers without spell check
- Valid Vietnamese still composes (thương, đầu)
- Backspace resets protection
- **Tone escape blocks subsequent tones** (das + s → literal r)
- **Tone escape blocks modifiers** (dashboard case)
- **Backspace clears escape flag** (recoverable)

**Total: 726 tests, 0 regressions.**
