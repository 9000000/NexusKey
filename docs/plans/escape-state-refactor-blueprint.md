# EscapeState + QuickConsonantState Refactor Blueprint

> Target: v2.1.13 (after v2.1.12 feature release)
> Date: 2026-04-11
> Source: Party mode deep analysis session

---

## 1. Problem Summary

TelexEngine and VniEngine use 10 scattered boolean/sentinel flags to track escape and quick consonant state. The worst offender is `toneEscaped_` — a single bool set in 11 locations in TelexEngine (5 in VniEngine) across 7 semantic contexts (tone, circumflex, bracket, horn, breve, stroke, synthetic), meaning "Vietnamese intent canceled" but named as if only tone-related.

**Key findings from grep audit:**
- `toneEscaped_` in tests: 8 occurrences, ALL in comments, zero assertions on flag value
- `dModifierEscaped_` in tests: 0 occurrences
- All tests are black-box (assert Peek()/Commit() output) — best possible safety net for this type of refactor
- `dModifierEscaped_` and `toneEscaped_` are ALWAYS co-set, NEVER co-read in same block
- No independent set path exists for `dModifierEscaped_`

**Current flags to replace:**

```cpp
// Escape intent (Layer 2):
bool toneEscaped_ = false;              // 12 set-true sites, 7 semantic meanings
bool dModifierEscaped_ = false;          // dd→d lock (strict subset of toneEscaped_)

// Quick consonant (4 fields for 1 feature):
bool quickConsonantOnly_ = false;        // Buffer is only QC expansion
bool quickConsonantEscaped_ = false;     // Backspace undid QC
size_t quickConsonantIdx_ = SIZE_MAX;    // Index of QC result char
wchar_t lastQuickConsonantKey_ = 0;      // Re-trigger suppression

// Quick start (separate lifecycle):
wchar_t quickStartKey_ = 0;             // f/j/w original key

// Derived signal (DO NOT refactor):
bool spellCheckDisabled_ = false;        // Computed each keystroke
EnglishProtectionState engProt_;         // Already a proper struct
```

---

## 2. Proposed Types

### 2.1 EscapeState

**File location:** Add to `src/core/engine/EngineHelpers.h` (already shared by both engines).

Replaces `toneEscaped_` + `dModifierEscaped_` (2 bools → 1 byte enum).

```cpp
enum class EscapeKind : uint8_t {
    None = 0,
    Tone,           // ss, ff, rr, xx, jj (Telex) / 11, 22... (VNI)
    Circumflex,     // aa, ee, oo escape / 6-key escape
    Horn,           // ơ[], ư], w-pairs, P4 / 7-key escape
    Breve,          // ă→a via ww / 8-key escape
    Stroke,         // ddd / d99 — replaces dModifierEscaped_
    Modifier,       // VNI generic (L625 fallback — covers circ/horn/breve in one pass)
};

struct EscapeState {
    EscapeKind kind = EscapeKind::None;

    [[nodiscard]] constexpr bool isEscaped() const noexcept {
        return kind != EscapeKind::None;
    }
    [[nodiscard]] constexpr bool isEscaped(EscapeKind k) const noexcept {
        return kind == k;
    }
    constexpr void escape(EscapeKind k) noexcept { kind = k; }
    constexpr void clear() noexcept { kind = EscapeKind::None; }
};
```

### 2.2 QuickConsonantState

Replaces 4 scattered fields → 1 struct. `quickStartKey_` stays SEPARATE (different lifecycle).

```cpp
struct QuickConsonantState {
    size_t idx = SIZE_MAX;
    wchar_t lastKey = 0;
    bool onlyQC = false;
    bool escaped = false;

    constexpr void Reset() noexcept {
        idx = SIZE_MAX; lastKey = 0; onlyQC = false; escaped = false;
    }
    constexpr void clearActive() noexcept {
        idx = SIZE_MAX; lastKey = 0;
    }
    constexpr void markEscaped() noexcept {
        escaped = true; clearActive();
    }
    [[nodiscard]] constexpr bool hasActive() const noexcept {
        return idx != SIZE_MAX;
    }
};
```

### 2.3 Resulting Member Variables

```cpp
// AFTER refactor:
EscapeState escape_;                    // Replaces toneEscaped_ + dModifierEscaped_
QuickConsonantState qc_;                // Replaces 4 QC fields
wchar_t quickStartKey_ = 0;            // STAYS SEPARATE — different lifecycle
bool spellCheckDisabled_ = false;       // STAYS — derived signal, not state
EnglishProtectionState engProt_;        // STAYS — already clean
```

