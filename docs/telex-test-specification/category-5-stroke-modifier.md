# Category 5: Stroke Modifier (đ)

**Purpose:** Test `dd→đ` transformation.

| ID | Input | Expected | Test Name | Notes |
|----|-------|----------|-----------|-------|
| ST-01 | `dd` | `đ` | `Stroke_DD_Lower` | |
| ST-02 | `DD` | `Đ` | `Stroke_DD_Upper` | |
| ST-03 | `Dd` | `Đ` | `Stroke_DD_Mixed` | |
| ST-04 | `ddi` | `đi` | `Stroke_WithSuffix` | đi |
| ST-05 | `did` | `đi` | `Stroke_NonConsecutive` | d...d still works |
| ST-06 | `dduoc` | `đuoc` | `Stroke_InWord` | Part of đuoc |

---
