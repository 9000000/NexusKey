# Category 14: Edge Cases

**Purpose:** Test unusual but valid input sequences.

| ID | Input | Expected | Test Name | Notes |
|----|-------|----------|-----------|-------|
| EC-01 | `w` | `w` | `EdgeCase_WAlone` | No vowel |
| EC-02 | `s` | `s` | `EdgeCase_ToneAlone` | No vowel |
| EC-03 | `bcdgh` | `bcdgh` | `EdgeCase_ConsonantsOnly` | |
| EC-04 | `asf` | `à` | `EdgeCase_ToneReplace` | Acute→Grave replace |
| EC-05 | `asr` | `ả` | `EdgeCase_ToneReplace_2` | Acute→Hook |
| EC-06 | `giuw` | `giư` | `EdgeCase_GI_Plus_U` | gi cluster |
| EC-07 | `quew` | `quew` | `EdgeCase_QU_Plus_E` | No transform |
| EC-08 | `uwaw` | `uaw` | `EdgeCase_UW_AW` | ư(uw) → ưa(uwa) → uaw (w escapes horn) |
| EC-09 | `aeiou` | `aeiou` | `EdgeCase_AllVowels` | No transform |
| EC-10 | `AEIOU` | `AEIOU` | `EdgeCase_AllVowels_Upper` | |
| EC-11 | `123` | `123` | `EdgeCase_Numbers` | Pass through |
| EC-12 | `a1b` | `a1b` | `EdgeCase_Mixed` | |
| EC-13 | (empty) | (empty) | `EdgeCase_Empty` | |
| EC-14 | ` ` | ` ` | `EdgeCase_Space` | Space handling |

---