---

## 3. Complete Mapping Table

### 3.1 SET SITES: `toneEscaped_ = true` → `escape_.escape(EscapeKind::X)`

| # | Engine | Line | Context | EscapeKind | Notes |
|---|--------|------|---------|------------|-------|
| 1 | Telex | L364 | ProcessTone — double tone (ss,ff) | `Tone` | |
| 2 | Telex | L410 | ProcessModifier — bracket `[[` | `Horn` | |
| 3 | Telex | L432 | ProcessModifier — bracket `]]` | `Horn` | |
| 4 | Telex | L473 | ProcessModifier — circumflex oo + spellCheck | `Circumflex` | Has spellCheck guard |
| 5 | Telex | L512 | ProcessModifier — free-mark circumflex | `Circumflex` | Backward scan path |
| 6 | Telex | L698 | ProcessWModifier — ươ→uo pair (non-edge) | `Horn` | |
| 7 | Telex | L706 | ProcessWModifier — uơ→uo edge (h/th/kh) | `Horn` | |
| 8 | Telex | L728 | ProcessWModifier — synthetic ư ww (P8) | `Horn` | |
| 9 | Telex | L734 | ProcessWModifier — horn vowel P4 | `Horn` | |
| 10 | Telex | L740 | ProcessWModifier — breve vowel P4 | `Breve` | Different kind! |
| 11 | Telex | L817 | ProcessDModifier — ddd stroke | `Stroke` | Also replaces dModifierEscaped_=true at L816 |
| 12 | VNI | L487 | Stroke 99 escape | `Stroke` | Also replaces dModifierEscaped_=true at L486 |
| 13 | VNI | L533 | Horn pair ươ→uo (non-edge) | `Horn` | |
| 14 | VNI | L540 | Horn pair uơ→uo (edge) | `Horn` | |
| 15 | VNI | L625 | Generic modifier escape (Pass 2) | `Modifier` | VNI uses one loop for all mod types |
| 16 | VNI | L652 | ProcessTone — double tone (11,22) | `Tone` | |

### 3.2 SET SITES: `toneEscaped_ = false` → `escape_.clear()`

| # | Engine | Line | Context |
|---|--------|------|---------|
| 17 | Telex | L371 | ProcessTone — tone applied |
| 18 | Telex | L1033 | Backspace — clear on edit |
| 19 | Telex | L1168 | Reset |
| 20 | VNI | L319 | Backspace |
| 21 | VNI | L452 | Reset |
| 22 | VNI | L645 | ProcessTone — tone applied |

### 3.3 `dModifierEscaped_` — ELIMINATED (merged into EscapeKind::Stroke)

| # | Engine | Line | Old | New |
|---|--------|------|-----|-----|
| 23 | Telex | L816 | `= true` | Merged into #11 (Stroke) |
| 24 | Telex | L1032 | `= false` | Merged into #18 (clear()) |
| 25 | Telex | L1167 | `= false` | Merged into #19 (clear()) |
| 26 | VNI | L486 | `= true` | Merged into #12 (Stroke) |
| 27 | VNI | L318 | `= false` | Merged into #20 (clear()) |
| 28 | VNI | L451 | `= false` | Merged into #21 (clear()) |

### 3.4 READ SITES

| # | Engine | Line | Old | New | Notes |
|---|--------|------|-----|-----|-------|
| R1 | Telex | L206 | `toneEscaped_` | `escape_.isEscaped()` | Tone gate |
| R2 | Telex | L240 | `!toneEscaped_` | `!escape_.isEscaped()` | EngProt bias |
| R3 | Telex | L268 | `toneEscaped_` | `escape_.isEscaped()` | blockModifiers |
| R4 | Telex | L806 | `dModifierEscaped_` | `escape_.isEscaped(EscapeKind::Stroke)` | ProcessDModifier guard |
| R5 | VNI | L195 | `toneEscaped_` | `escape_.isEscaped()` | Tone gate |
| R6 | VNI | L228 | `!toneEscaped_` | `!escape_.isEscaped()` | EngProt bias |
| R7 | VNI | L244 | `toneEscaped_` | `escape_.isEscaped()` | blockMod |
| R8 | VNI | L476 | `dModifierEscaped_` | `escape_.isEscaped(EscapeKind::Stroke)` | Stroke guard |
| R9 | VNI | L734 | `toneEscaped_` | `escape_.isEscaped()` | ONLY in VNI — ApplyAutoUO guard |

