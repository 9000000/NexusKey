# TelexEngine Comprehensive Test Case Specification

**Version:** 1.0  
**Date:** 2026-02-05  
**Purpose:** Complete test coverage BEFORE refactoring  
**Reference:** `docs/vietnamese-phonology-spec.md`

---

## Test Organization Strategy

```
Tests are organized by PHONOLOGICAL CATEGORY, not by function.
This ensures we test the LANGUAGE RULES, not the implementation.
```

| Category | # Tests | Priority | Coverage |
|----------|---------|----------|----------|
| 1. Basic Vowels | 6 | High | Foundation |
| 2. Circumflex | 12 | High | aa→â, ee→ê, oo→ô |
| 3. Breve | 6 | High | aw→ă |
| 4. Horn | 15 | Critical | ow→ơ, uw→ư, auto-ươ |
| 5. Stroke | 6 | Medium | dd→đ |
| 6. Tones | 30 | High | s,f,r,x,j on all vowels |
| 7. Falling Diphthongs | 24 | Critical | ai,ao,au,ay... |
| 8. Rising Diphthongs | 12 | Critical | oa,oe,uy,uê |
| 9. Triphthongs | 16 | High | iêu,ươi,oai... |
| 10. W-Modifier Priority | 14 | Critical | 7-level priority |
| 11. Tone Placement | 18 | Critical | Horn>Modified>Diphthong>Default |
| 12. Tone Relocation | 8 | High | After horn applied |
| 13. Escape Mechanism | 14 | High | Double-key escape |
| 14. Edge Cases | 20 | Medium | Complex words |
| 15. Real Words | 40 | Critical | Dictionary validation |
| **TOTAL** | **~220** | | |

---

## Category 1: Basic Vowels (No Transformation)

**Purpose:** Verify vowels pass through unchanged when no Telex key is pressed.

| ID | Input | Expected | Test Name | Notes |
|----|-------|----------|-----------|-------|
| BV-01 | `a` | `a` | `BasicVowel_A` | Plain vowel |
| BV-02 | `e` | `e` | `BasicVowel_E` | |
| BV-03 | `i` | `i` | `BasicVowel_I` | |
| BV-04 | `o` | `o` | `BasicVowel_O` | |
| BV-05 | `u` | `u` | `BasicVowel_U` | |
| BV-06 | `y` | `y` | `BasicVowel_Y` | |

---

## Category 2: Circumflex Modifier (^)

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

## Category 3: Breve Modifier (˘)

**Purpose:** Test `aw` → `ă` transformation.

| ID | Input | Expected | Test Name | Notes |
|----|-------|----------|-----------|-------|
| BR-01 | `aw` | `ă` | `Breve_AW_Lower` | |
| BR-02 | `AW` | `Ă` | `Breve_AW_Upper` | |
| BR-03 | `Aw` | `Ă` | `Breve_AW_Mixed` | |
| BR-04 | `taw` | `tă` | `Breve_WithPrefix` | |
| BR-05 | `nawm` | `năm` | `Breve_InWord` | năm |
| BR-06 | `awc` | `ăc` | `Breve_WithSuffix` | |

---

## Category 4: Horn Modifier (̛)

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

## Category 5: Stroke Modifier (đ)

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

## Category 6: Tone Marks

**Purpose:** Test all 5 tones on all 12 vowels (60 combinations).

### 6.1 Acute Tone (s - Sắc)

| ID | Input | Expected | Test Name |
|----|-------|----------|-----------|
| TN-S01 | `as` | `á` | `Tone_Acute_A` |
| TN-S02 | `aas` | `ấ` | `Tone_Acute_Circumflex_A` |
| TN-S03 | `aws` | `ắ` | `Tone_Acute_Breve_A` |
| TN-S04 | `es` | `é` | `Tone_Acute_E` |
| TN-S05 | `ees` | `ế` | `Tone_Acute_Circumflex_E` |
| TN-S06 | `is` | `í` | `Tone_Acute_I` |
| TN-S07 | `os` | `ó` | `Tone_Acute_O` |
| TN-S08 | `oos` | `ố` | `Tone_Acute_Circumflex_O` |
| TN-S09 | `ows` | `ớ` | `Tone_Acute_Horn_O` |
| TN-S10 | `us` | `ú` | `Tone_Acute_U` |
| TN-S11 | `uws` | `ứ` | `Tone_Acute_Horn_U` |
| TN-S12 | `ys` | `ý` | `Tone_Acute_Y` |

