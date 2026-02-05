# Vietnamese Phonology Specification for Telex Engine

**Version:** 1.0  
**Date:** 2026-02-04  
**Status:** Draft  
**Applies to:** NexusKey TelexEngine

---

## 1. Overview

This document defines the Vietnamese phonological rules that the NexusKey TelexEngine must implement. It serves as the authoritative reference for:
- Character composition logic
- Modifier (diacritical mark) application
- Tone mark placement algorithms

## 2. Vietnamese Syllable Structure

### 2.1 Canonical Form

Every Vietnamese syllable follows this structure:

```
Syllable = [Onset] + Nucleus + [Coda] + Tone
           (C₁)      (V)       (C₂)     (T)
```

| Component | Vietnamese | Required | Count | Examples |
|-----------|------------|----------|-------|----------|
| **Onset (C₁)** | Phụ âm đầu | No | 27 | b, c, ch, d, đ, g, gh, gi, h, k, kh, l, m, n, ng, ngh, nh, p, ph, q, r, s, t, th, tr, v, x |
| **Nucleus (V)** | Nguyên âm | **Yes** | 12 base + 6 modified | a, ă, â, e, ê, i, o, ô, ơ, u, ư, y |
| **Coda (C₂)** | Phụ âm cuối | No | 8 | c, ch, m, n, ng, nh, p, t |
| **Tone (T)** | Thanh điệu | Always | 6 | ngang, sắc, huyền, hỏi, ngã, nặng |

### 2.2 Vowel Inventory

#### Base Vowels (Nguyên âm đơn)
| Vowel | IPA | Description |
|-------|-----|-------------|
| a | /aː/ | Open front unrounded |
| e | /ɛ/ | Open-mid front unrounded |
| i, y | /i/ | Close front unrounded |
| o | /ɔ/ | Open-mid back rounded |
| u | /u/ | Close back rounded |

#### Modified Vowels (Nguyên âm có dấu phụ)
| Vowel | IPA | Base | Modifier | Telex Input |
|-------|-----|------|----------|-------------|
| ă | /a/ | a | Breve (˘) | `aw` |
| â | /ɤ̆/ | a | Circumflex (^) | `aa` |
| ê | /e/ | e | Circumflex (^) | `ee` |
| ô | /o/ | o | Circumflex (^) | `oo` |
| ơ | /ɤː/ | o | Horn (̛) | `ow` |
| ư | /ɯ/ | u | Horn (̛) | `uw` |

---

## 3. Telex Input Method Specification

### 3.1 Modifier Keys

| Telex Key | Modifier Type | Applicable Base | Result |
|-----------|---------------|-----------------|--------|
| Double vowel (`aa`, `ee`, `oo`) | Circumflex | a, e, o | â, ê, ô |
| `w` after `a` | Breve | a | ă |
| `w` after `o` | Horn | o | ơ |
| `w` after `u` | Horn | u | ư |
| `dd` | Stroke | d | đ |

### 3.2 Tone Keys

| Telex Key | Tone Name | Vietnamese | Unicode Mark | Example |
|-----------|-----------|------------|--------------|---------|
| (none) | Level | Ngang | (none) | ma |
| `s` | Acute | Sắc | ́ (U+0301) | má |
| `f` | Grave | Huyền | ̀ (U+0300) | mà |
| `r` | Hook above | Hỏi | ̉ (U+0309) | mả |
| `x` | Tilde | Ngã | ̃ (U+0303) | mã |
| `j` | Dot below | Nặng | ̣ (U+0323) | mạ |

### 3.3 Escape Mechanism

When a modifier key is pressed twice in sequence:
1. **First press:** Applies the modifier
2. **Second press:** Clears (undoes) the modifier AND adds the key as a literal character

| Escape Sequence | Behavior | Result |
|-----------------|----------|--------|
| `aaa` | `aa` → â, third `a` escapes | `aa` |
| `aww` | `aw` → ă, second `w` escapes | `aw` |
| `oww` | `ow` → ơ, second `w` escapes | `ow` |
| `ddd` | `dd` → đ, third `d` escapes | `dd` |
| `ass` | `as` → á, second `s` escapes | `as` |

---

## 4. Modifier Application Priority ('w' Key)

The 'w' key can produce different results depending on context. This section defines the **strict priority order**.

### 4.1 Priority Table for 'w' Modifier

| Priority | Condition | Pattern | Action | Result |
|----------|-----------|---------|--------|--------|
| **1** | Has "ua" vowel pair | `u` followed by `a` | Apply Horn to `u` | ưa |
| **2** | Has "oa" vowel pair | `o` followed by `a` | Apply Breve to `a` | oă |
| **3** | Has plain `u` (no horn) | Standalone `u` | Apply Horn to `u` | ư |
| **4** | Has plain `o` (no horn), NOT in "oa" | Standalone `o` | Apply Horn to `o` | ơ |
| **5** | Has plain `a` (no modifier) | Standalone `a` | Apply Breve to `a` | ă |
| **6** | Has existing horn (ư or ơ) | - | Clear modifier, add 'w' | Escape |
| **7** | Has existing breve (ă) | - | Clear modifier, add 'w' | Escape |
| **8** | No applicable vowel | - | Add literal 'w' | w |

