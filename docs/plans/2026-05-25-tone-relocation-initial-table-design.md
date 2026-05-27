# §7 Tone Relocation + Initial Consonant Table — Design

**Date:** 2026-05-25
**Branch:** `feat/architecture-review-v3.1`
**Status:** Design validated, ready for implementation
**Driver bug:** `s-u-s-a-a-t → súaat` (expect `suất`)
**Brainstorm transcript:** session 2026-05-25 (anh + claude)

## 1. Problem statement

Vietnamese phonology spec (§7 in `docs/vietnamese-phonology-spec-distillate.md`) says: **any vowel-modifier applied AFTER tone is already placed → tone relocates** to satisfy §5 P1/P2 invariant (tone sits on Last Horn vowel or Modified vowel).

Current code applies §7 ONLY for horn modifier and only when pre-state is `ValidPrefix` for circumflex. The "of-extra-a defensive guard" (commit `ad09f15`) blocks circumflex relocation when pre-state is `Valid` — to reject typo `cuara → cuẩ`. Side effect: blocks legitimate `susaat → suất` too, because validator can't distinguish `súa` (mid-construction) from `của` (complete word) — they have identical structure.

## 2. Root cause analysis

Three options considered:
- A. Accept all (drop defensive guard) — breaks `cuara → củaa` test.
- B. Heuristic via `toneClusterSizeAtFire` field on CharState — distinguishes susaat (tone fired with 1 vowel) vs cuara (tone fired with 2 vowels). Generalizes but adds state.
- C. **Productive-initial filter** (chosen) — Vietnamese lexicon constraint: only some consonants productive with `Cuâ-` cluster (s, t, x, ch, h, kh, l, nh, qu, th, tr). `c, d, m, b, v...` non-productive.

Anh chose C (`củaa` keep defensive). Validator structural-only — adding "phonotactic productivity" hint without becoming full lexicon.

### Empirical Vietnamese dictionary survey

| Cluster | Productive initials | Non-productive |
|---|---|---|
| `uâ` | ch, h, kh, l, nh, qu, s, t, th, tr, x (11) | b, c, d, đ, g, m, n, ng, ph, r, v (12+) |
| `uê` | h, kh, qu, t, th (5) | everything else |
| `uô` | almost all (b, c, ch, d, đ, g, h, kh, l, m, n, ng, nh, ph, qu, r, s, t, th, tr, v, x) | — |
| `iê` | nearly all | — |
| Single-vowel `â/ê/ô/ơ/ư/ă` | nearly all | — |

Only `uâ` and `uê` need productive filter. Others natively productive.

## 3. Code smell audit (parallel discovery)

Mid-brainstorm, anh hỏi "why 2 functions instead of a table?" — exposing broader code smell:

| Smell location | Description | Verdict |
|---|---|---|
| `IsHardEnglishStart` (EnglishProtection.h:54) | 21 hardcoded `(c0=='X' && c1=='Y')` clauses | Refactor to table |
| `ParseInitialConsonant` (PhonotacticsValidator.cpp:184) | If-chain for 10 2-char + 16 1-char initials | Refactor to table |
| `IsTriphthong` (VietnameseTables.h:222) | 6 hardcoded triples | Borderline — small, leave |
| `IsUOEdgeCasePrefix` (EngineHelpers.h:179) | 3 prefixes | Borderline — leave |

**Bundling decision (anh):** §7 fix + `IsHardEnglishStart` + `ParseInitialConsonant` refactor → single PR with shared `InitialId` enum.

## 4. Design

### 4.1. New module: `InitialConsonantTable` (in `VietnameseTables.h` or new header)

