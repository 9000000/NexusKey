# Học tập từ Unikey Engine — Design & Action Plan

> Phân tích Unikey 1.0.4 engine so với NexusKey, rút ra bài học và kế hoạch cải tiến.

## Implementation Progress

| Task | Status | Details |
|------|--------|---------|
| **P1.1** VCPairList | DONE | 26 rules (12 single + 11 double + 1 triple vowel), bitmask validation |
| **P1.2** gi/qu exceptions | VERIFIED | quỳnh, giếng, quyết, quyến all pass — no code changes needed |
| **P1.3** k-vowel initial | DONE | k restricted to e/ê/i/y; kh unaffected |
| **P2.1** Tone drift guard | VERIFIED | 6 tone types tested — all stable, no drift |
| **P2.2** gi+tone relocate | VERIFIED | 6 tests pass — gì+a→già correctly relocates tone |
| **Bug fix** k-coda in cross-vowel circumflex | DONE | Added 'k' to TelexEngine.cpp:500 coda check |
| **Bug fix** k-coda in English protection | DONE | Added 'k' to EnglishProtection.h:204 exception |
| **Engine integration tests** | DONE | 10 tests verifying VCPair/k-initial affect tone gating |
| **Total** | **1063 tests pass** | 100 new tests, 0 regressions |

## Tổng quan kiến trúc

| | Unikey | NexusKey |
|---|---|---|
| Buffer | `WordInfo[]` — Unicode precomposed + metadata | `CharState[]` — base + modifier + tone tách biệt |
| Undo | Dual buffer (char + keystroke history) | State-based — pop CharState |
| Tone placement | `getTonePosition()` — static `terminated` param | `FindToneTarget()` — 4-level priority, dynamic coda scan |
| English detection | Passive — detect sau khi gõ xong word, rồi restore | 3-tier active — block trước khi apply tone |
| Spell check | Boolean flag + VCPairList (~100 entries) | Syllable-level: 48-entry vowel nucleus table |
| Input methods | Function pointer dispatch table | Template-based shared helpers |
| Lookup | Binary search trên sorted arrays | O(1) flat constexpr arrays |

**Kết luận:** NexusKey kiến trúc hiện đại hơn đáng kể. Nhưng Unikey có một số chi tiết validation rất chặt chẽ mà NexusKey có thể học hỏi.

---

## Những gì NexusKey đã vượt Unikey

1. **State-based architecture** — Backspace O(1), không cần reverse map
2. **3-tier English Protection** — Unikey hoàn toàn không có
3. **O(1) constexpr composition tables** — không cần binary search runtime
4. **Modern/Classic diphthong toggle** (hóa vs hoá)
5. **W-modifier 8-level explicit priority stack**
6. **Quick consonant/start/end** — cc→ch, f→ph, gg→gi...
7. **Smart Switch** — nhớ V/E mode per-app
8. **Backspace into committed word** — undo last commit với timeout
9. **Temp spell check bypass** (Ctrl tap), **temp English mode** (double-Alt)
10. **Auto-capitalize** sau dấu câu

---

## Những gì Unikey làm tốt hơn — Bài học cần kế thừa

### PHASE 1: Critical — Spell Check Gaps (Ảnh hưởng trực tiếp đến UX gõ tiếng Việt)

#### 1.1 Bổ sung VCPairList — Whitelist nguyên âm + phụ âm cuối

**Vấn đề:** NexusKey đánh dấu `canEnd=true` cho single vowels → cho phép MỌI phụ âm cuối với mọi nguyên âm. Thực tế tiếng Việt có ràng buộc chặt hơn.

**Unikey có 51 cặp hợp lệ rõ ràng.** Ví dụ:
- `ơ` chỉ đi với m, n, p, t (KHÔNG có ch, ng, nh)
- `y` chỉ đi với t (cực kỳ hạn chế)
- `â` không đi với ch, nh