**Total: 28 SET changes + 9 READ changes = 37 mechanical edits.**

### 3.5 Public API Impact

`HasActiveQuickConsonant()` in both engines reads `quickConsonantIdx_` directly:
```cpp
[[nodiscard]] bool HasActiveQuickConsonant() const override { return quickConsonantIdx_ != SIZE_MAX; }
```
After refactor: `return qc_.hasActive();`

`Commit()` reads `quickConsonantOnly_`, `quickConsonantIdx_`, `quickStartKey_` — all must be updated to use struct accessors.

**Note:** VniEngine uses `std::wstring rawInput_` while TelexEngine uses `std::vector<wchar_t> rawInput_`. This does NOT affect the flag refactor, but be aware when copy-pasting patterns — syntax for `.push_back()` vs `+=` differs.

---

## 4. Backspace() — Critical Order-of-Operations

Backspace() is the MOST DANGEROUS function for this refactor. Each early return path updates DIFFERENT flag subsets.

```
Backspace():
  [TOP] escape_.clear()                    // UNCONDITIONAL — must stay at top
  
  [EARLY RETURN 2] Quick start undo:
    - Clears quickStartKey_
    - Does NOT clear qc_ fields           // ← Do NOT call qc_.Reset() here!
    - return
    
  [AFTER RETURN 2] quickStartKey_ = 0
  
  [EARLY RETURN 3] Quick consonant undo:
    - qc_.markEscaped()                    // SETS escaped=true, clears idx+lastKey
    - Does NOT clear qc_.onlyQC            // ← Intentional! Session-level flag
    - return
    
  [AFTER RETURN 3] qc_.clearActive()       // Clear idx+lastKey for normal BS
  
  [NORMAL PATH] pop state, trim raw, recalc
```

**Rules:**
- `escape_.clear()` MUST be at top, before any early return
- `qc_.Reset()` ONLY safe in full `Reset()` — NEVER in Backspace
- `qc_.markEscaped()` in early return 3 = SET escaped + clear active (partial update)
- `qc_.onlyQC` NEVER cleared in Backspace — only in PushChar(L87) and Reset()

---

## 5. Exception Cascade (L268-304)

The modifier gate combines escape state + English protection. With EscapeState:

```cpp
// BEFORE:
bool blockModifiers = toneEscaped_ ||
    (!config_.allowEnglishBypass && engProt_.bias == LanguageBias::HardEnglish);

// AFTER (semantically identical):
bool blockModifiers = escape_.isEscaped() ||
    (!config_.allowEnglishBypass && engProt_.bias == LanguageBias::HardEnglish);
```

**Decision: Keep semantics identical.** Do NOT use EscapeKind to change cascade logic. The spell exclusion punch-through for dd→đ stays unchanged. Stability over cleverness.

---

## 6. VniEngine Differences

**90% identical pattern, 10% needs care:**

| Aspect | Telex | VNI | Action |
|--------|-------|-----|--------|
| SET-TRUE sites | 12 | 5 | VNI simpler, fewer paths |
| Bracket keys | Yes ([, ]) | No | N/A for VNI |
| Free-mark circumflex | Yes | No | N/A for VNI |
| Synthetic ư P8 | Yes | No | N/A for VNI |
| Generic modifier escape | Per-type handlers | Single Pass 2 loop (L625) | Use `EscapeKind::Modifier` |
| ApplyAutoUO + escape check | NO check | YES (L734) | Keep difference! |
| Modifier gate | `blockModifiers` | `blockMod` | Same logic, different var name |

**ApplyAutoUO L734:** VniEngine checks `toneEscaped_` before running AutoUO. TelexEngine does NOT. This is likely intentional — preserve during refactor. Do NOT "unify" engines here.

---

## 7. QuickConsonant State Machine

```
IDLE ──[f/j/w start]──► QUICK_START (quickStartKey_=c)
                          │── vowel → proceed, clear startKey
                          │── non-vowel → UNDO → IDLE

IDLE ──[cc/gg/nn...]──► ACTIVE_QC
                          │ idx=pos, lastKey=key, onlyQC=true/false
                          │── any char → onlyQC=false
                          │── same key → suppress (literal)
                          │── BS at idx → ESCAPED (escaped=true)
                          │── Commit + onlyQC → auto-restore
                          │── Commit + hasActive → skip auto-restore

ESCAPED ──[same key]──► suppress QC rule
        ──[other key]──► IDLE (clear)
```

