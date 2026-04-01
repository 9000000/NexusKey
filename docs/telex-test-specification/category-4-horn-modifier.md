# Category 4: Horn Modifier (̛)

**Purpose:** Test `ow→ơ`, `uw→ư`, and auto-ươ transformation.

### 4.1 Basic Horn

| ID | Input | Expected | Test Name | Notes |
|----|-------|----------|-----------|-------|
| HN-01 | `ow` | `ơ` | `Horn_OW_Lower` | |
| HN-02 | `OW` | `Ơ` | `Horn_OW_Upper` | |
| HN-03 | `uw` | `ư` | `Horn_UW_Lower` | |
| HN-04 | `UW` | `Ư` | `Horn_UW_Upper` | |
| HN-05 | `tow` | `tơ` | `Horn_OW_WithPrefix` | |
| HN-06 | `tuw` | `tư` | `Horn_UW_WithPrefix` | |

### 4.2 Auto-ươ Transformation

| ID | Input | Expected | Test Name | Notes |
|----|-------|----------|-----------|-------|
| HN-07 | `uow` | `uơ` | `Horn_UO_NoAuto` | No char after = no auto |
| HN-08 | `uown` | `ươn` | `Horn_UO_AutoWithN` | Auto triggers |
| HN-09 | `uowc` | `ươc` | `Horn_UO_AutoWithC` | Auto triggers |
| HN-10 | `huonw` | `hươn` | `Horn_UO_DelayedW` | W after consonant |
| HN-11 | `dduowng` | `đương` | `Horn_UO_FullWord` | đường without tone |

### 4.3 Double-W Pattern

| ID | Input | Expected | Test Name | Notes |
|----|-------|----------|-----------|-------|
| HN-12 | `uoww` | `ươ` | `Horn_DoubleW` | Second W adds horn to U |
| HN-13 | `huoww` | `hươ` | `Horn_DoubleW_WithPrefix` | |

### 4.4 Horn with Circumflex Removal

| ID | Input | Expected | Test Name | Notes |
|----|-------|----------|-----------|-------|
| HN-14 | `giuaaw` | `giưa` | `Horn_RemovesCircumflex` | â→a when ư created |
| HN-15 | `cuaaw` | `cưa` | `Horn_RemovesCircumflex_2` | |

---