### 4.2 Special Case: Auto-ươ Transformation

When the pattern `uơ` (u without horn + ơ with horn) is followed by another character:
- Automatically apply horn to `u` → `ươ`

**Rationale:** The `ươ` cluster is extremely common in Vietnamese (được, người, đường, etc.)

| Input Sequence | Intermediate | Final |
|----------------|--------------|-------|
| `uow` | u + ơ | uơ (no auto) |
| `uown` | u + ơ + n | ươn (auto-applied) |
| `dduowngf` | đ + u + ơ + n + g + f | đường |

### 4.3 Circumflex Removal on Horn Application

When 'w' applies horn to `u` and there's an adjacent `â`:
- Remove the circumflex from `â` → `a`

**Example:** `giuaaw` → `giưa` (NOT `giưâ`)

---

## 5. Tone Placement Priority

This section defines where the tone mark is placed when a tone key is pressed.

### 5.1 Priority Table for Tone Placement

| Priority | Rule Name | Condition | Action | Examples |
|----------|-----------|-----------|--------|----------|
| **1** | Last Horn | Word contains ơ or ư | Place tone on **last** horn vowel | được → đ**ợ**c, người → ng**ờ**i |
| **2** | Modified Vowel | Word contains â, ê, ô, or ă | Place tone on the modified vowel | cấp, bếp, hộp, ắc |
| **3a** | Falling Diphthong | See §5.2 | Place tone on **first** vowel | cáo, tối, gửi |
| **3b** | Rising Diphthong | See §5.2 | Place tone on **second** vowel | hoà, qùy |
| **4** | Default | No special pattern | Place tone on **rightmost** vowel | mẹ, bà |

### 5.2 Diphthong Classification

#### Falling Diphthongs (Tone on FIRST vowel)
| Pattern | Examples | Reason |
|---------|----------|--------|
| ai | hai, mai, tái | First vowel is nucleus |
| ao | sao, cào, đáo | First vowel is nucleus |
| au | sau, đau, tàu | First vowel is nucleus |
| ay | may, say, tầy | First vowel is nucleus |
| âu | dâu, câu, mầu | â is modified (high priority) |
| ây | mấy, cây, đầy | â is modified (high priority) |
| eo | leo, kéo, tèo | First vowel is nucleus |
| êu | kêu, nếu, thều | ê is modified (high priority) |
| iu | dịu, chíu, líu | First vowel is nucleus |
| oi | lói, chọi, gọi | First vowel is nucleus |
| ôi | tối, đội, bội | ô is modified (high priority) |
| ơi | gởi, trời | ơ is horn (highest priority) |
| ui | tui, cúi, múi | First vowel is nucleus |
| ưi | gửi | ư is horn (highest priority) |

#### Rising Diphthongs (Tone on SECOND vowel)
| Pattern | Examples | Reason |
|---------|----------|--------|
| oa | hoa, loa, tòa | Second vowel is nucleus |
| oe | xòe, toe, khòe | Second vowel is nucleus |
| uy | quy, lùy, thúy | Second vowel is nucleus |
| uê | huế, tuế | ê is modified (high priority) |

#### Special: ua/uê Pattern (Tone on FIRST)
| Pattern | Examples | Reason |
|---------|----------|--------|
| ua | của, mùa, cua | u is the main vowel |
| ue (before modifier) | tuệ | u gets tone initially |

### 5.3 Horn Vowel Special Rule (ươ Cluster)

For the common `ươ` cluster:
- The tone is placed on `ơ` (the **last** horn vowel)
- This overrides the normal first-vowel rule

| Word | Telex | Decomposition | Tone On |
|------|-------|---------------|---------|
| được | dduowcj | đ + ư + ơ + c + j | **ợ** |
| người | nguowif | ng + ư + ơ + i + f | **ờ** |
| mười | muowif | m + ư + ơ + i + f | **ờ** |
| đường | dduowngf | đ + ư + ơ + ng + f | **ờ** |

---

## 6. Tone Relocation Rule

When a horn modifier is added **after** a tone has already been placed, the tone must be relocated.

### 6.1 Relocation Trigger

**Condition:** Horn modifier is applied AND there is a tone on a non-horn vowel

### 6.2 Relocation Action

Move the tone from the current vowel to the horn vowel.

### 6.3 Example

| Input Sequence | Step-by-Step | Final |
|----------------|--------------|-------|
| `cuar` | c + ủ + a | của (tone on u) |
| `cuarw` | c + ủ + a → c + ử + a | cửa (tone moved to ư) |

---

## 7. Character Composition Table

### 7.1 Vowel + Modifier → Modified Vowel

| Base | + Circumflex | + Breve | + Horn |
|------|-------------|---------|--------|
| a | â | ă | - |
| e | ê | - | - |
| o | ô | - | ơ |
| u | - | - | ư |

### 7.2 Modified Vowel + Tone → Toned Character

This table is exhaustive (60 entries):

