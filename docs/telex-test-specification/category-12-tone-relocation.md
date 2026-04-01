# Category 12: Tone Relocation

**Purpose:** Test that tone moves when horn is applied AFTER tone.

| ID | Input | Expected | Test Name | Notes |
|----|-------|----------|-----------|-------|
| TL-01 | `cuarw` | `cửa` | `ToneReloc_Cua_W` | ủa + w → ửa |
| TL-02 | `muas` | **`múa`** | `ToneReloc_Mua_Baseline` | Before w |
| TL-03 | `muasw` | `mứa` | `ToneReloc_Mua_W` | After w |
| TL-04 | `tuarw` | `tửa` | `ToneReloc_Tua_W` | |
| TL-05 | `huonfw` | `hườn` | `ToneReloc_Huon_W` | Complex |
| TL-06 | `nguoifw` | `ngời` | `ToneReloc_Nguoi_W` | Edge case |
| TL-07 | `thuarw` | `thửa` | `ToneReloc_Thua_Full` | |
| TL-08 | `luarw` | `lửa` | `ToneReloc_Lua` | lửa |

---
