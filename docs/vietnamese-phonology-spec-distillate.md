---
type: bmad-distillate
sources:
  - "archive/vietnamese-phonology-spec.md"
downstream_consumer: "Vietnamese IME engine development, debugging typing bugs, writing tests"
created: "2026-04-01"
token_estimate: 2100
parts: 1
---

## Syllable Structure

- Canonical: `[Onset C₁] + Nucleus V (required) + [Coda C₂] + Tone T`
- Onsets (27): b, c, ch, d, đ, g, gh, gi, h, k, kh, l, m, n, ng, ngh, nh, p, ph, q, r, s, t, th, tr, v, x
- Nucleus (12 base + 6 modified): a, ă, â, e, ê, i, o, ô, ơ, u, ư, y
- Codas (8): c, ch, m, n, ng, nh, p, t
- Tones (6): ngang, sắc, huyền, hỏi, ngã, nặng

## Vowel Inventory

- Base vowels: a /aː/; e /ɛ/; i,y /i/; o /ɔ/; u /u/
- Modified vowels: ă /a/ (a+breve, `aw`); â /ɤ̆/ (a+circumflex, `aa`); ê /e/ (e+circumflex, `ee`); ô /o/ (o+circumflex, `oo`); ơ /ɤː/ (o+horn, `ow`); ư /ɯ/ (u+horn, `uw`)

## Telex Modifier Keys

| Telex Key | Modifier | Base | Result |
|-----------|----------|------|--------|
| `aa`, `ee`, `oo` | Circumflex | a, e, o | â, ê, ô |
| `aw` | Breve | a | ă |
| `ow` | Horn | o | ơ |
| `uw` | Horn | u | ư |
| `dd` | Stroke | d | đ |

## Telex Tone Keys

| Key | Tone | Vietnamese | Unicode | Example |
|-----|------|------------|---------|--------|
| (none) | Level | Ngang | — | ma |
| `s` | Acute | Sắc | U+0301 | má |
| `f` | Grave | Huyền | U+0300 | mà |
| `r` | Hook above | Hỏi | U+0309 | mả |
| `x` | Tilde | Ngã | U+0303 | mã |
| `j` | Dot below | Nặng | U+0323 | mạ |

## Escape Mechanism

- Double modifier press: 1st applies modifier, 2nd clears + appends literal
- `aaa`→`aa`; `aww`→`aw`; `oww`→`ow`; `ddd`→`dd`; `ass`→`as`

## 'w' Modifier Priority (strict order)

| P | Condition | Action | Result |
|---|-----------|--------|--------|
| 1 | Has "ua" pair | Horn→u | ưa |
| 2 | Has "uo" pair | Horn→o | uơ (→ươ via AutoUO) |
| 3 | Has "oa" pair | Breve→a | oă |
| 4 | Plain `u` (no horn) | Horn→u | ư |
| 5 | Plain `o` (not in "oa"), no horn | Horn→o | ơ |
| 6 | Plain `a` (no modifier), no horn exists | Breve→a | ă |
| 7 | Existing horn (ư or ơ) | Clear + literal 'w' | Escape |
| 8 | Existing breve (ă) | Clear + literal 'w' | Escape |
| 9 | No applicable vowel | Literal 'w' | w |

## Auto-ươ Transformation

- Pattern `uơ` + following char → auto-apply horn to `u` → `ươ` (rationale: ươ extremely common — được, người, đường)
- `uow`→uơ (no auto); `uown`→ươn; `dduowngf`→đường

## Circumflex Removal on Horn

- When 'w' applies horn to `u` and adjacent `â` exists: remove circumflex from `â`→`a`
- Example: `giuaaw`→`giưa` (NOT `giưâ`)

## Tone Placement Priority (strict order)

| P | Rule | Condition | Placement | Examples |
|---|------|-----------|-----------|----------|
| 1 | Last Horn | ơ or ư present | Last horn vowel | đ**ợ**c, ng**ờ**i |
| 2 | Modified Vowel | â, ê, ô, ă present | The modified vowel | cấp, bếp, hộp, ắc |
| 3a | Falling Diphthong | See table | First vowel | cáo, tối, gửi |
| 3b | Rising Diphthong | See table | Second vowel | hoà, qùy |
| 4 | Default | No special pattern | Rightmost vowel | mẹ, bà |