### 6.2 Grave Tone (f - Huyền)

| ID | Input | Expected | Test Name |
|----|-------|----------|-----------|
| TN-F01 | `af` | `à` | `Tone_Grave_A` |
| TN-F02 | `aaf` | `ầ` | `Tone_Grave_Circumflex_A` |
| TN-F03 | `awf` | `ằ` | `Tone_Grave_Breve_A` |
| TN-F04 | `ef` | `è` | `Tone_Grave_E` |
| TN-F05 | `eef` | `ề` | `Tone_Grave_Circumflex_E` |
| TN-F06 | `if` | `ì` | `Tone_Grave_I` |
| TN-F07 | `of` | `ò` | `Tone_Grave_O` |
| TN-F08 | `oof` | `ồ` | `Tone_Grave_Circumflex_O` |
| TN-F09 | `owf` | `ờ` | `Tone_Grave_Horn_O` |
| TN-F10 | `uf` | `ù` | `Tone_Grave_U` |
| TN-F11 | `uwf` | `ừ` | `Tone_Grave_Horn_U` |
| TN-F12 | `yf` | `ỳ` | `Tone_Grave_Y` |

### 6.3 Hook Tone (r - Hỏi)

| ID | Input | Expected | Test Name |
|----|-------|----------|-----------|
| TN-R01 | `ar` | `ả` | `Tone_Hook_A` |
| TN-R02 | `aar` | `ẩ` | `Tone_Hook_Circumflex_A` |
| TN-R03 | `awr` | `ẳ` | `Tone_Hook_Breve_A` |
| TN-R04 | `er` | `ẻ` | `Tone_Hook_E` |
| TN-R05 | `eer` | `ể` | `Tone_Hook_Circumflex_E` |
| TN-R06 | `ir` | `ỉ` | `Tone_Hook_I` |
| TN-R07 | `or` | `ỏ` | `Tone_Hook_O` |
| TN-R08 | `oor` | `ổ` | `Tone_Hook_Circumflex_O` |
| TN-R09 | `owr` | `ở` | `Tone_Hook_Horn_O` |
| TN-R10 | `ur` | `ủ` | `Tone_Hook_U` |
| TN-R11 | `uwr` | `ử` | `Tone_Hook_Horn_U` |
| TN-R12 | `yr` | `ỷ` | `Tone_Hook_Y` |

### 6.4 Tilde Tone (x - Ngã)

| ID | Input | Expected | Test Name |
|----|-------|----------|-----------|
| TN-X01 | `ax` | `ã` | `Tone_Tilde_A` |
| TN-X02 | `aax` | `ẫ` | `Tone_Tilde_Circumflex_A` |
| TN-X03 | `awx` | `ẵ` | `Tone_Tilde_Breve_A` |
| TN-X04 | `ex` | `ẽ` | `Tone_Tilde_E` |
| TN-X05 | `eex` | `ễ` | `Tone_Tilde_Circumflex_E` |
| TN-X06 | `ix` | `ĩ` | `Tone_Tilde_I` |
| TN-X07 | `ox` | `õ` | `Tone_Tilde_O` |
| TN-X08 | `oox` | `ỗ` | `Tone_Tilde_Circumflex_O` |
| TN-X09 | `owx` | `ỡ` | `Tone_Tilde_Horn_O` |
| TN-X10 | `ux` | `ũ` | `Tone_Tilde_U` |
| TN-X11 | `uwx` | `ữ` | `Tone_Tilde_Horn_U` |
| TN-X12 | `yx` | `ỹ` | `Tone_Tilde_Y` |

### 6.5 Dot Below Tone (j - Nặng)

| ID | Input | Expected | Test Name |
|----|-------|----------|-----------|
| TN-J01 | `aj` | `ạ` | `Tone_Dot_A` |
| TN-J02 | `aaj` | `ậ` | `Tone_Dot_Circumflex_A` |
| TN-J03 | `awj` | `ặ` | `Tone_Dot_Breve_A` |
| TN-J04 | `ej` | `ẹ` | `Tone_Dot_E` |
| TN-J05 | `eej` | `ệ` | `Tone_Dot_Circumflex_E` |
| TN-J06 | `ij` | `ị` | `Tone_Dot_I` |
| TN-J07 | `oj` | `ọ` | `Tone_Dot_O` |
| TN-J08 | `ooj` | `ộ` | `Tone_Dot_Circumflex_O` |
| TN-J09 | `owj` | `ợ` | `Tone_Dot_Horn_O` |
| TN-J10 | `uj` | `ụ` | `Tone_Dot_U` |
| TN-J11 | `uwj` | `ự` | `Tone_Dot_Horn_U` |
| TN-J12 | `yj` | `ỵ` | `Tone_Dot_Y` |

