# vkey SpellDecisionEngine + isLikelyEnglishAcronym — Analysis

**Date**: 2026-05-22
**Status**: Analysis-only, no code change. Waiting on anh's answers to 3 open questions before brainstorming.
**Trigger**: anh hỏi `https://github.com/tuanlongsav/vkey` có gì để học hỏi → em chỉ ra 10+ ideas → anh chọn "lift SpellDecisionEngine + add isLikelyEnglishAcronym" để phân tích kỹ.
**Sibling**: `2026-05-22-cay-ime-exploration.md` (cùng đợt khảo sát external IME).

---

## 1. vkey at a Glance

- **vkey** — macOS Vietnamese IME, Swift, fork từ Caffee, đã hấp thụ XKey + GoNhanh + Wiktionary lexicon.
- Bundle 8,960 VN syllables + 9,826 EN words embedded (~230KB).
- Architecture: Platform (CGEventTap + EventSimulator + AX) / Application (InputProcessor) / Engine (Telex + VNI + TiengVietState/Parser/Validator/Transformer).
- License GPL-3.0, dictionary CC BY-SA 4.0.

File trọng tâm em đọc:
- `vkey/Input/SpellDecisionEngine.swift` (250 LOC)
- `vkey/Engine/TiengVietValidator.swift` (300 LOC) — 6-step spell check + Vowel Inclusion Pairs
- `vkey/Input/PredictionEngine.swift` — 3-layer trigram/bigram scoring
- `vkey/Platform/Focused.swift` — AX timeout 0.1s
- `app-arch.md` (25KB) — layered diagram

## 2. vkey's `SpellDecisionEngine` Contract

**Operates at commit time** (post-typing). Inputs:

```swift
func evaluate(rawInput: String, transformed: String, needsRecovery: Bool) -> SpellDecision
```

Outputs:

```swift
enum SpellDecision {
    case keepVietnamese
    case keepRaw
    case restoreRawEnglish(String)
    case suggest([String])
}
```

Decision tree (simplified):

```
1. spellCheckEnabled? → if no, return keepVietnamese
2. empty? → keepVietnamese
3. isLikelyEnglishAcronym(rawInput)? → restoreRawEnglish    ← target heuristic
4. rawToken chứa ss/ff/rr/xx/jj doubled tone? → keepRaw
5. lexicon legacy restore? → restoreRawEnglish
6. lexicon shouldKeepVietnamese()? → keepVietnamese
7. isVietnameseWord(transformed)? + isEnglishWord(raw)?
   - If transformed có VN diacritic AND !isVN AND !isEN → keepVN (new word)
   - If !isVN AND raw is ASCII AND raw != transformed → restoreRawEN
   - If !isVN AND needsRecovery AND rawIsEN → restoreRawEN
   - If isVN AND rawIsEN:
     - policy == englishFirst → restoreRawEN
     - policy == balanced → if hasVNDiacritic OR in commonVN set → keepVN, else restoreRawEN
     - policy == vietnameseFirst → keepVN
8. needsRecovery? → suggest() or keepRaw
9. fallback → keepVietnamese
```

## 3. NexusKey's Equivalent Architecture (đã có gì)

### Per-keystroke gates (`EnglishProtection.h`, 458 LOC, header-only)

Tier 1 hard reject:
- `IsHardEnglishStart(c0,c1)` — cl, cr, bl, br, dr, fr, fl, gr, gl, pr, pl, sm, sn, sp, sw, st, sc, sk, sl, wh, wr
- `IsHardEnglishRawStart(raw,len)` — raw-input variant (catches wh/wr after Telex P8 rewrite)
- `IsHardEnglishEnd(c)` — x, r, z, f, s, j (never end Vietnamese)
- `IsInvalidVietnameseCoda(states,count)` — coda phải là c/m/n/p/t/ch/ng/nh
- `IsQWithoutU(c0,c1)`
- `HasStructuralVCVPattern` — V+C+V scan (catches behavior, manager, danger)

Tier 2 soft bias:
- `CheckSoftEnglishBias` — y+a/e/o không nối valid sequence

Tier 3 insistence:
- `UpdateToneInsistence` — same tone key 2× → user khẳng định, allow Telex tone

Raw prefix tables:
- `kBlockedTonePrefixes = {pas, gues}`
- `kBlockedModifierPrefixes = {pow, upw}`

### Commit-time (TypingEngine.cpp:1562-1623)

```cpp
std::wstring TypingEngine::Commit() {
    // 1. Quick-start consonant alone (f/j/w) → restore if ShouldAutoRestore
    // 2. Quick consonant alone (gg, uu) → restore
    // 3. Guard: skipAutoRestore = !spellCheck || !autoRestore || qc.hasActive()
    //    Otherwise:
    //      - shouldRestore = spellCheckDisabled_ || ValidPrefix
    //      - If shouldRestore: check excl list + HasIntentionalStrokeD
    //      - If !keepComposed: ShouldAutoRestore(raw,composed) → return raw
    // 4. Default: return composed
}
```

### Tổng kết NexusKey hiện trạng

