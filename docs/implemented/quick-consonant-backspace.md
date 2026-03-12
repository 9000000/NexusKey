# Quick Consonant: Backspace & Auto-Restore Architecture

**Date Implemented:** 2026-03-12
**Feature:** Quick Consonant (`nn` → `ng`, `cc` → `ch`, `pp` → `ph`) and its interaction with Backspace and Auto-Restore.

## 1. Problem Statement
Before this implementation, there were two major conflicts when typing with "Quick Consonant" (Gõ nhanh phụ âm kép) and "Auto-Restore" (Tự khôi phục phím với từ sai) enabled:

1. **Auto-Restore Override**: Typing `rienn` converts to `rieng`. Space commits `rieng`. In Vietnamese, `rieng` without a tone mark is flagged as an invalid word by the spell checker, causing Auto-Restore to mistakenly revert it back to `rienn`.
2. **Backspace Behavior**: Depending on when Backspace was pressed, users had to retype characters because the engine popped the converted state entirely (`aph` + BS → `ap`) or `HookEngine` rehydrated the word inappropriately (`aph` + Space + BS + BS → `app`).

## 2. Core Solutions

### A. Preventing Auto-Restore on Quick Consonants
**Files Changed:** `TelexEngine.cpp` & `VniEngine.cpp` (`Commit()`)

We added a guard clause `skipAutoRestore` to prevent the spell checker from overriding an intentional quick consonant expansion.
```cpp
bool skipAutoRestore = !config_.spellCheckEnabled
                    || !config_.autoRestoreEnabled
                    || tempSpellOff_
                    || quickConsonantIdx_ != SIZE_MAX; // ← The Fix
if (!skipAutoRestore) { ... }
```
If `quickConsonantIdx_ != SIZE_MAX`, the user *intentionally* triggered an expansion. We must trust their input and keep the expanded form (`rieng`) rather than falling back to the raw form (`rienn`).

### B. In-Place Restore During Composition
**Files Changed:** `TelexEngine.cpp` & `VniEngine.cpp` (`Backspace()`)

When backspacing *while typing* (before pressing space), users expect the original triggering key to be restored in-place (e.g., `app` → `aph`, BS → `app`).
Instead of simply popping the converted state (`h`), we:
1. Save `lastQuickConsonantKey_`.
2. Set `quickConsonantEscaped_ = true` to prevent immediate re-triggering.
3. Pop the converted character.
4. Manually `ProcessChar(originalKey)` to inject the raw character back into the engine state.

### C. Bypassing HookEngine Rehydration After Commit
**Files Changed:**
- `IInputEngine.h` (added `HasActiveQuickConsonant()`)
- `HookEngine.h` (added `lastCommittedWasQuickConsonant_`)
- `HookEngine.cpp` (`ProcessKeyDown`, `CommitComposition`)

**The Issue:** `HookEngine` has a feature called "Backspace-into-committed-word". When a word is committed, double-backspace causes HookEngine to invisibly re-type the raw keystrokes into the engine so the user can edit the word with diacritics intact. However, for quick consonants, `aph` + Space + BS + BS caused HookEngine to rehydrate `app` to `aph`, then apply the Backspace, resulting in `app` (from Solution B above) instead of `ap`.

**The Fix:** We exposed `HasActiveQuickConsonant()` on the engine interface.
```cpp
// In HookEngine::CommitComposition
lastCommittedWasQuickConsonant_ = engine_->HasActiveQuickConsonant();

// In HookEngine::ProcessKeyDown
if (!restored && (vkCode == VK_SPACE || vkCode == VK_RETURN) &&
    !lastCommittedHistory_.empty() && !lastCommittedWasQuickConsonant_) {
    commitUndoState_ = 1; // Only enable rehydration if it WASN'T a quick consonant
}
```
If the word ended in a quick consonant, we disable `commitUndoState_`. Double-backspace then drops through to standard OS character deletion, naturally resolving `aph` to `ap`!

## 3. Behavior Matrices

### Scenario 1: During Composition (Before Space)
| Input | Before | After |
|-------|--------|-------|
| `a` `p` `p`  | `aph` | `aph` |
| + `BS` | `ap` (had to retype `p`) | `app` (restored in-place) |
| + `p` | `app` | `appp` (`quickConsonantEscaped_` active) |

### Scenario 2: After Commit (After Space)
| Input | Before | After |
|-------|--------|-------|
| `a` `p` `p`  | `aph` | `aph` |
| + `Space` | `aph ` | `aph ` |
| + `BS` | `aph` (OS deletes space) | `aph` (OS deletes space) |
| + `BS` | `app` (HookEngine rehydrates) | `ap` (OS deletes `h`) |

### Scenario 3: Auto-Restore
| Input | Before | After |
|-------|--------|-------|
| `rienn` | `rieng` | `rieng` |
| + `Space` | `rienn` (`rieng` flagged invalid) | `rieng` (`skipAutoRestore` active) |

## 4. Associated Tests
**File:** `TelexEngineTest.cpp` AND `VniEngineTest.cpp`
- `QuickConsonant_PP_Backspace_Escape`
- `QuickConsonant_GG_Backspace_Escape`
- `QuickConsonant_UU_Backspace_Escape`
- `QuickConsonant_NN_Word_NotRestored` (Telex only)
- `QuickConsonant_NN_Backspace_ThenCommit_Restores` (Telex only)

All tests were updated to assert `app` instead of `ap` for in-place restoring.