---

## Category 7: Falling Diphthongs (Tone on FIRST vowel)

**Purpose:** Test diphthongs where tone goes on the first vowel.

| ID | Pattern | Input | Expected | Test Name | Notes |
|----|---------|-------|----------|-----------|-------|
| FD-01 | ai | `ais` | `ái` | `FallingDiphthong_AI_Acute` | háy, tái |
| FD-02 | ai | `air` | `ải` | `FallingDiphthong_AI_Hook` | |
| FD-03 | ao | `aos` | `áo` | `FallingDiphthong_AO_Acute` | sáo, tráo |
| FD-04 | ao | `aof` | `ào` | `FallingDiphthong_AO_Grave` | |
| FD-05 | au | `aus` | `áu` | `FallingDiphthong_AU_Acute` | dấu, châu |
| FD-06 | au | `aur` | `ảu` | `FallingDiphthong_AU_Hook` | |
| FD-07 | ay | `ays` | `áy` | `FallingDiphthong_AY_Acute` | tây, ngày |
| FD-08 | ay | `ayf` | `ày` | `FallingDiphthong_AY_Grave` | |
| FD-09 | âu | `aaus` | `ấu` | `FallingDiphthong_AU_Circum` | dấu |
| FD-10 | ây | `aays` | `ấy` | `FallingDiphthong_AY_Circum` | mấy |
| FD-11 | eo | `eos` | `éo` | `FallingDiphthong_EO_Acute` | kéo |
| FD-12 | eo | `eor` | `ẻo` | `FallingDiphthong_EO_Hook` | |
| FD-13 | êu | `eeus` | `ếu` | `FallingDiphthong_EU_Circum` | nếu |
| FD-14 | iu | `ius` | `íu` | `FallingDiphthong_IU_Acute` | |
| FD-15 | iu | `iuj` | `ịu` | `FallingDiphthong_IU_Dot` | dịu |
| FD-16 | oi | `ois` | `ói` | `FallingDiphthong_OI_Acute` | |
| FD-17 | oi | `oij` | `ọi` | `FallingDiphthong_OI_Dot` | gọi |
| FD-18 | ôi | `oois` | `ối` | `FallingDiphthong_OI_Circum` | tối |
| FD-19 | ơi | `owis` | `ới` | `FallingDiphthong_OI_Horn` | trời |
| FD-20 | ui | `uis` | `úi` | `FallingDiphthong_UI_Acute` | |
| FD-21 | ui | `uij` | `ụi` | `FallingDiphthong_UI_Dot` | |
| FD-22 | ưi | `uwix` | `ữi` | `FallingDiphthong_UI_Horn` | gửi |
| FD-23 | ua | `uar` | `ủa` | `FallingDiphthong_UA` | của |
| FD-24 | uê | `uees` | `uế` | `FallingDiphthong_UE` | huế |

---

## Category 8: Rising Diphthongs (Tone on SECOND vowel)

**Purpose:** Test diphthongs where tone goes on the second vowel.

| ID | Pattern | Input | Expected | Test Name | Notes |
|----|---------|-------|----------|-----------|-------|
| RD-01 | oa | `oas` | `oá` | `RisingDiphthong_OA_Acute` | |
| RD-02 | oa | `oaf` | `oà` | `RisingDiphthong_OA_Grave` | hoà |
| RD-03 | oa | `oar` | `oả` | `RisingDiphthong_OA_Hook` | |
| RD-04 | oe | `oex` | `oẽ` | `RisingDiphthong_OE_Tilde` | xoẽ |
| RD-05 | oe | `oef` | `oè` | `RisingDiphthong_OE_Grave` | xòe |
| RD-06 | uy | `uys` | `uý` | `RisingDiphthong_UY_Acute` | |
| RD-07 | uy | `uyf` | `uỳ` | `RisingDiphthong_UY_Grave` | quỳ |
| RD-08 | uy | `uyj` | `uỵ` | `RisingDiphthong_UY_Dot` | |
| RD-09 | uê | `ueesf` | `uề` | `RisingDiphthong_UE_Circum` | huề |
| RD-10 | uê | `ueess` | `uế` | `RisingDiphthong_UE_Acute` | huế |
| RD-11 | oa | `hoaf` | `hoà` | `RisingDiphthong_Hoa` | Full word |
| RD-12 | uy | `quyf` | `quỳ` | `RisingDiphthong_Quy` | Full word |