**Key invariants:**
- `idx != SIZE_MAX` ↔ `lastKey != 0` (always together)
- `onlyQC == true` → `idx != SIZE_MAX`
- `escaped == true` → `idx == SIZE_MAX`
- `quickStartKey_` is SEPARATE — different lifecycle, different concerns

---

## 8. Execution Plan

### Git Strategy

Work on feature branch `refactor/escape-state-enum`. Each session = 1 atomic commit. If session 2 fails, revert to session 1 commit (EscapeState alone is a valid intermediate state).

### Session 1: EscapeState (estimated 1-2 hours)

1. Create `EscapeState` struct in `EngineHelpers.h`
2. TelexEngine.h: Replace `toneEscaped_` + `dModifierEscaped_` with `EscapeState escape_`
3. TelexEngine.cpp: Apply mapping table (12 set-true → escape(), 2 clear → clear(), 3 read sites)
4. Remove `dModifierEscaped_` entirely (6 lines across set/clear/reset)
5. Update `HasActiveQuickConsonant()` if touching QC (session 2 only)
6. Build + run `TelexEngineTest` + `FeatureOptionsTest` + `SpellCheckerTest` — must pass 100%
7. Repeat for VniEngine (5 set-true, 2 clear, 4 read sites + L734)
8. Build + run `VniEngineTest` — must pass 100%
8. Commit: "refactor: replace toneEscaped_/dModifierEscaped_ with EscapeState enum"

### Session 2: QuickConsonantState (estimated 1-2 hours)

1. Create `QuickConsonantState` struct with Reset(), clearActive(), markEscaped(), hasActive()
2. TelexEngine.h: Replace 4 QC fields with `QuickConsonantState qc_`
3. TelexEngine.cpp: Apply field renames (qc_.idx, qc_.lastKey, qc_.onlyQC, qc_.escaped)
4. CAREFUL: Backspace() — use markEscaped() for return 3, clearActive() for normal path
5. CAREFUL: Keep `quickStartKey_` separate
6. Build + test
7. Repeat for VniEngine
8. Commit: "refactor: consolidate quick consonant fields into QuickConsonantState struct"

### Pre-refactor checklist:
- [ ] v2.1.12 released and stable
- [ ] Create feature branch `refactor/escape-state-enum`
- [ ] Run full test suite — green baseline
- [ ] Add test: `ooo` with spellCheck ON vs OFF
- [ ] Add test: `ddd` with spellCheck ON vs OFF
- [ ] Add test: Backspace after quick start → re-type
- [ ] Add test: Backspace after quick consonant → escape suppression
- [ ] Add test: VNI `uo` → verify ApplyAutoUO behavior (L734 difference)
- [ ] Add test: Backspace all chars → verify full state reset

### Post-refactor verification:
- [ ] Run ALL test suites: TelexEngineTest, VniEngineTest, FeatureOptionsTest, SpellCheckerTest
- [ ] Smoke test on Windows: type `ooo`, `ddd`, `đường`, backspace mid-word in Notepad + Chrome
- [ ] Merge to Main after verification

---

## 9. What NOT To Do

- Do NOT use `std::variant` — overkill, no per-kind data payloads needed
- Do NOT use bitfield — invariant is single escape active per word
- Do NOT change exception cascade semantics (L268-304)
- Do NOT call `qc_.Reset()` in Backspace — partial updates required
- Do NOT "unify" VniEngine ApplyAutoUO with TelexEngine — behavioral difference is likely intentional
- Do NOT refactor `spellCheckDisabled_` or `EnglishProtectionState` — already clean
- Do NOT copy Go Nhanh's `last_transform` pattern directly — their replay-based undo model is different from NexusKey's incremental mutation
- Do NOT add implicit conversion operators on EscapeKind enum class — keep type safety

---

## 10. Risk Assessment

| Risk | Level | Mitigation |
|------|-------|-----------|
| Set-site mapping error (wrong EscapeKind) | Medium | Use this checklist, grep verify |
| Backspace early return regression | HIGH | Do NOT use qc_.Reset() in Backspace |
| VNI ApplyAutoUO behavior change | Medium | Preserve L734 check |
| L473 circumflex + spellCheck config | Medium | Add test before refactoring |
| Test coverage gap | MEDIUM → LOW after adding pre-refactor tests | Add 6 tests from checklist before starting |
| `dModifierEscaped_` merge | ~ZERO | Always co-set, never co-read |

**Overall risk: LOW (after pre-refactor tests added)** — mechanical refactor with comprehensive black-box test coverage + targeted new tests for high-risk paths.
