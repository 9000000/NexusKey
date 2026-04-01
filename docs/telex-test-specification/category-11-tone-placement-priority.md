# Category 11: Tone Placement Priority

**Purpose:** Verify strict priority: Horn > Modified > Diphthong > Default.

### 11.1 Priority 1: Horn Vowel (Last Horn)

| ID | Input | Expected | Test Name | Tone On |
|----|-------|----------|-----------|---------|
| TP-01 | `uowf` | `uờ` | `TonePriority_1_Horn_Last` | ơ (no auto-ươ) |
| TP-02 | `uowcj` | `ượ` | `TonePriority_1_Horn_WithCoda` | ơ |
| TP-03 | `dduowcj` | `được` | `TonePriority_1_UO_Cluster` | ơ |
| TP-04 | `nguowif` | `người` | `TonePriority_1_Nguoi` | ơ |
| TP-05 | `muowif` | `mười` | `TonePriority_1_Muoi` | ơ |

### 11.2 Priority 2: Modified Vowel

| ID | Input | Expected | Test Name | Tone On |
|----|-------|----------|-----------|---------|
| TP-06 | `caaps` | `cấp` | `TonePriority_2_Circumflex` | â |
| TP-07 | `beepj` | `bệp` | `TonePriority_2_E_Circum` | ê |
| TP-08 | `hoopj` | `họp` | `TonePriority_2_O_Circum` | Wait, hộp? |
| TP-09 | `awcs` | `ắc` | `TonePriority_2_Breve` | ă |

### 11.3 Priority 3: Diphthong Rules

| ID | Input | Expected | Test Name | Tone On | Rule |
|----|-------|----------|-----------|---------|------|
| TP-10 | `caos` | `cáo` | `TonePriority_3_Falling_AO` | a | Falling |
| TP-11 | `hoas` | `hoá` | `TonePriority_3_Rising_OA` | a | Rising |
| TP-12 | `cuir` | `cúi` | `TonePriority_3_Falling_UI` | u | Falling |
| TP-13 | `quyf` | `quỳ` | `TonePriority_3_Rising_UY` | y | Rising |

### 11.4 Priority 4: Default (Rightmost)

| ID | Input | Expected | Test Name |
|----|-------|----------|-----------|
| TP-14 | `mas` | `má` | `TonePriority_4_Default` |
| TP-15 | `mef` | `mè` | `TonePriority_4_Default_E` |
| TP-16 | `mir` | `mỉ` | `TonePriority_4_Default_I` |
| TP-17 | `mox` | `mõ` | `TonePriority_4_Default_O` |
| TP-18 | `muj` | `mụ` | `TonePriority_4_Default_U` |

---
