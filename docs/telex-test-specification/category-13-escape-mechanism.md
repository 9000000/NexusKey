# Category 13: Escape Mechanism

**Purpose:** Test that double-key clears transformation + adds literal.

### 13.1 Tone Escape

| ID | Input | Expected | Test Name | Notes |
|----|-------|----------|-----------|-------|
| ES-01 | `ass` | `as` | `Escape_Acute` | á + s → as |
| ES-02 | `aff` | `af` | `Escape_Grave` | |
| ES-03 | `arr` | `ar` | `Escape_Hook` | |
| ES-04 | `axx` | `ax` | `Escape_Tilde` | |
| ES-05 | `ajj` | `aj` | `Escape_Dot` | |
| ES-06 | `tesst` | `test` | `Escape_InWord` | Complete word |

### 13.2 Modifier Escape

| ID | Input | Expected | Test Name | Notes |
|----|-------|----------|-----------|-------|
| ES-07 | `aaa` | `aa` | `Escape_Circumflex_A` | â + a → aa |
| ES-08 | `eee` | `ee` | `Escape_Circumflex_E` | |
| ES-09 | `ooo` | `oo` | `Escape_Circumflex_O` | |
| ES-10 | `aww` | `aw` | `Escape_Breve` | ă + w → aw |
| ES-11 | `oww` | `ow` | `Escape_Horn_O` | |
| ES-12 | `uww` | `uw` | `Escape_Horn_U` | |
| ES-13 | `ddd` | `dd` | `Escape_Stroke` | đ + d → dd |
| ES-14 | `dddd` | `đd` | `Escape_Stroke_Quad` | đ + dd → đd? |

---