## Falling Diphthongs (tone on FIRST vowel)

- ai (hai, tái); ao (sao, đáo); au (đau, tàu); ay (may, tầy); âu (dâu, mầu); ây (mấy, đầy); eo (kéo, tèo); êu (nếu, thều); iu (dịu, líu); oi (lói, gọi); ôi (tối, đội); ơi (trời, gởi); ui (cúi, múi); ưi (gửi)

## Rising Diphthongs (tone on SECOND vowel)

- oa (tòa, hoa); oe (xòe, khòe); uy (lùy, thúy); uê (huế, tuế)

## Special: ua/ue (tone on FIRST)

- ua (của, mùa); ue before modifier (tuệ — u gets tone initially)

## ươ Cluster Tone Rule

- Tone placed on ơ (last horn vowel), overrides first-vowel rule
- được→đ**ợ**c; người→ng**ờ**i; mười→m**ườ**i; đường→đ**ườ**ng

## Tone Relocation Rule

- Trigger: horn modifier applied AFTER tone already placed on non-horn vowel
- Action: move tone from current vowel to horn vowel
- Example: `cuar`→của (tone on u); `cuarw`→cửa (tone moved to ư)

## Vowel + Modifier Composition

| Base | +Circumflex | +Breve | +Horn |
|------|-------------|--------|-------|
| a | â | ă | — |
| e | ê | — | — |
| o | ô | — | ơ |
| u | — | — | ư |

## Full Toned Character Table (60 entries)

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

## Stop-Final Tone Restriction

- Stop finals: c, ch, k, p, t → only Ngang, Sắc, Nặng allowed
- Non-stop finals: m, n, ng, nh → all 6 tones allowed
- Blocked tones on stop finals: Huyền (f), Hỏi (r), Ngã (x)
- Engine behavior (spellCheck ON): blocked tone key treated as literal character immediately
- Examples: `ocr`→`ocr` (literal); `ocs`→`ốc`; `atf`→`atf` (literal); `anr`→`ản` (non-stop, allowed)
- Spell check OFF: tone applied regardless (`ocr`→`ỏc`)

## Triphthongs

| Pattern | Tone position | Notes |
|---------|--------------|-------|
| iêu | ê | Circumflex priority |
| yêu | ê | Circumflex priority |
| ươi | ơ | Last horn wins |
| uôi | ô | Circumflex priority |
| oai | a (middle) | Middle = nucleus |
| oay | a (middle) | Middle = nucleus |
| uya | y (middle) | Middle = nucleus |
| uyê | ê | Circumflex priority |

## Semi-vowel Context Rules

| Char | Context | Role |
|------|---------|------|
| i | After vowel (ai, oi, ui) | Coda (semi-vowel) |
| i | Word-initial / after consonant | Vowel |
| y | After u (uy) | Vowel |
| y | Elsewhere | Vowel (=i) |
| u | Before vowel (ua, ue, uo) | Onset glide or vowel |
| o | Before a (oa) | Onset glide |

## gi- Cluster

- Before vowel: `gi` + V → gi treated as onset (gia, giữa)
- Alone: `gi` → g + i (two characters)

## Engine State Requirements

- Must maintain: raw input buffer; per-position character states (base, modifier, tone, case); pattern cache for vowel patterns
- Must apply rules in strict priority order (§4 for 'w', §5 for tone)

## Unicode Reference

| Char | Unicode | Char | Unicode |
|------|---------|------|--------|
| ă/Ă | U+0103/U+0102 | â/Â | U+00E2/U+00C2 |
| đ/Đ | U+0111/U+0110 | ê/Ê | U+00EA/U+00CA |
| ô/Ô | U+00F4/U+00D4 | ơ/Ơ | U+01A1/U+01A0 |
| ư/Ư | U+01B0/U+01AF | | |
- Toned vowels: Vietnamese Extended block U+1EA0–U+1EF9

## Document Metadata

- Version 1.2, 2026-03-30, applies to NexusKey TelexEngine
- Authors: AI + Phat; v1.1 added P2 "uo" pattern, fixed P6 escape; v1.2 added §8 stop-final restriction