```cpp
namespace NextKey {

// All Vietnamese initial consonants (canonical + ZWJF substitutes mapped at parse).
// Order kept stable — bitmask references rely on enum value.
enum class InitialId : uint8_t {
    None = 0,  // No initial / vowel-initial syllable
    B, C, Ch, D, Dd /*đ*/, G, Gh, Gi, H, K, Kh, L, M, N,
    Ng, Ngh, Nh, P, Ph, Q, Qu, R, S, T, Th, Tr, V, X,
    Count
};
static_assert(static_cast<size_t>(InitialId::Count) <= 32,
              "InitialId bitmask uses uint32_t");

[[nodiscard]] constexpr uint32_t Bit(InitialId i) noexcept {
    return 1u << static_cast<uint8_t>(i);
}

// Hard-English onset bitmask: row indexed by first consonant, bits set for
// second consonants that form impossible Vietnamese onsets.
// Replaces IsHardEnglishStart's 21 hardcoded clauses.
// Lookup: HardEnglishOnsetMask(c0) & Bit(c1).
[[nodiscard]] constexpr uint32_t HardEnglishOnsetMask(wchar_t c0) noexcept;

// Vowel cluster identifier (only those needing productive-initial filter).
// Add row when bug surfaces for new cluster.
enum class VowelCluster : uint8_t {
    UaCirc,  // uâ — chuẩn, huấn, luật, nhuần, quân, suất, thuần, truân, tuần, xuân
    UeCirc,  // uê — Huế, huệ, khuê, quê, thuê, tuệ
    Count
};

// Productive-initial mask per cluster.
constexpr uint32_t kClusterProductive[static_cast<size_t>(VowelCluster::Count)] = {
    /* UaCirc */
        Bit(InitialId::Ch) | Bit(InitialId::H)  | Bit(InitialId::Kh) |
        Bit(InitialId::L)  | Bit(InitialId::Nh) | Bit(InitialId::Qu) |
        Bit(InitialId::S)  | Bit(InitialId::T)  | Bit(InitialId::Th) |
        Bit(InitialId::Tr) | Bit(InitialId::X),
    /* UeCirc */
        Bit(InitialId::H)  | Bit(InitialId::Kh) | Bit(InitialId::Qu) |
        Bit(InitialId::T)  | Bit(InitialId::Th),
};

[[nodiscard]] constexpr bool IsProductiveInitial(
        VowelCluster c, InitialId i) noexcept {
    if (c == VowelCluster::Count || i == InitialId::None) return false;
    return (kClusterProductive[static_cast<size_t>(c)] & Bit(i)) != 0;
}

// Parse initial from states. Returns {InitialId, length consumed}.
// ZWJF substitution baked in: w → Qu, f → Ph, z|j → Gi.
// `đ` (d with Modifier::Stroke) maps to InitialId::Dd.
struct InitialParse {
    InitialId id;
    size_t    length;
};
template<typename CharStateT>
[[nodiscard]] InitialParse ParseInitialId(
        const CharStateT* states, size_t count, bool allowZwjf) noexcept;

}  // namespace NextKey
```

### 4.2. Callsite migrations

**A. `IsHardEnglishStart` (EnglishProtection.h)**

```cpp
// BEFORE: 21 hardcoded pairs.
// AFTER:
[[nodiscard]] inline bool IsHardEnglishStart(wchar_t c0, wchar_t c1) noexcept {
    return (HardEnglishOnsetMask(towlower(c0)) >>
            (towlower(c1) - L'a')) & 1;
}
```

(Or refactor caller to use `ParseInitialId` directly + lookup table.)

**B. `ParseInitialConsonant` (PhonotacticsValidator.cpp)**

```cpp
// AFTER: thin wrapper for backward compat.
template<typename CharStateT>
size_t ParseInitialConsonant(const CharStateT* states, size_t count,
                              bool allowZwjf) {
    return ParseInitialId(states, count, allowZwjf).length;
}
```

Existing callers in PhonotacticsValidator continue working; new callers use `ParseInitialId` directly for ID + length.

**C. `HandleAdjacentCircumflex` Valid branch (TypingEngine.cpp:937-963)**

```cpp
// Detect "Cua" structure and apply productive filter.
auto preState = Phonology::ValidateSyllableState(...);
const bool isValidPrefix =
    (preState == Phonology::SyllableState::ValidPrefix);

// New: also apply speculate-relocate when initial productive with cluster.
bool isProductivePromotion = false;
if (!isValidPrefix && targetBase == L'a' && IsCuaShape(states_)) {
    auto init = ParseInitialId(states_.data(), states_.size(), config_.allowZwjf);
    isProductivePromotion = IsProductiveInitial(VowelCluster::UaCirc, init.id);
}

const bool needsRelocate = isValidPrefix || isProductivePromotion;

if (needsRelocate) {
    if (!WouldBeValidSyllable(idx, Modifier::Circumflex,
                              SIZE_MAX, /*speculateRelocateTone=*/true)
        && !WouldModifierKeyMatchExclusion(c)) return false;
} else {
    if (ShouldRejectModifier(idx, Modifier::Circumflex, targetBase)) return false;
}
last.mod = Modifier::Circumflex;
if (needsRelocate) RelocateToneToTarget();
return true;
```

