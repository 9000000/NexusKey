# TypingEngine Unification — Combined Mode + Maintenance Reduction

## Problem

1. TelexEngine (34K) and VniEngine (18K) share ~60-65% logic but maintain separate implementations. Bug fixes and new features must be applied to both.
2. Users request a Combined input mode (Telex + VNI triggers simultaneously).

## Decision

Rename TelexEngine to **TypingEngine**, add VNI key handling, deprecate VniEngine. Single class, single buffer, mode dispatch via `config_.inputMethod`.

### Why not other approaches

| Approach | Rejected because |
|----------|-----------------|
| CombinedEngine wrapper (Composite) | Two engines hold independent state — cannot share CharState buffer. Key routing too simplistic for context-dependent triggers. |
| Template base class (Approach 1) | Adds inheritance/CRTP complexity. CombinedEngine needs both Telex+VNI methods — multiple inheritance or composition, both messy. Debug harder. |
| Keep separate + extract more to EngineHelpers | Doesn't solve duplication in PushChar pipeline, Backspace, Commit, Reset. |

## Design

### Unified Types (namespace NextKey::)

```cpp
enum class Modifier : uint8_t { None, Circumflex, Breve, Horn, Stroke };
enum class Tone : uint8_t { None, Acute, Grave, Hook, Tilde, Dot };

struct CharState {
    wchar_t base = 0;
    Modifier mod = Modifier::None;
    Tone tone = Tone::None;
    bool isUpper = false;
    bool synthetic = false;       // Telex P8 only, default false
    size_t rawIdx = 0;
    size_t toneRawIdx = SIZE_MAX; // EraseConsumedRaw, default SIZE_MAX
    // Methods: IsVowel(), CanHaveMod(), IsD(), IsHorn(), HasModifier()
};

enum class InputMethod : uint8_t {
    Telex = 0, VNI = 1, SimpleTelex = 2, Combined = 3
};
```

### Mode Helpers

```cpp
bool isTelexMode() const noexcept {
    return config_.inputMethod != InputMethod::VNI;
}
bool isVniMode() const noexcept {
    return config_.inputMethod == InputMethod::VNI ||
           config_.inputMethod == InputMethod::Combined;
}
```

### PushChar Pipeline

Trigger key sets are fully disjoint (Telex=letters, VNI=digits) — no conflicts in Combined mode.

```
PushChar(c):
  rawInput_ += c

  SHARED: Quick Start Consonant (f->ph, j->gi, w->qu)
  SHARED: Quick Consonant (cc->ch, gg->gi, nn->ng...)
  SHARED: uu->uo expansion

  TONE (mode-gated):
    Clear: Telex 'z' / VNI '0' -> ProcessClearTone()
    Apply: Telex s/f/r/x/j -> TelexKeyToTone()
           VNI 1/2/3/4/5   -> VniKeyToTone()
    -> Shared gates (spellCheck, escape, English protection*)
    -> ProcessTone(Tone, cachedTarget)
    * English raw-prefix check only for Telex keys

  MODIFIER (mode-gated):
    Telex only: [ -> o+horn, ] -> u+horn
    Telex only: w/aa/ee/oo/dd -> ProcessTelexModifier()
    VNI only:   6/7/8/9 -> ProcessVniModifier()

  SHARED: Quick End Consonant (g->ng, h->nh, k->ch)
  SHARED: ProcessChar() + RelocateToneToTarget()
          + ApplyAutoUO() + UpdateSpellState() + CheckEnglishBias()
```

### New VNI Methods (~200 lines)

```cpp
// Key mapping
static Tone VniKeyToTone(wchar_t c);    // 1->Acute ... 5->Dot
static bool IsVniToneKey(wchar_t c);    // '1'-'5'
static bool IsVniModifierKey(wchar_t c); // '6'-'9'

// Modifier dispatch
bool ProcessVniModifier(wchar_t c);
//   '9' -> ProcessDModifier(c)              // reuse existing
//   '6' -> ProcessVniVowelModifier(Circumflex, c)
//   '7' -> ProcessVniHornModifier(c)        // uo pair + uu + generic
//   '8' -> ProcessVniVowelModifier(Breve, c)

bool ProcessVniHornModifier(wchar_t c);
//   uo pair cycle (2-state / 3-state for h/th/kh)
//   uu pattern (horn first u)
//   generic: rightmost o/u -> horn

bool ProcessVniVowelModifier(Modifier mod, wchar_t key);
//   Pass 1: rightmost eligible unmodified vowel -> apply
//   Pass 1.5: modifier switching (a^+8->a(breve), o^+7->o(horn))
//   Pass 2: escape -> clear modifier, add digit literal
```

### Reused Methods (no change or signature-only change)