| Vowel | +Sắc | +Huyền | +Hỏi | +Ngã | +Nặng |
|-------|------|--------|------|------|-------|
| a | á | à | ả | ã | ạ |
| ă | ắ | ằ | ẳ | ẵ | ặ |
| â | ấ | ầ | ẩ | ẫ | ậ |
| e | é | è | ẻ | ẽ | ẹ |
| ê | ế | ề | ể | ễ | ệ |
| i | í | ì | ỉ | ĩ | ị |
| o | ó | ò | ỏ | õ | ọ |
| ô | ố | ồ | ổ | ỗ | ộ |
| ơ | ớ | ờ | ở | ỡ | ợ |
| u | ú | ù | ủ | ũ | ụ |
| ư | ứ | ừ | ử | ữ | ự |
| y | ý | ỳ | ỷ | ỹ | ỵ |

---

## 8. Edge Cases & Exceptions

### 8.1 Triphthongs

| Pattern | Examples | Tone Position | Notes |
|---------|----------|---------------|-------|
| iêu | tiếu, kiều | ê (modified) | Circumflex takes priority |
| yêu | yêu, yếu | ê (modified) | Circumflex takes priority |
| ươi | tưới, người | ơ (horn) | Last horn wins |
| uôi | tuổi, muối | ô (modified) | Circumflex takes priority |
| oai | hoài, ngoài | a (middle) | Middle vowel is nucleus |
| oay | xoáy, ngoáy | a (middle) | Middle vowel is nucleus |
| uya | khuya | y (middle) | Middle vowel is nucleus |
| uyê | tuyệt, thuyết | ê (modified) | Circumflex takes priority |

### 8.2 Semi-vowel Behaviors

| Character | Context | Treated As |
|-----------|---------|------------|
| i | After vowel (ai, oi, ui) | Coda (semi-vowel) |
| i | Word-initial or after consonant | Vowel |
| y | After u (uy) | Vowel |
| y | Elsewhere | Vowel (same as i) |
| u | Before vowel (ua, ue, uo) | Onset glide or vowel |
| o | Before a (oa) | Onset glide |

### 8.3 gi- Cluster

The combination "gi" has special handling:
- Before vowel: `gi` + V → treated as onset
- Alone: `gi` → g + i (two characters)

| Input | Decomposition | Note |
|-------|---------------|------|
| gia | gi + a | gi is onset |
| giữa | gi + ữ + a | gi is onset, ữ is nucleus |

---

## 9. Implementation Requirements

### 9.1 State Machine Requirements

The engine MUST maintain:
1. **Raw input buffer** - for escape reconstruction
2. **Character states** - base, modifier, tone, case for each position
3. **Pattern cache** - detected vowel patterns

### 9.2 Priority Resolution

The engine MUST apply rules in strict priority order as defined in:
- §4 for 'w' modifier
- §5 for tone placement

### 9.3 Testability Requirements

Each rule in this specification MUST have corresponding unit tests covering:
- Normal case
- Escape case
- Edge cases with surrounding characters

---

## Appendix A: Complete Telex Transformation Table

| Input | Output | Category |
|-------|--------|----------|
| aa | â | Circumflex |
| aw | ă | Breve |
| dd | đ | Stroke |
| ee | ê | Circumflex |
| oo | ô | Circumflex |
| ow | ơ | Horn |
| uw | ư | Horn |
| *s | *́ (acute) | Tone |
| *f | *̀ (grave) | Tone |
| *r | *̉ (hook) | Tone |
| *x | *̃ (tilde) | Tone |
| *j | *̣ (dot) | Tone |

---

## Appendix B: Unicode Reference

| Character | Unicode | UTF-16 | Description |
|-----------|---------|--------|-------------|
| ă | U+0103 | 0x0103 | Latin small letter a with breve |
| Ă | U+0102 | 0x0102 | Latin capital letter A with breve |
| â | U+00E2 | 0x00E2 | Latin small letter a with circumflex |
| Â | U+00C2 | 0x00C2 | Latin capital letter A with circumflex |
| đ | U+0111 | 0x0111 | Latin small letter d with stroke |
| Đ | U+0110 | 0x0110 | Latin capital letter D with stroke |
| ê | U+00EA | 0x00EA | Latin small letter e with circumflex |
| Ê | U+00CA | 0x00CA | Latin capital letter E with circumflex |
| ô | U+00F4 | 0x00F4 | Latin small letter o with circumflex |
| Ô | U+00D4 | 0x00D4 | Latin capital letter O with circumflex |
| ơ | U+01A1 | 0x01A1 | Latin small letter o with horn |
| Ơ | U+01A0 | 0x01A0 | Latin capital letter O with horn |
| ư | U+01B0 | 0x01B0 | Latin small letter u with horn |
| Ư | U+01AF | 0x01AF | Latin capital letter U with horn |

Toned vowels are in the Vietnamese Extended block: U+1EA0 - U+1EF9

---

## Document History

| Version | Date | Author | Changes |
|---------|------|--------|---------|
| 1.0 | 2026-02-04 | AI + Phat | Initial specification |
