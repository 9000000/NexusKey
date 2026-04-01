# Category 2: Circumflex Modifier (^)

**Purpose:** Test double-vowel → circumflex transformation.

### 2.1 Basic Circumflex

| ID | Input | Expected | Test Name | Notes |
|----|-------|----------|-----------|-------|
| CF-01 | `aa` | `â` | `Circumflex_AA_Lower` | |
| CF-02 | `AA` | `Â` | `Circumflex_AA_Upper` | Case preservation |
| CF-03 | `Aa` | `Â` | `Circumflex_AA_Mixed` | Second char case |
| CF-04 | `ee` | `ê` | `Circumflex_EE_Lower` | |
| CF-05 | `EE` | `Ê` | `Circumflex_EE_Upper` | |
| CF-06 | `oo` | `ô` | `Circumflex_OO_Lower` | |
| CF-07 | `OO` | `Ô` | `Circumflex_OO_Upper` | |

### 2.2 Circumflex in Context

| ID | Input | Expected | Test Name | Notes |
|----|-------|----------|-----------|-------|
| CF-08 | `caa` | `câ` | `Circumflex_WithPrefix` | After consonant |
| CF-09 | `coo` | `cô` | `Circumflex_WithPrefix_O` | |
| CF-10 | `vieet` | `việt` | `Circumflex_InWord` | Full word |
| CF-11 | `tieeng` | `tiếng` | `Circumflex_Complex` | With tone |
| CF-12 | `aab` | `âb` | `Circumflex_WithSuffix` | Before consonant |

---