Helper `IsCuaShape(states_)`: returns true when states is `[initial..., u, a]` (last 2 vowels are u + a, no modifier, no other vowels in between). Lifted from inline check.

**D. `ProcessVniVowelModifier` Pass 1 (TypingEngine.cpp:1953-1968)**

Same gate addition: `isValidPrefix || isProductivePromotion`. Closes TODO "VNI parity" in `docs/TODO.md`.

### 4.3. Helper consolidation

Add to TypingEngine private:
```cpp
[[nodiscard]] bool IsProductiveCuaCircPromotion() const;
```
Returns `IsCuaShape(states_) && IsProductiveInitial(UaCirc, ParseInitialId(...))`. Used by both adjacent-circumflex and VNI sites — single source of truth.

## 5. Tests

### Bucket A — Productive initials (NEW passes)
- `Susaat_PromotesToSuat`: `susaat → suất`
- `Tuaat_PromotesToTuat`: `tuaat → tuất`
- `Xuaat_PromotesToXuat`: `xuaat → xuất`
- `Chuaat_PromotesToChuat`: `chuaat → chuất`
- `Huaat_PromotesToHuat`: `huaat → huất` (HỎI form? verify with `s` for sắc)
- `Luaat_PromotesToLuat`: `luaat → luất` (luật via `j`)
- Plus no-coda variants: `susaa → suấ`, `tuaa → tuấ`

### Bucket B — Non-productive initials (REGRESSION — stays defensive)
- `CuaPlusA_AdjacentRejected` (EXISTING, KEEP): `cuara → củaa`
- `DuaPlusA_Rejected`: `duafaa → dùaa`
- `MuaPlusA_Rejected`: `muafaa → mùaa`
- `BuaPlusA_Rejected`: `buafaa → bùaa`
- `VuaPlusA_Rejected`: `vuafaa → vùaa`

### Bucket C — VNI parity
- `Su1at6 → suất` (already passes — pin)
- `Su1a6 → suấ` (currently broken — fix verifies)
- `Cu1a6 → cúa6` (non-productive — `6` stays literal)
- VNI productive set covers same list as Telex.

### Bucket D — Escape interaction
- `Susaaa_EscapesCircumflex`: `susaaa → súaaa` (3rd `a` cancels promotion)

### Bucket E — ZWJF mode
- `Wuaats` (allowZwjf=true): w treated as qu → productive → `quất` (or `wuất` depending on render — verify)
- `Fuara` (allowZwjf=true): f=ph → non-productive → reject
- `Zuara` (allowZwjf=true): z=gi → non-productive → reject

### Bucket F — InitialConsonantTable parity (NEW infrastructure)
- Every valid Vietnamese onset parses to non-None InitialId
- `IsHardEnglishStart(c0, c1) == HardEnglishOnsetMask(c0) & Bit(c1)` for all (c0, c1) pairs
- Round-trip: states_[N] → ParseInitialId → length matches ParseInitialConsonant.

### Bucket G — Existing regression coverage
- `SuatPlusA_PromotesToSuat` (free-mark) — still green
- `BaPlusA_AdjacentCircumflexes` (baa → bâ) — still green
- `ChieuPlusE_StillCircumflexes` (chieue → chiêu) — still green
- Chaos test 44/44 — still green
- 2010+ existing GTests — still green

## 6. Risks + edge cases

### R1. ParseInitialId boundary
- 2-char vs 1-char detection. Use `ParseInitialId` for canonical decoding, not ad-hoc `states[0]` reads.

### R2. Combined-mode dispatcher
- Telex `aa` (`adjacentCircumflexProposal_`) and VNI `6` (`vniCircumflexProposal_`) both need productive gate.
- Single `IsProductiveCuaCircPromotion()` helper used by both.

