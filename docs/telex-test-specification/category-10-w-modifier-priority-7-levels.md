# Category 10: W-Modifier Priority (7 Levels)

**Purpose:** Verify strict priority order when 'w' is pressed.

| ID | Priority | Input | Expected | Test Name | Rule |
|----|----------|-------|----------|-----------|------|
| WP-01 | 1 | `muaw` | `mưa` | `WPriority_1_UA_Horn` | ua→ưa |
| WP-02 | 1 | `cuaw` | `cưa` | `WPriority_1_UA_Horn_2` | ua→ưa |
| WP-03 | 1 | `thuaw` | `thưa` | `WPriority_1_UA_Horn_3` | ua→ưa |
| WP-04 | 2 | `hoaw` | `hoă` | `WPriority_2_OA_Breve` | oa→oă |
| WP-05 | 2 | `toaw` | `toă` | `WPriority_2_OA_Breve_2` | oa→oă |
| WP-06 | 3 | `tuw` | `tư` | `WPriority_3_SingleU` | u→ư |
| WP-07 | 3 | `suw` | `sư` | `WPriority_3_SingleU_2` | |
| WP-08 | 4 | `tow` | `tơ` | `WPriority_4_SingleO` | o→ơ |
| WP-09 | 4 | `sow` | `sơ` | `WPriority_4_SingleO_2` | |
| WP-10 | 5 | `taw` | `tă` | `WPriority_5_SingleA` | a→ă |
| WP-11 | 5 | `law` | `lă` | `WPriority_5_SingleA_2` | |
| WP-12 | 6 | `uww` | `uw` | `WPriority_6_Escape_U` | Clear ư |
| WP-13 | 6 | `oww` | `ow` | `WPriority_6_Escape_O` | Clear ơ |
| WP-14 | 6 | `aww` | `aw` | `WPriority_6_Escape_A` | Clear ă |

---