---

## Category 9: Triphthongs

**Purpose:** Test 3-vowel clusters with correct tone placement.

| ID | Pattern | Input | Expected | Test Name | Notes |
|----|---------|-------|----------|-----------|-------|
| TR-01 | iêu | `ieeus` | `iếu` | `Triphthong_IEU_Acute` | tiếu |
| TR-02 | iêu | `ieeuf` | `iều` | `Triphthong_IEU_Grave` | tiều |
| TR-03 | yêu | `yeeus` | `yếu` | `Triphthong_YEU_Acute` | yếu |
| TR-04 | yêu | `yeeuf` | `yều` | `Triphthong_YEU_Grave` | |
| TR-05 | uôi | `uoois` | `uối` | `Triphthong_UOI_Acute` | muối |
| TR-06 | uôi | `uooir` | `uổi` | `Triphthong_UOI_Hook` | tuổi |
| TR-07 | ươi | `uowis` | `ưới` | `Triphthong_UOI_Horn` | tưới |
| TR-08 | ươi | `uowif` | `ười` | `Triphthong_UOI_Horn_Grave` | mười |
| TR-09 | oai | `oais` | `oái` | `Triphthong_OAI_Acute` | |
| TR-10 | oai | `oaif` | `oài` | `Triphthong_OAI_Grave` | hoài |
| TR-11 | oay | `oays` | `oáy` | `Triphthong_OAY_Acute` | xoáy |
| TR-12 | oay | `oayf` | `oày` | `Triphthong_OAY_Grave` | |
| TR-13 | uya | `uyar` | `uyả` | `Triphthong_UYA_Hook` | |
| TR-14 | uyê | `uyeetj` | `uyệt` | `Triphthong_UYE_Dot` | tuyệt |
| TR-15 | uyê | `uyeens` | `uyến` | `Triphthong_UYE_Acute` | |
| TR-16 | oăn | `oawnj` | `oặn` | `Triphthong_OAN_Dot` | hoặn?? |

---

## Category 10: W-Modifier Priority (7 Levels)

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

## Category 11: Tone Placement Priority

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

## Category 12: Tone Relocation

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

## Category 13: Escape Mechanism

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

## Category 14: Edge Cases

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

## Category 15: Real Vietnamese Words

**Purpose:** Validate against real dictionary words.

### 15.1 Common Words

| ID | Word | Telex Input | Expected | Test Name |
|----|------|-------------|----------|-----------|
| RW-01 | việt | `vieetj` | `việt` | `RealWord_Viet` |
| RW-02 | nam | `nam` | `nam` | `RealWord_Nam` |
| RW-03 | tiếng | `tieengs` | `tiếng` | `RealWord_Tieng` |
| RW-04 | nước | `nuowcs` | `nước` | `RealWord_Nuoc` |
| RW-05 | người | `nguowif` | `người` | `RealWord_Nguoi` |
| RW-06 | được | `dduowcj` | `được` | `RealWord_Duoc` |
| RW-07 | đường | `dduowngf` | `đường` | `RealWord_Duong` |
| RW-08 | chào | `chaof` | `chào` | `RealWord_Chao` |
| RW-09 | cảm | `carm` | `cảm` | `RealWord_Cam` |
| RW-10 | ơn | `own` | `ơn` | `RealWord_On` |

### 15.2 Words with Triphthongs

| ID | Word | Telex Input | Expected | Test Name |
|----|------|-------------|----------|-----------|
| RW-11 | yêu | `yeeu` | `yêu` | `RealWord_Yeu` |
| RW-12 | yếu | `yeeus` | `yếu` | `RealWord_Yeu_Tone` |
| RW-13 | tiểu | `tieeur` | `tiểu` | `RealWord_Tieu` |
| RW-14 | tuyệt | `tuyeetj` | `tuyệt` | `RealWord_Tuyet` |
| RW-15 | nguyên | `nguyeen` | `nguyên` | `RealWord_Nguyen` |
| RW-16 | khuya | `khuya` | `khuya` | `RealWord_Khuya` |
| RW-17 | hoài | `hoaif` | `hoài` | `RealWord_Hoai` |
| RW-18 | xoáy | `xoays` | `xoáy` | `RealWord_Xoay` |
| RW-19 | tuổi | `tuooir` | `tuổi` | `RealWord_Tuoi` |
| RW-20 | muối | `muoois` | `muối` | `RealWord_Muoi` |

