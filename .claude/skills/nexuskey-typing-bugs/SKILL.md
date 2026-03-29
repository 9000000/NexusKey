---
name: nexuskey-typing-bugs
description: Use when diagnosing Vietnamese typing engine bugs — wrong tone placement, English words mangled, dd/w modifiers misfiring, abbreviations not working, or Vietnamese chars not appearing. Covers TelexEngine PushChar pipeline tracing, diphthong table analysis, English protection gaps, and modifier escape issues.
---

# NexusKey Typing Engine Bug Analysis

## Overview

Systematic approach to diagnosing and fixing Vietnamese typing engine bugs. All bugs trace to one of 5 subsystems in the PushChar() pipeline. Identify which subsystem, trace the data flow, fix, test.

## When to Use

- Wrong tone placement (dấu đặt sai vị trí)
- English word gets Vietnamese diacritics (dropdown → đrơpdn)
- Vietnamese modifier doesn't fire (dd should → đ but doesn't)
- Modifier fires when it shouldn't (English word gets modified)
- Abbreviation typing broken (vđề, cđề not working)
- Escape mechanism broken (ww, dd escape not undoing correctly)

## PushChar() Pipeline (trace bugs here)

```
PushChar(c)
  ├─ 0. Quick consonant (cc→ch, gg→gi, uu→ươ)
  ├─ 1. Tone keys (s,f,r,x,j,z)
  │     ├─ Gate: spellCheckDisabled_ → literal
  │     ├─ Gate: toneEscaped_ → literal
  │     ├─ Gate: HardEnglish bias → literal
  │     ├─ Gate: SoftEnglish + insistence → literal/allow
  │     ├─ Gate: IsHardEnglishToneContext (V+C+V) → literal
  │     └─ ProcessTone → FindToneTarget → place tone
  ├─ 2. Modifier keys (d,w,[,],aa,ee,oo)
  │     ├─ Pre-check: coda + 'd' = invalid? → HardEnglish
  │     ├─ Gate: toneEscaped_ || HardEnglish → literal
  │     ├─ Gate: spellCheck + disabled → literal
  │     └─ ProcessModifier → ProcessDModifier / ProcessWModifier / circumflex
  ├─ 2b. Quick end consonant (g→ng, h→nh, k→ch)
  └─ 3. Regular character → ProcessChar
        └─ CheckEnglishBias (Tier 1 + Tier 2)
```

## Bug Category → Subsystem Map

| Symptom | Subsystem | Key files |
|---------|-----------|-----------|
| Tone on wrong vowel | FindToneTarget + diphthong tables | TelexEngine.cpp, VietnameseTables.h |
| English word gets tone | English Protection gates (step 1) | EnglishProtection.h |
| English word gets modifier | English Protection gates (step 2) | EnglishProtection.h, TelexEngine.cpp |
| dd→đ not working | ProcessDModifier + FindStrokeDTarget | EngineHelpers.h, TelexEngine.cpp |
| dd→đ fires on English word | Coda pre-check + English bias | TelexEngine.cpp (pre-check before step 2) |
| w modifier wrong | ProcessWModifier priority levels | TelexEngine.cpp (P1-P8) |
| Escape not undoing | ProcessDModifier/ProcessWModifier escape path | TelexEngine.cpp |
| Quick consonant wrong | Quick consonant logic (step 0) | TelexEngine.cpp |

## Diagnostic Recipes

### Recipe 1: Wrong Tone Placement

**Trace:** FindToneTargetImpl in TelexEngine.cpp

1. List vowels in states_ (skip "gi"/"qu" cluster vowels)
2. Check P1: any horn vowel? → tone goes there
3. Check P2: any modified vowel (â,ê,ô,ă)? → tone goes there
4. Check P3: diphthong table lookup in VietnameseTables.h
   - `kDiphthongClassic[first_vowel][second_vowel]` → rule 1/2/3
   - Rule 1 = FIRST, Rule 2 = SECOND, Rule 3 = coda-aware (SECOND if coda, FIRST if no coda)
