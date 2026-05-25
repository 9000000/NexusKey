// src/core/engine/rule/ModifierProposal.h
//
// Abstract base for per-modifier sub-handler proposals (W8.1+).
//
// A ModifierProposal is the documented home for a single modifier action's
// dispatch — it carries NON-AUTHORITATIVE METADATA describing the modifier's
// relocation intent + a tryApply() entry point that delegates to the
// engine body (via IModifierSubExecutor).
//
// Design notes (W8.1 plan v3, post-round-3 review):
//   - Metadata methods (relocationKind) are DOCUMENTATION ONLY — they do NOT
//     enforce behaviour. The engine body is source of truth for the actual
//     speculation/apply/relocate sequence. Tests pin metadata-to-body parity;
//     if either drifts, tests catch it.
//   - The ad09f15 invariant "speculate path mirrors apply path" still lives
//     in HandleAdjacentCircumflex body's per-pre-state branching plus the
//     WouldBeValidSyllable comment block. This abstraction does NOT lift
//     the invariant into a compile-time contract.
//   - The cumulative win across W8.1-W8.5 is organisational:
//       `grep RelocationKind:: src/core/engine/rule/` shows the modifier
//       policy map at-a-glance, and parity tests across Telex/VNI proposals
//       surface drift via class-level diff.
#pragma once

#include <cstdint>

#include "core/engine/TypingAction.h"

namespace NextKey::EngineRule {

// Documents how each modifier interacts with the existing tone after applying
// its base change. NOT enforced at the class level — the engine body's
// actual RelocateToneTo* calls are the source of truth. Tests pin the
// mapping so drift between metadata and body surfaces in CI.
enum class RelocationKind : uint8_t {
    // Engine body never calls any RelocateToneTo* after applying the modifier.
    // Tone stays on its current vowel. Example: Breve P7 (standalone-a → ă)
    // in HandleHornW.
    None,

    // Engine body unconditionally calls RelocateToneToTarget() after applying
    // the modifier. Examples:
    //   - Free-marking circumflex (HandleAdjacentCircumflex free-marking
    //     branch, TypingEngine.cpp ~L1057).
    //   - Adjacent circumflex when pre-state is ValidPrefix (post-c6369dd
    //     gated by needsRelocate, TypingEngine.cpp ~L962).
    //   - VNI vowel modifier (ProcessVniVowelModifier, TypingEngine.cpp
    //     ~L1928).
    TargetTone,

    // Engine body calls RelocateToneToHornVowel() — a DIFFERENT function
    // from RelocateToneToTarget (separate target-finding logic for the
    // horn cluster). Example: Horn P5/P6 in HandleHornW (W8.2 will land
    // this as HornModifierProposal).
    HornVowel,

    // Engine body decides per pre-state, spanning multiple kinds above.
    // Used today only by AdjacentCircumflexProposal because its body has
    // two branches with different relocation behaviour:
    //   - Adjacent branch (TypingEngine.cpp ~L905-963): TargetTone IFF
    //     pre-state == ValidPrefix (per c6369dd needsRelocate gate); None
    //     IFF pre-state == Valid or Invalid (mod-only typo guard).
    //   - Free-marking branch (~L984-1057): always TargetTone after apply.
    // Auditors checking "speculate mirrors apply" should read both
    // branches.
    Conditional,
};

class ModifierProposal {
public:
    virtual ~ModifierProposal() = default;

    // Declarative metadata — DOCUMENTATION + test-pinning only. Not enforced.
    [[nodiscard]] virtual RelocationKind relocationKind() const noexcept = 0;

    // The only behaviour-bearing method. Delegates to the engine body via
    // IModifierSubExecutor; the body remains the source of truth for the
    // actual speculation/apply/relocate sequence.
    [[nodiscard]] virtual bool tryApply(TypingAction action, wchar_t keyChar) = 0;
};

}  // namespace NextKey::EngineRule