- Đã có **80% scope** của vkey's `SpellDecisionEngine` nhưng rải qua per-keystroke gates + binary commit decision.
- **Không có lexicon** — pure phonotactic rules. (vkey ship 230KB embedded dictionary.)
- **Không có policy enum** — luôn cố keep Vietnamese, restore khi structural invalid.
- **Không có acronym heuristic** — `ARM/USA/API` bị tone keys giữa từ áp dụng nếu lọt qua VCV check (`IsHardEnglishEnd` chỉ check last char, không check uppercase signal).

## 4. Khác biệt kiến trúc (critical)

| Aspect | vkey | NexusKey |
|---|---|---|
| Decision time | Commit (post-typing) | Per-keystroke + commit |
| Input | `(raw, transformed, needsRecovery)` | `CharState[]` + raw |
| Lexicon | 230KB embedded | None |
| Output enum | 4-way `{keepVN, keepRaw, restoreRawEN, suggest}` | Binary `{composed, raw}` |
| Acronym detection | ✅ commit-time heuristic | ❌ |
| User policy | `english/balanced/vn-first` | Implicit balanced |
| Suggestion path | ✅ via SuggestionService | ❌ |

**Hệ quả implementation**: vkey để user gõ "text" → buffer thành "tẽt" → commit hỏi lexicon "tẽt là VN? text là EN? → restore". NexusKey ngược: chặn tone `s` từ keystroke đầu vì `te+t` hit `IsHardEnglishEnd`, buffer KHÔNG BAO GIỜ thành "tẽt". Hai triết lý preventive vs corrective.

## 5. Tách 2 Đề Xuất

### Đề Xuất A: `IsLikelyEnglishAcronym` — RECOMMEND SHIP

**Source**: `vkey/Input/SpellDecisionEngine.swift:50-77` (`isLikelyEnglishAcronym`).

**Heuristic**:
- Length 2-5
- Tất cả ASCII uppercase letters
- Không chứa telex doubled markers: `dd, aa, oo, ee, uu, ww, uw, ow, aw, rr, ss, ff, xx, jj`
- Last char không phải telex tone key: `s, f, r, x, j`

→ Return raw. Catches `ARM, USA, API, OK, BBC`.

**NexusKey port** — thêm vào `src/core/engine/EnglishProtection.h`:

```cpp
[[nodiscard]] inline bool IsLikelyEnglishAcronym(
        const wchar_t* raw, size_t len) noexcept {
    if (len < 2 || len > 5) return false;
    for (size_t i = 0; i < len; ++i) {
        if (raw[i] > 127) return false;
        if (!iswupper(raw[i]) || !iswalpha(raw[i])) return false;
    }
    // Check doubled markers (lowercase pairs in original raw)
    for (size_t i = 0; i + 1 < len; ++i) {
        wchar_t a = towlower(raw[i]);
        wchar_t b = towlower(raw[i+1]);
        if (a == b && (a == L'd' || a == L'a' || a == L'o' || a == L'e' ||
                       a == L'u' || a == L'w' || a == L'r' || a == L's' ||
                       a == L'f' || a == L'x' || a == L'j')) return false;
        // Cross-pair markers: uw, ow, aw
        if ((a == L'u' && b == L'w') || (a == L'o' && b == L'w') ||
            (a == L'a' && b == L'w')) return false;
    }
    // Last char telex tone key
    wchar_t last = towlower(raw[len-1]);
    if (last == L's' || last == L'f' || last == L'r' ||
        last == L'x' || last == L'j') return false;
    return true;
}
```

**Call sites**:
1. **Pre-tone gate** (`TypingEngine.cpp:290`, beside `IsBlockedEnglishTone`): nếu match → `engProt_.bias = HardEnglish` + literal passthrough.
2. **Commit fallback** (`TypingEngine.cpp:1592-1618`): nếu rawInput match acronym AND composed có VN diacritic → return raw.

VNI tone keys là digits (1-5) → heuristic không apply (đã filter qua telex-tone-key check).

**Test cases** (target `EnglishProtectionTests.cpp`):
- ✅ `ARM` (a-r-m, `r` ở giữa → telex hỏi → `Ảm`) → restore
- ✅ `USA, API, OK, BBC, HTTP` → restore
- ❌ `Ma` (mixed case) → no-op
- ❌ `MASS` (kết bằng `s` tone) → KHÔNG flag, để doubled-tone preservation chạy
- ❌ `pass` (lowercase) → no-op, dùng `kBlockedTonePrefixes`
- ❌ `ADD` (`dd` doubled marker) → KHÔNG flag, để telex `dd→đ` chạy
- ❌ `LOW` (`ow` marker) → KHÔNG flag
- ⚠️ `OK` (2 chars, no tone key, all upper) → flag → restore. Behavior change: nếu user gõ "OK" với telex thì hiện tại đã ra "OK" (không tone key giữa). Cần verify không phá case khác.

**Effort**: 0.5-1 ngày. 1 file edit + 10 GTest cases. Khớp PHILOSOPHY (nhanh, gọn, nhẹ).