5. Check P4: default → rightmost vowel

**Common fix:** Change diphthong table entry. Example: "yu" was rule 1 (always first=y), fixed to rule 3 (coda-aware: yủn gets u, khuỷu gets y).

### Recipe 2: English Word Mangled

**Trace:** CheckEnglishBias in EnglishProtection.h + gates in PushChar

1. What sets HardEnglish?
   - IsHardEnglishStart: impossible consonant cluster (cl, cr, br, dr, sp, st...)
   - IsHardEnglishEnd: impossible final consonant (x, r, z, f, s, j)
   - IsInvalidVietnameseCoda: 2+ consonants after vowel not ch/ng/nh
   - IsHardEnglishToneContext: V+C+V pattern anywhere in buffer
2. Is bias already HardEnglish when modifier/tone fires? Check line 228 gate.
3. Special: `d` at position 0 is EXEMPT from start cluster check (`!states[0].IsD()`)
4. Pre-check: `d` modifier blocked when existing coda + d = invalid (e.g., "drop" + d → "pd")

**Common fix:** Add pattern to detection functions, or add pre-check before modifier.

### Recipe 3: Modifier Not Firing

**Trace:** ProcessModifier → ProcessDModifier or ProcessWModifier

For dd→đ:
1. FindStrokeDTarget: scans backward for last 'd'
2. Check: preceded by vowel (through contiguous d cluster)? → blocked
3. Check: dModifierEscaped_? → blocked
4. Check: bias == HardEnglish? → blocked (gate at line 228)

For w modifier:
1. ProcessWModifier: check P1-P8 priority order
2. Which pattern matches? (ua, uo, oa, escape, standalone u/o/a, P8 synthetic)
3. Is canPromoteUO preventing escape? (horned 'o' + unmodified 'u' → skip escape, apply P5)

### Recipe 4: Escape Not Undoing Correctly

**Check order of operations in escape path:**
- UndoHornU must run BEFORE clearing the modifier (checks HasModifier on the target)
- toneEscaped_ must be set AFTER escape (blocks further modifiers)
- ProcessChar(c) adds the literal key after escape

**Common fix:** Swap order of UndoHornU and modifier clear. (Real bug: UndoHornU checked HasModifier() after it was already cleared → companion vowel not undone.)

## Diphthong Table Quick Reference

```
kDiphthongClassic[6][6]:  a  e  i  o  u  y
                    a  [  0  0  1  1  1  1 ]
                    e  [  0  0  1  1  1  0 ]
                    i  [  1  0  1  0  1  0 ]
                    o  [  3  3  1  0  1  0 ]
                    u  [  1  1  1  0  1  2 ]
                    y  [  0  0  0  0  3  0 ]

0=no rule, 1=FIRST, 2=SECOND, 3=CODA_AWARE(coda→2, no coda→1)
```

## Testing Pattern

Always write a test BEFORE fixing:
```cpp
TEST_F(EnglishDetectionNoSpellCheckTest, DescriptiveName) {
    TypeString(*engine_, L"input_keys");
    EXPECT_EQ(engine_->Peek(), L"expected_output");
}
```

Use `EnglishDetectionNoSpellCheckTest` fixture (spell check OFF) for English protection tests.
Use `TelexEngineTest` fixture (spell check ON) for general typing tests.

Run: `./build-linux/tests/NextKeyTests --gtest_filter="*TestName*"`

## Common Mistakes

- Checking states_ AFTER ProcessChar (too late — modifier already fired)
- Forgetting IsD() treats 'd' as vowel-like in coda scans
- UndoHornU called after modifier cleared (checks HasModifier which is already None)
- Diphthong rule 1 when rule 3 (coda-aware) is correct
- Pre-check fires for ALL keys when it should only fire for modifier candidates
- Forgetting to update BOTH kDiphthongClassic AND kDiphthongModern tables
