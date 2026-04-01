---
type: bmad-distillate
sources:
  - "pre-tone-stop-final-check.md"
downstream_consumer: "NexusKey engine development — stop-final tone blocking rules, test cases, implementation details"
created: "2026-04-01"
token_estimate: 1800
parts: 1
---

## Problem
- Tone key on stop-final coda produces phonologically invalid Vietnamese (e.g., `ocr`→`ỏc`; `ỏc` doesn't exist)
- Stop finals (c, ch, k, p, t) only accept Ngang, Sắc(s/1), Nặng(j/5); Huyền(f/2), Hỏi(r/3), Ngã(x/4) are impossible
- Root cause: `spellCheckDisabled_` set AFTER `ProcessTone()` — off-by-one timing; SpellChecker correctly detects invalid but too late

## Fix: Pre-Tone Stop-Final Check
- Before `ProcessTone()`, check: if coda is stop-final AND tone is Grave/Hook/Tilde → treat as literal immediately
- Phonological structural rule — no dictionary lookup needed
- Only active when `config_.spellCheckEnabled && !spellCheckDisabled_`

## HasStopFinalCoda Helper (`EngineHelpers.h`)
- Template function `HasStopFinalCoda<CharStateT>(states, count)` → bool
- Scans backward from end; finds consonant group; checks preceded by vowel
- codaLen==1: c, k, p, t → true; codaLen==2: ch → true; else false
- `count < 2` → false; no vowel before coda → false
- Note: removed `IsD()` from loop (đ is not vowel nucleus)

## Engine Integration
- **TelexEngine.cpp**: Insert after `HasInvalidAdjacentVowelPair` check, before `ProcessTone(c)`
- **VniEngine.cpp**: Same position; uses `KeyToTone(c)` (not `c-'0'`)
- Gate order: spellCheckDisabled_ → toneEscaped_ → HardEnglish → SoftEnglish → IsHardEnglishToneContext → HasInvalidAdjacentVowelPair → **[NEW] HasStopFinalCoda** → ProcessTone()

## Code Pattern (both engines)
```cpp
if (config_.spellCheckEnabled && !spellCheckDisabled_) {
    Tone requested = KeyToTone(c);
    if (requested == Tone::Grave || requested == Tone::Hook || requested == Tone::Tilde) {
        if (HasStopFinalCoda(states_.data(), states_.size())) {
            asLiteral(); return;
        }
    }
}
```

## Stop Final × Tone Lookup

| Coda | Valid tones | Invalid tones | Notes |
|------|------------|---------------|-------|
| c | Sắc(s/1), Nặng(j/5) | Huyền(f/2), Hỏi(r/3), Ngã(x/4) | ốc, ọc valid |
| ch | Sắc, Nặng | Huyền, Hỏi, Ngã | ách, ịch valid |
| k | Sắc only (ắk) | Huyền, Hỏi, Ngã | k valid after ă only (Đắk Lắk) |
| p | Sắc, Nặng | Huyền, Hỏi, Ngã | ập, ịp valid |
| t | Sắc, Nặng | Huyền, Hỏi, Ngã | ất, ột valid |

- Non-stop finals (m, n, ng, nh): ALL 6 tones valid — not affected

## Input→Output Matrix

| Input | Before fix | After fix | Rule |
|-------|-----------|-----------|------|
| `ocr` | ỏc (wrong) | ocr (literal) | Hook+c blocked |
| `ocf` | òc (wrong) | ocf (literal) | Grave+c blocked |
| `ocx` | õc (wrong) | ocx (literal) | Tilde+c blocked |
| `ocs` | ốc ✓ | ốc ✓ | Acute+c allowed |
| `ocj` | ọc ✓ | ọc ✓ | Dot+c allowed |
| `atr`/`atf`/`atx` | ảt/àt/ãt (wrong) | atr/atf/atx (literal) | blocked+t |
| `ats`/`atj` | át/ạt ✓ | át/ạt ✓ | allowed+t |
| `achr`/`achf`/`achx` | ảch/àch/ãch (wrong) | achr/achf/achx (literal) | blocked+ch |
| `achs` | ách ✓ | ách ✓ | allowed+ch |
| `awkr` | ảk (wrong) | awkr (literal) | Hook+k; must use aw (ă) path |
| `awks` | ắk ✓ | ắk ✓ | Acute+k allowed |
| `anr`/`amf` | ản/àm ✓ | ản/àm ✓ | non-stop finals unaffected |
| `ocrr` | ocr | ocrr (both literal) | no double-escape needed |

## Edge Cases
- Tone escape with non-stop finals unchanged: `anrr`→`ản`→`anr` (escape works normally)
- Modifier before coda: `oowcr`→literal (ô+c, Hook blocked); `oowcs`→ốc (Acute allowed)
- Consonant onset: `bocr`→bocr literal (c preceded by vowel o → stop detected)
- Onset `ch` vs coda `ch`: backward scan disambiguates — `cha`=onset (no block), `ach`=coda (blocked)
- `toneEscaped_` check runs BEFORE pre-tone check — no conflict
- SpellChecker rejects `k` after non-ă vowels → `spellCheckDisabled_=true` before tone key → pre-tone check only handles `ă+k` path (test with `"awkr"` not `"akr"`)

## Files Changed
- `src/core/engine/EngineHelpers.h` — add `HasStopFinalCoda<T>` template
- `src/core/engine/TelexEngine.cpp` — add pre-tone check ~line 212
- `src/core/engine/VniEngine.cpp` — add pre-tone check at tone digit block
- `tests/SpellCheckerTest.cpp` — add ~22 tests (14 Telex + 6 VNI + 2 k), update 2 existing tests

## Test Updates
- Rename `ToneEscape_WithCoda_SpellCheckOn` → `StopFinal_OCR_DirectLiteral` (input `"ocr"` → `"ocr"`)
- Add `StopFinal_OCRR_BothLiteral` (`"ocrr"` → `"ocrr"`)
- VNI equivalent: `StopFinal_OC3_DirectLiteral`, `StopFinal_OC33_BothLiteral`

## Scope Unchanged
- EnglishProtection.h, SpellChecker.cpp, `spellCheckEnabled=false` flow, `autoRestoreEnabled` logic — no changes