| Method | Change |
|--------|--------|
| ProcessDModifier(c) | None — works for dd and d9 |
| ProcessClearTone() | None — works for z and 0 |
| ProcessTone() | Signature: takes Tone enum instead of wchar_t |
| ProcessWModifier(c) | None — Telex w only |
| ProcessTelexModifier(c) | Renamed from ProcessModifier() |

### Edge Case Decisions

1. **Escape across modes**: Block. After Telex ss escape, VNI 1 is also blocked. User backspaces to retry.
   - `a+s+1` → `a1` (VNI Acute escapes Telex Acute — same tone twice = clear)
   - `a+s+2` → `à` (VNI Grave replaces Telex Acute — different tone = replace)
   - `a+s+s+1` → `as1` (escape state blocks VNI tone)
   - `d+d+9` → `d9` (VNI stroke escapes Telex stroke)
2. **Combined = full Telex + VNI**: Not paired with SimpleTelex. SimpleCombined can be added later.
3. **EraseConsumedRaw for VNI**: Yes. Keeps raw/composed consistent across modes.
4. **Cross-mode modifier interaction**: Works naturally. Telex aa (circumflex) + VNI 8 (breve) -> Pass 1.5 switches. VNI 6 (circumflex) + Telex w (breve) -> P7 switches.

### Risk Mitigation: VNI Keys vs English Protection

**Problem**: `IsHardEnglishToneContext()` is a no-op for VNI keys because `IsHardEnglishEnd(digit)` returns false immediately. This means the V+C+V structural check (catches "manager", "behavior" mid-typing) does not fire for VNI tone keys 1-5.

The 3-tier `CheckEnglishBias()` (runs after every ProcessChar) catches MOST English patterns:
- Invalid start clusters (cl, cr, br...) → HardEnglish ✓
- Invalid coda (dm, pp, nd...) → HardEnglish ✓ (`admin123` safe)
- Hard end consonant after vowel (s, f, r, x, j, z) → HardEnglish ✓

But there's a gap: mid-typing V+C+V patterns where the coda hasn't become invalid yet.

**Fix**: Add a VNI-specific structural check in the VNI tone gate, without the `IsHardEnglishEnd` guard:

```cpp
// In VNI tone pipeline, after HardEnglish/SoftEnglish check:
if (states_.size() >= 4) {
    if (HasStructuralVCVPattern(states_.data(), states_.size())) {
        engProt_.bias = LanguageBias::HardEnglish;
        asLiteral(); return;
    }
}
```

`HasStructuralVCVPattern()` extracts the V+C(1+)+V scan from `IsHardEnglishToneContext()` without the `IsHardEnglishEnd` key guard. Gives VNI keys the same structural protection as Telex keys.

**Not a Combined-specific issue**: Words that look like valid Vietnamese (`an1`→`án`, `con1`→`cón`) behave the same in pure VNI mode — this is expected VNI behavior, not a bug.

## Migration Plan

### Phase 1 — TypingEngine + Combined mode

1. Unify CharState/Modifier/Tone into NextKey:: namespace
2. Rename TelexEngine -> TypingEngine (file + class)
3. Add backward-compat aliases in Telex:: namespace (TelexEngine = TypingEngine)
4. Add InputMethod::Combined
5. Add VNI methods + mode gates in PushChar
6. Update EngineFactory for Combined
7. Tests: existing 72K TelexEngineTest via alias. New CombinedEngineTest.

### Phase 2 — VNI mode via TypingEngine

1. EngineFactory returns TypingEngine for InputMethod::VNI
2. Run VniEngineTest cases against TypingEngine(VNI) — verify parity
3. Fix behavioral delta if any
4. VniEngine no longer instantiated

### Phase 3 — Cleanup

1. Delete VniEngine.h/cpp
2. Remove Telex::/Vni:: namespace aliases
3. Migrate test references to TypingEngine directly

## Files to Update

| File | Phase | Change |
|------|-------|--------|
| src/core/engine/TypingEngine.h (new) | P1 | Renamed from TelexEngine.h + unified CharState |
| src/core/engine/TypingEngine.cpp (new) | P1 | Renamed from TelexEngine.cpp + VNI methods |
| src/core/engine/EngineFactory.cpp | P1+P2 | Return TypingEngine for Combined, then VNI |
| src/core/config/TypingConfig.h | P1 | Add Combined = 3 |
| src/core/config/ConfigManager.cpp | P1 | Parse/save Combined value |
| src/app/dialogs/SettingsDialog.cpp | P1 | Add Combined to dropdown |
| src/app/ui/settings/settings.js | P1 | Add Combined option |
| src/core/ipc/SharedState.h | P1 | Handle new enum value |
| src/core/engine/VniEngine.h/cpp | P3 | Delete |
| tests/CombinedEngineTest.cpp (new) | P1 | Combined mode tests |
| src/tsf/EngineController.cpp | None | Uses IInputEngine interface |
| src/app/system/HookEngine.cpp | None | Uses IInputEngine interface |