**Action:**
- Thêm `kValidVCPairs[]` constexpr table vào SpellChecker
- Mỗi vowel nucleus + final consonant combination phải nằm trong whitelist
- Fallback: nếu không tìm thấy trong bảng → `Invalid`

**Impact:** Loại bỏ các âm tiết vô nghĩa mà NexusKey đang chấp nhận (ơch, yng, ânh...)

#### 1.2 Exception rules cho gi/qu đặc biệt

**Vấn đề:** Unikey có explicit exceptions cho:
- `quyn`, `quynh` — qu + y + n/nh (hợp lệ dù VC validation thường fail)
- `gieng`, `giêng` — gi + e/ê + n/ng (hợp lệ dù gi+e thường fail)

**Action:**
- Verify NexusKey xử lý đúng các từ: quýnh, giếng, giềng, quỳnh
- Nếu fail → thêm explicit exceptions vào `SpellChecker::ValidateImpl()`

#### 1.3 k-consonant vowel whitelist

**Vấn đề:** NexusKey chỉ cho phép `k` + `ắ` (Đắk Lắk). Unikey cho phép k với e, i, y, ê, eo, êu, ia, iê, iêu...

**Phân tích:** Trong tiếng Việt hiện đại, `k` thay thế `c` trước e/ê/i/y. Các từ: kẻ, kì, ký, kể, kêu, kia, kiên, kiểu... đều hợp lệ. Nhưng `k` ở đây là initial consonant, không phải final.

**Action:**
- Xem lại logic `k` trong SpellChecker — phân biệt initial vs final `k`
- Initial `k`: cho phép trước e, ê, i, y và các sequences bắt đầu bằng chúng
- Final `k`: giữ nguyên chỉ cho ắ (Đắk Lắk)

---

### PHASE 2: High — Tone Placement Refinement

#### 2.1 Tone drift guard trên repeated vowels (P4 blocking)

**Trạng thái:** NexusKey ĐÃ CÓ `IsToneRelocBlockedByP4()` — guard chống tone trượt khi gõ lặp nguyên âm (Kìaaaa → tone ở trên ì, không trượt).

**Action:** Verify coverage — test các edge cases:
- `hòaaaa` — tone phải ở trên `o`
- `thuở + o + o` — tone phải ở trên `ở`

#### 2.2 markChange incremental update (từ Unikey)

**Unikey dùng `markChange()` để chỉ recompose từ vị trí thay đổi trở đi.** NexusKey hiện recompose toàn bộ string mỗi lần (`ComposeAll()`).

**Phân tích:**
- Với buffer nhỏ (< 20 chars per word), `ComposeAll()` đã O(n) với n rất nhỏ
- Overhead thực tế không đáng kể
- **Kết luận: SKIP** — premature optimization, complexity không đáng

#### 2.3 Tone trên gi cluster khi append vowel

**Unikey xử lý đặc biệt:** `gì` + 'a' → tone di chuyển từ 'i' sang 'a' (vì 'i' trở thành phần của consonant cluster 'gi').

**Action:** Verify NexusKey xử lý đúng:
- Gõ: g → ì → a → kết quả phải là "già" (tone trên a)
- Gõ: g → í → a → kết quả phải là "giá" (tone trên a)

---

### PHASE 3: Medium — Feature Parity

#### 3.1 User-defined key mapping

**Unikey có `usrkeymap.h/cpp`:** cho phép user remap tone keys, modifier keys qua file config.

**NexusKey thiếu:** Keys hardcoded per input method. Power users không thể customize.

**Design:**
```toml
# nexuskey.toml
[custom_keymap]
tone_acute = "s"      # default Telex
tone_grave = "f"
tone_hook = "r"
tone_tilde = "x"
tone_dot = "j"
tone_remove = "z"
modifier_circumflex = "aa"  # double-key
modifier_breve = "w"
modifier_horn = "w"
modifier_stroke_d = "dd"
```

