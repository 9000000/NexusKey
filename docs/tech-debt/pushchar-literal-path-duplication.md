# ~~Tech Debt: PushChar "Treat as Literal" Path Duplication~~ RESOLVED

> **Resolved 2026-03-18**: Local `asLiteral` lambda added to both
> `TelexEngine::PushChar` and `VniEngine::PushChar` tone blocks.

## Location
- `src/core/engine/TelexEngine.cpp` — `PushChar()`, inside `if (IsToneKey(c))` block

## Description

The three-line "treat tone key as literal character" sequence appears 5 times
inside the tone-key handling block of `PushChar()`:

```cpp
ProcessChar(c);
UpdateSpellState();
return;
```

Each guard condition has its own copy:
1. `spellCheckDisabled_` (spell check blocked)
2. `toneEscaped_` (user escaped with double-key)
3. `engProt_.bias == HardEnglish` (hard English cluster detected)
4. `IsHardEnglishToneContext(...)` (structural V+C+V pattern — added 2026-03-18)
5. Fall-through when `ProcessTone()` returns false (no valid target)

## Suggested Fix

Replace the repeated block with a local lambda:

```cpp
auto treatAsLiteral = [&] { ProcessChar(c); UpdateSpellState(); };
```

Each guard then becomes one line:

```cpp
if (config_.spellCheckEnabled && spellCheckDisabled_) { treatAsLiteral(); return; }
if (toneEscaped_)                                     { treatAsLiteral(); return; }
if (engProt_.bias == LanguageBias::HardEnglish)       { treatAsLiteral(); return; }
if (IsHardEnglishToneContext(...))                     { engProt_.bias = LanguageBias::HardEnglish;
                                                         treatAsLiteral(); return; }
```

## Priority
Low — no functional impact, purely readability. VniEngine has the same pattern
in its own `PushChar()` but the fix should be applied to both simultaneously.