### R3. UserDefined custom modifier keys
- User remaps `q → CircumflexA` → sequence `s-u-s-a-q` adjacent. `targetBase` from action (= 'a'), not key char ('q'). Filter uses targetBase + states. Safe.

### R4. `đ` initial
- ParseInitialId returns `Dd`. Not in productive set → rejected. Correct (`đuấ-` not Vietnamese).

### R5. Spec doc update
- Update `docs/vietnamese-phonology-spec-distillate.md` §7: "ANY vowel-modifier after tone → relocate, gated by productive-initial filter for `uâ`/`uê` clusters when pre-state Valid."

### R6. Drift between InitialId enum and ParseInitialConsonant
- Parity test (Bucket F) catches this. Add to CI.

### R7. Future cluster additions
- New row in `kClusterProductive` table. Update spec doc. Tests sanity-check.

### R8. ModernOrtho interaction (VERIFIED no-op)
- `modernOrtho` toggles `kDiphthongClassic` vs `kDiphthongModern` table — affects oa/uy/uê placement (hoà vs hòa). For `uâ` cluster both tables agree (`u[a]=1=FIRST` in both). Productive filter is ortho-agnostic.
- Probe 2026-05-25 verified: susaat/tusaat/xusaat/chusaat/lusaat/husaat (productive) + cufaa/dufaa/mufaa/vufaa (non-productive) produce **identical output** under `modernOrtho=true` and `modernOrtho=false`. No Bucket H needed.
- Memo `speculate_mirror_runtime_2026-05-23` noted Modern unmask 'oo' family — that pattern was in `uo`/`ưo` cluster (different code path, not affected by §7 fix scope).

## 7. Rollout

**Single PR. No feature flag.**

1. Add `InitialConsonantTable.h` (or extend `VietnameseTables.h`).
2. Add tests (Buckets A-F) — verify they describe target behavior.
3. Migrate `IsHardEnglishStart` to use new table.
4. Migrate `ParseInitialConsonant` to wrap `ParseInitialId`.
5. Apply productive gate in `HandleAdjacentCircumflex` Valid branch + `ProcessVniVowelModifier`.
6. Linux GTest full suite — all green.
7. Windows MSVC build verify.
8. Chaos test 44/44 verify.
9. Update spec distillate §7.
10. Commit, push, PR.

**Estimated effort:** 4-6 hours coding + 2 hours tests + 1 hour spec update = ~1 day.

## 8. Followup TODOs (out of scope)

- Lexicon-based replacement of productive filter (Layer 2 from insight session).
- Migrate other `Is*` predicates (IsTriphthong, IsUOEdgeCasePrefix) to tables if/when they grow.
- Step-rendering test framework (insight #2 from prior session — separate scope).
- Track tone-cluster-size-at-fire on CharState (alternative heuristic; not needed after productive-filter ships).

## 9. Open items resolved during brainstorm

- ✅ susaat behavior: ACCEPT (relocate to suất).
- ✅ cuara behavior: REJECT (keep `củaa`, productive-initial gates the difference).
- ✅ Toggle interaction: §7 fix unconditional — `cofaw → còă` path (HornW P3) untouched, "Gõ tự do" semantics preserved.
- ✅ ZWJF: substitution baked into `ParseInitialId`.
- ✅ Refactor scope: bundle with `IsHardEnglishStart` + `ParseInitialConsonant` for shared `InitialId`.

## 10. Design completion checklist

- [x] Bug confirmed via failing test (`Susaat_AdjacentPromotesToSuat`).
- [x] Root cause traced to `HandleAdjacentCircumflex` Valid branch.
- [x] Vietnamese phonology survey (productive set for `uâ`, `uê`).
- [x] Toggle interaction verified (cofaw path unchanged).
- [x] Architecture pattern chosen: table-driven over per-cluster predicates.
- [x] Code smell audit (IsHardEnglishStart + ParseInitialConsonant bundled).
- [x] Edge cases enumerated (ZWJF, UserDefined, `đ`, Combined mode).
- [x] Test buckets defined (A-G).
- [x] Rollout plan (single PR, no flag).