**Scope:** Chỉ cho phép remap TONE và MODIFIER keys. Không cho remap vowels/consonants (quá phức tạp, ít giá trị).

**Action:**
- Thêm `CustomKeyMap` struct vào TypingConfig
- Parse từ TOML config
- TelexEngine/VniEngine check custom map trước khi dùng default

#### 3.2 VIQR input method

**Unikey có VIQR:** dùng dấu câu cho tone (` ' ? ~ .).

**Phân tích:**
- User base cực kỳ nhỏ năm 2026
- Complexity thêm vào engine không đáng
- **Kết luận: SKIP**

---

### PHASE 4: Low — Nice-to-have

#### 4.1 SimpleTelex refinement

**Unikey SimpleTelex:** stripped down cực mạnh — chỉ giữ tone keys + D-mark, bỏ hoàn toàn roof/hook/breve modifiers.

**NexusKey SimpleTelex:** Telex với `allowZwjf=false` — vẫn giữ đầy đủ modifiers.

**Phân tích:** NexusKey approach hợp lý hơn cho users muốn "Telex nhẹ" nhưng vẫn đầy đủ. Unikey quá cực đoan. **SKIP.**

#### 4.2 Vowel sequence enum system (71 entries)

**Unikey dùng `VowelSeq` enum với 71 patterns**, mỗi pattern có metadata đầy đủ (roofPos, hookPos, withRoof, withHook, sub-sequences).

**NexusKey dùng 42-entry vowel nucleus table** — đủ cho spell check nhưng ít granular hơn.

**Phân tích:**
- NexusKey không cần enum vì dùng state-based architecture — không track sequence, track từng char state
- 71 entries của Unikey bao gồm intermediate typing states mà NexusKey handle qua CharState
- **Kết luận: SKIP** — kiến trúc khác nhau, không cần port

---

## Phase Priority Matrix

| Phase | Items | Impact | Effort | Priority |
|-------|-------|--------|--------|----------|
| **1** | VCPairList, gi/qu exceptions, k-vowel whitelist | Spell check chính xác hơn đáng kể | Medium | **Critical** |
| **2** | Tone drift verify, gi+tone verify | Sửa edge case tone placement | Low | **High** |
| **3** | Custom key mapping | Power user feature | High | **Medium** |
| **4** | SimpleTelex, VowelSeq enum | Marginal improvement | High | **Low/Skip** |

---

## Chi tiết kỹ thuật Phase 1

### VCPairList Design

```cpp
// SpellChecker.cpp — new constexpr table
// Vowel nucleus → allowed final consonants bitmask
// Finals: c=0x01, ch=0x02, k=0x04, m=0x08, n=0x10, ng=0x20, nh=0x40, p=0x80, t=0x100

struct VCRule {
    const wchar_t* vowel;   // normalized vowel nucleus
    uint16_t allowedFinals; // bitmask of allowed finals
};

constexpr uint16_t F_c  = 0x001;
constexpr uint16_t F_ch = 0x002;
constexpr uint16_t F_k  = 0x004;
constexpr uint16_t F_m  = 0x008;
constexpr uint16_t F_n  = 0x010;
constexpr uint16_t F_ng = 0x020;
constexpr uint16_t F_nh = 0x040;
constexpr uint16_t F_p  = 0x080;
constexpr uint16_t F_t  = 0x100;
constexpr uint16_t F_ALL = 0x1FF;
constexpr uint16_t F_STOP = F_c | F_ch | F_k | F_p | F_t;
constexpr uint16_t F_NASAL = F_m | F_n | F_ng | F_nh;

// Derived from Unikey VCPairList + Vietnamese phonology
constexpr VCRule kVCRules[] = {
    // Single vowels
    { L"a",  F_ALL },                          // ac, ach, am, an, ang, anh, ap, at
    { L"â",  F_c | F_m | F_n | F_ng | F_p | F_t },  // âc, âm, ân, âng, âp, ât (NO ch, nh)
    { L"ă",  F_c | F_k | F_m | F_n | F_ng | F_p | F_t }, // ắc, ắk (Đắk), ăm, ăn, ăng, ăp, ăt (NO ch, nh)
    { L"e",  F_ALL },                          // ec, em, en, eng, ep, et
    { L"ê",  F_c | F_ch | F_m | F_n | F_nh | F_p | F_t }, // NO ng
    { L"i",  F_c | F_ch | F_m | F_n | F_ng | F_nh | F_p | F_t }, // ALL
    { L"o",  F_c | F_m | F_n | F_ng | F_p | F_t },  // NO ch, nh
    { L"ô",  F_c | F_m | F_n | F_ng | F_p | F_t },  // NO ch, nh
    { L"ơ",  F_m | F_n | F_p | F_t },                // ONLY m, n, p, t
    { L"u",  F_c | F_m | F_n | F_ng | F_p | F_t },  // NO ch, nh
    { L"ư",  F_c | F_m | F_n | F_ng | F_p | F_t },  // NO ch, nh
    { L"y",  F_t },                                    // ONLY t (e.g., ít → ý+t? rare)

    // Double vowels with coda
    { L"iê", F_c | F_m | F_n | F_ng | F_p | F_t },  // tiếc, tiếm, tiên, tiếng, tiếp, tiết
    { L"oă", F_c | F_n | F_ng | F_t },               // hoắc, hoăn, hoắng, hoắt
    { L"oe", F_m | F_n | F_ng | F_t },               // NO c, ch, p — rare words
    { L"oo", F_c | F_ng },                            // boong, xoong — very limited
    { L"uâ", F_n | F_ng | F_t },                     // tuân, tuấng, tuất
    { L"uê", F_ch | F_n | F_nh },                    // huếch, thuên, thuênh
    { L"uô", F_c | F_m | F_n | F_ng | F_p | F_t },  // chuộc, chuôm, tuôn, muống, chuốp, thuốt
    { L"uơ", F_c | F_m | F_n | F_ng | F_p | F_t },  // same as uô (horn variant)
    { L"uy", F_ch | F_n | F_nh | F_t },              // huych, huynh, quynh, quyết
    { L"ươ", F_c | F_m | F_n | F_ng | F_p | F_t },  // lược, ươm, ương, ướp, ướt
    { L"yê", F_c | F_m | F_n | F_ng | F_p | F_t },  // same as iê (y variant)

    // Triphthongs with coda
    { L"uyê", F_n | F_t },                           // quyến, quyết
};
```

### Validation Logic

```cpp
Result SpellChecker::ValidateFinalConsonant(
    const wchar_t* vowelNucleus,
    const wchar_t* finalConsonant) 
{
    uint16_t finalBit = FinalConsonantToBit(finalConsonant);
    if (finalBit == 0) return Result::Invalid;  // unknown final
    
    for (const auto& rule : kVCRules) {
        if (wcscmp(rule.vowel, vowelNucleus) == 0) {
            return (rule.allowedFinals & finalBit) ? Result::Valid : Result::Invalid;
        }
    }
    return Result::Invalid;  // vowel not in table
}
```

---

## Tham chiếu Unikey Source

| File | Nội dung | Dòng quan trọng |
|------|----------|-----------------|
| `ukengine.cpp` | VSeqList, CSeqList, VCPairList, tone/roof/hook algorithms | 63-155, 207-290, 929-951 |
| `ukengine.h` | WordInfo, KeyBufEntry, UkEngine class | 127-166 |
| `inputproc.cpp` | Telex/VNI/VIQR/MsVi mappings, classifier table | 44-260 |
| `vnlexi.h` | VnLexiName enum, VowelSeq enum, ConSeq enum | Full file |
| `mactab.cpp` | Macro table binary search | 81-120 |
| `usrkeymap.cpp` | User key mapping parser | Full file |