### 15.3 Words with oa/ă Pattern

| ID | Word | Telex Input | Expected | Test Name |
|----|------|-------------|----------|-----------|
| RW-21 | hoặc | `hoacwj` | `hoặc` | `RealWord_Hoac` |
| RW-22 | toằn | `toawnf` | `toằn` | `RealWord_Toan` | oă+grave |
| RW-23 | hoằn | `hoawnf` | `hoằn` | `RealWord_Hoan` | oă+grave |
| RW-24 | năm | `nawm` | `năm` | `RealWord_Nam5` |
| RW-25 | ăn | `awn` | `ăn` | `RealWord_An` |
| RW-26 | lặng | `lawngj` | `lặng` | `RealWord_Lang` |
| RW-27 | bắt | `bawts` | `bắt` | `RealWord_Bat` |
| RW-28 | đặc | `ddawcj` | `đặc` | `RealWord_Dac` |
| RW-29 | thắng | `thawngs` | `thắng` | `RealWord_Thang` |
| RW-30 | xắn | `xawns` | `xắn` | `RealWord_Xan` |

### 15.4 Words with ư/ơ Pattern

| ID | Word | Telex Input | Expected | Test Name |
|----|------|-------------|----------|-----------|
| RW-31 | của | `cuar` | `của` | `RealWord_Cua` |
| RW-32 | mưa | `muaw` | `mưa` | `RealWord_Mua` |
| RW-33 | thưa | `thuaw` | `thưa` | `RealWord_Thua` |
| RW-34 | giữa | `giuwax` | `giữa` | `RealWord_Giua` |
| RW-35 | lửa | `luwar` | `lửa` | `RealWord_Lua` |
| RW-36 | sử | `suwr` | `sử` | `RealWord_Su` |
| RW-37 | phở | `phowr` | `phở` | `RealWord_Pho` |
| RW-38 | thơ | `thow` | `thơ` | `RealWord_Tho` |
| RW-39 | sơn | `sown` | `sơn` | `RealWord_Son` |
| RW-40 | hơi | `howi` | `hơi` | `RealWord_Hoi` |

---

## Implementation Checklist

- [ ] Create test file structure with above categories
- [ ] Implement BV-01 to BV-06 (Basic Vowels)
- [ ] Implement CF-01 to CF-12 (Circumflex)
- [ ] Implement BR-01 to BR-06 (Breve)
- [ ] Implement HN-01 to HN-15 (Horn)
- [ ] Implement ST-01 to ST-06 (Stroke)
- [ ] Implement TN-S01 to TN-J12 (Tones)
- [ ] Implement FD-01 to FD-24 (Falling Diphthongs)
- [ ] Implement RD-01 to RD-12 (Rising Diphthongs)
- [ ] Implement TR-01 to TR-16 (Triphthongs)
- [ ] Implement WP-01 to WP-14 (W-Priority)
- [ ] Implement TP-01 to TP-18 (Tone Placement)
- [ ] Implement TL-01 to TL-08 (Tone Relocation)
- [ ] Implement ES-01 to ES-14 (Escape)
- [ ] Implement EC-01 to EC-14 (Edge Cases)
- [ ] Implement RW-01 to RW-40 (Real Words)
- [ ] Run full test suite
- [ ] Document any failing tests (expected vs actual)
- [ ] Mark tests that expose bugs vs expected behavior

---

## Known Issues to Investigate

| ID | Input | Current | Expected | Status |
|----|-------|---------|----------|--------|
| ✅ | `toawnf` | verify | `toằn` | **CONFIRMED** - oă pattern + grave |
| ✅ | `hoawnf` | verify | `hoằn` | **CONFIRMED** - same pattern |
| ✅ | `uwaw` | verify | `uaw` | **CONFIRMED** - no special transform |
| ✅ | `quew` | verify | `quew` | **CONFIRMED** - no transform |

---

## Test Execution Strategy

1. **Run existing tests first** - establish baseline
2. **Add tests category by category** - incremental
3. **Document failures** - some may be engine bugs, not test bugs
4. **Prioritize critical paths** - Real Words > Edge Cases
5. **Run after each addition** - catch regressions early