**Risk**: thấp. O(len), no lexicon, không thay đổi flow chính. Sandbox được qua spell-check flag.

### Đề Xuất B: Lift `SpellDecisionEngine` — KHÔNG RECOMMEND lift nguyên

**Lý do**: 70-80% logic vkey là lexicon-driven (`isVietnameseWord`, `isEnglishWord`, `EnVnReference.lookupEnglish`, `extremelyCommonVietnameseWords` set, legacy restore). NexusKey không có lexicon → phần lớn không apply.

**Nếu vẫn muốn lift** (cosmetic refactor):

| Sub-item | Effort | ROI |
|---|---|---|
| B.1 Move `Commit()` lines 1562-1623 ra `SpellDecisionEngine.h/cpp` | 1-2 ngày | Low (Commit() chỉ 60 LOC, chưa "god") |
| B.2 Thêm `RestorePolicy {EnglishFirst, Balanced, VnFirst}` enum + UI | +0.5 ngày + Sciter UI | Medium (cần user feedback) |
| B.3 Embedded lexicon | **2 tuần+** | High nếu chấp nhận đổi triết lý |
| B.4 Suggestion path | Milestone | New feature, scope khác |

**Tại sao defer**:
1. Code hiện ở `Commit()` chỉ 60 dòng — chưa cần tách.
2. Probe `2026-05-22-hookengine-degod-probe.md` đang chạy (Phase 1 ConfigSnapshotBuilder lift pending Windows verify). Không nên pile thêm.
3. PHILOSOPHY: "nhẹ" — thêm 230KB embedded lexicon đụng pillar này. Cần quyết định riêng.
4. Phonotactic approach hiện tại là chủ ý thiết kế, không phải omission.

## 6. Recommendation

```
SHIP:  Đề Xuất A (IsLikelyEnglishAcronym)
       - Sau khi anh trả lời 3 open questions bên dưới
       - 1 phase nhỏ, 1 file, GTest, sandbox-able qua spell-check flag

DEFER: Đề Xuất B (full lift)
       - B.1 cosmetic extraction → đợi Commit() > 120 LOC
       - B.2 RestorePolicy enum → đợi user yêu cầu UX
       - B.3 lexicon → milestone riêng nếu đổi triết lý
       - B.4 suggestion → milestone riêng
```

## 7. Open Questions Cho Anh

1. **Repro**: anh đã thực sự gặp bug `ARM → Ảm` (hoặc tương tự) chưa, hay đây chỉ là idea học từ vkey readme?
2. **Scope**: nếu có repro, app nào? Chrome / Notepad / Word — vì các host xử lý case khác nhau (Chrome skip OnTestKeyDown).
3. **Call site**: acronym detection nên fire ở
   - (a) pre-tone gate (chặn từ đầu, nhanh hơn),
   - (b) commit time (restore khi commit, safety net),
   - (c) **cả 2** — em nghiêng về (c) vì Chrome OnKeyDown bypass có thể skip path (a).

Anh trả lời (1)(2)(3) → em vào `brainstorming` skill để chốt design rồi viết execution plan + tests.

## 8. Related References

- vkey repo: `https://github.com/tuanlongsav/vkey`
- vkey `SpellDecisionEngine.swift`: source for both A and B
- NexusKey `EnglishProtection.h`: target file for A.1
- NexusKey `TypingEngine.cpp:290, 1592-1618`: call sites for A
- `docs/plans/2026-05-22-hookengine-degod-probe.md`: ongoing refactor — do not collide
- `docs/CODE_GOVERNANCE.md`: must answer Q1-Q5 before implementation
- `docs/PHILOSOPHY.md`: "nhẹ" pillar — informs B.3 reject decision

## 9. Other vkey Ideas Captured (Defer Tier)

For traceability, ý em đã chỉ ra trong khảo sát ban đầu nhưng không nằm trong scope này:

| Idea | Tier | Note |
|---|---|---|
| 3-state Smart Switch (VI/EN/Disabled, 👤 vs 🤖) | Tier 1 | Extension của `project_appoverrides_2026-03-30` — milestone sau |
| Stats auto-learn (≥1 day, ≥5/day, ratio ≥75%) | Tier 1 | Đi kèm 3-state |
| Auto Typo Correction (`thfi→thì`, `veeitj→việt`) | Tier 1 | Port logic `TiengVietValidator.swift:174-184` |
| `AXUIElementSetMessagingTimeout(0.1)` | Tier 1 | Win analogue: `SendMessageTimeout(SMTO_ABORTIFHUNG, 100ms)` — defensive add nếu chưa có |
| PredictionEngine 3-layer scoring | Tier 1 | Cần lexicon + NGramStore — milestone riêng |
| `TiengVietState` immutable + pure parser | Tier 2 | Validates direction của `degod-probe` |
| `NGramStore` async learning | Tier 2 | Đi kèm Prediction |
| AX Probing push-based | Tier 3 | NexusKey đã có `SetWinEventHook` + `WriteAnchor` |
| HUD glassmorphic | Tier 3 | Tray icon đã đủ |
