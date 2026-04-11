// NexusKey - Telex Engine Implementation V3 (Optimized Table-Driven)
// Copyright (c) 2024-2026 PhatMT. All rights reserved.
// SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-NexusKey-Commercial
// Dual-licensed: GPL-3.0 for open-source use, commercial license for proprietary use.
// See LICENSE and LICENSE-COMMERCIAL in the project root.
//
// V3 changes: flat constexpr arrays for O(1) Compose(), stack-allocated
// FindToneTarget(), bounded ApplyAutoUO(), pre-reserved buffers.

#include "TelexEngine.h"
#include "EngineHelpers.h"
#include "VietnameseTables.h"
#include <algorithm>

namespace NextKey {
namespace Telex {

namespace {

//=============================================================================
// Telex-specific helpers (tone key mapping etc.)
//=============================================================================

// IsVowelChar is shared — defined in VietnameseTables.h

constexpr bool IsToneKey(wchar_t c) noexcept {
    return c == L's' || c == L'f' || c == L'r' || c == L'x' || c == L'j' ||
           c == L'S' || c == L'F' || c == L'R' || c == L'X' || c == L'J';
}

constexpr Tone KeyToTone(wchar_t c) noexcept {
    switch (c) {
        case L's': case L'S': return Tone::Acute;
        case L'f': case L'F': return Tone::Grave;
        case L'r': case L'R': return Tone::Hook;
        case L'x': case L'X': return Tone::Tilde;
        case L'j': case L'J': return Tone::Dot;
        default: return Tone::None;
    }
}

/// Map Modifier enum to flat array column index (Circumflex=0, Breve=1, Horn=2)
constexpr int ModifierIndex(Modifier mod) {
    switch (mod) {
        case Modifier::Circumflex: return 0;
        case Modifier::Breve:      return 1;
        case Modifier::Horn:       return 2;
        default:                   return -1;
    }
}

/// Map Tone enum to flat array column index (Acute=0 .. Dot=4)
constexpr int ToneIndex(Tone tone) {
    switch (tone) {
        case Tone::Acute: return 0;
        case Tone::Grave: return 1;
        case Tone::Hook:  return 2;
        case Tone::Tilde: return 3;
        case Tone::Dot:   return 4;
        default:          return -1;
    }
}

}  // namespace

//=============================================================================
// TelexEngine Implementation
//=============================================================================

TelexEngine::TelexEngine(const TypingConfig& config) : config_(config) {
    states_.reserve(8);
    rawInput_.reserve(12);
    Reset();
}

//-----------------------------------------------------------------------------
// Main Entry Point
//-----------------------------------------------------------------------------

void TelexEngine::PushChar(wchar_t c) {
    // Guard: cap buffer size to prevent unbounded memory growth.
    // Vietnamese syllables are at most ~8 chars; 64 is generous for any real input.
    // Beyond this, silently drop — the word is clearly not Vietnamese.
    if (rawInput_.size() >= 64) return;

    rawInput_.push_back(c);
    qc_.onlyQC = false;  // Any new char clears the flag

    // 0a. Quick start consonant: f→ph, j→gi, w→qu (only at word start)
    if (config_.quickStartConsonant && states_.empty()) {
        wchar_t lower = towlower(c);
        wchar_t first = 0, second = 0;
        if (lower == L'f') { first = L'p'; second = L'h'; }
        else if (lower == L'j') { first = L'g'; second = L'i'; }
        else if (lower == L'w') { first = L'q'; second = L'u'; }
        if (first) {
            bool upper = iswupper(c);
            ProcessChar(upper ? towupper(first) : first);
            ProcessChar(second);
            quickStartKey_ = c;  // Remember original key for undo
            UpdateSpellState();
            return;
        }
    }

    // 0a-cont. Undo quick start consonant if next char is not a vowel
    // e.g., f→ph, then 't' → undo to "ft" (not "pht")
    if (quickStartKey_ != 0) {
        wchar_t savedKey = quickStartKey_;
        quickStartKey_ = 0;  // Clear before any further processing
        if (!IsVowelChar(c)) {
            states_.clear();
            rawInput_.clear();
            rawInput_.push_back(savedKey);
            ProcessChar(savedKey);
            rawInput_.push_back(c);
            // Fall through to normal processing below
        }
    }

    // 0b. Quick consonant: cc→ch, gg→gi, nn→ng, kk→kh, qq→qu, pp→ph, tt→th
    // Skip if backspace just undid a quick consonant (let user type the literal)
    bool quickEscaped = qc_.escaped;
    qc_.escaped = false;

    // Suppress consecutive re-triggering: after cc→ch, skip quick consonant
    // while the user keeps pressing the same key (e.g., cccc → chcc, not chch)
    wchar_t lower = towlower(c);
    bool isUpper = iswupper(c);
    if (qc_.lastKey != 0) {
        if (lower == qc_.lastKey) {
            quickEscaped = true;  // Reuse escape flag to skip quick consonant
        } else {
            qc_.lastKey = 0;  // Different key, allow future expansions
        }
    }

    if (config_.quickConsonant && !states_.empty() && !quickEscaped) {
        const CharState& last = states_.back();
        if (!last.IsVowel() && !last.IsD()) {
            wchar_t replacement = 0;
            if (last.base == L'c' && lower == L'c') replacement = L'h';
            else if (last.base == L'g' && lower == L'g') replacement = L'i';
            else if (last.base == L'n' && lower == L'n') replacement = L'g';
            else if (last.base == L'k' && lower == L'k') replacement = L'h';
            else if (last.base == L'q' && lower == L'q') replacement = L'u';
            else if (last.base == L'p' && lower == L'p') replacement = L'h';
            else if (last.base == L't' && lower == L't') replacement = L'h';
            if (replacement) {
                if (states_.size() == 1) qc_.onlyQC = true;
                qc_.idx = states_.size();  // Index of the char about to be added
                qc_.lastKey = lower;  // Suppress re-trigger — save ORIGINAL key before update
                c = isUpper ? towupper(replacement) : replacement;
                lower = towlower(c);  // Update for downstream ProcessChar
                isUpper = iswupper(c);
            }
        }
        // uu→ươ: apply horn to existing 'u', then insert 'ơ'
        // Guard: don't expand if last 3 vowels form a triphthong (e.g., khuyu + u)
        else if (last.IsVowel() && last.base == L'u' && last.mod == Modifier::None && lower == L'u') {
            size_t n = states_.size();
            bool triphthong = n >= 3 && states_[n - 3].IsVowel() && states_[n - 2].IsVowel() &&
                              IsTriphthong(states_[n - 3].base, states_[n - 2].base, last.base);
            if (!triphthong) {
                bool upper = iswupper(c);
                states_.back().mod = Modifier::Horn;  // u→ư
                CharState s;
                s.base = L'o';
                s.mod = Modifier::Horn;  // ơ
                s.isUpper = upper;
                s.rawIdx = rawInput_.empty() ? 0 : rawInput_.size() - 1;
                states_.push_back(s);
                qc_.idx = states_.size() - 1;  // Index of the ơ just added
                if (states_.size() == 2) qc_.onlyQC = true;
                qc_.lastKey = lower;  // Suppress re-trigger on consecutive same key
                UpdateSpellState();
                return;
            }
            // Triphthong — fall through to normal processing
        }
    }

    // 1a. 'z' key — clear existing tone (if any)
    if (lower == L'z' && !states_.empty()) {
        if (config_.spellCheckEnabled && spellCheckDisabled_) {
            ProcessChar(c);
            UpdateSpellState();
            return;
        }
        if (ProcessClearTone()) {
            UpdateSpellState();
            return;
        }
    }

    // 1b. Try tone keys (s, f, r, x, j) — gated by spell check + English protection
    if (IsToneKey(c) && !states_.empty()) {
        // All "treat as literal" paths share the same two operations.
        auto asLiteral = [&] { ProcessChar(c, lower, isUpper); UpdateSpellState(); };

        if (config_.spellCheckEnabled && spellCheckDisabled_) {
            // Allow tone escape (rr, ss, ff, etc.) even when spell check disabled:
            // the user is canceling a tone they already applied (e.g. ocrr → ocr).
            size_t t = FindToneTarget();
            if (t == SIZE_MAX || states_[t].tone != KeyToTone(c)) { asLiteral(); return; }
            // Has matching pending tone → fall through to ProcessTone for escape
        }
        // Tone escape: user pressed same tone key twice (e.g., ss) — blocks Vietnamese.
        if (escape_.isEscaped())                                { asLiteral(); return; }
        // English Protection: always active, independent of spell check.
        if (!config_.allowEnglishBypass) {
            if (engProt_.bias == LanguageBias::HardEnglish)         { asLiteral(); return; }
            if (engProt_.bias == LanguageBias::SoftEnglish) {
                if (!UpdateToneInsistence(c, engProt_))              { asLiteral(); return; }
                // User insisted (same key twice) — fall through to apply tone
            }
            // Structural hard-English: V+C+V pattern is impossible in Vietnamese syllables.
            // Catches "manager"/"danger" type words even without spell check.
            if (IsHardEnglishToneContext(states_.data(), states_.size(), c)) {
                engProt_.bias = LanguageBias::HardEnglish;
                asLiteral(); return;
            }
            // Adjacent plain vowel pair with no Vietnamese diphthong rule in either
            // table (e.g., 'ea' in "search", 'ae' in "aerial", 'oy' in "boy").
            if (HasInvalidAdjacentVowelPair(states_.data(), states_.size())) {
                engProt_.bias = LanguageBias::HardEnglish;
                asLiteral(); return;
            }
        }
        // Pre-tone stop-final check (spellCheck path only):
        // Stop finals (c, ch, k, p, t) only accept Acute and Dot tones.
        // Grave/Hook/Tilde on a stop-final syllable is phonologically impossible.
        if (config_.spellCheckEnabled && !spellCheckDisabled_) {
            Tone requested = KeyToTone(c);
            if (requested == Tone::Grave || requested == Tone::Hook ||
                    requested == Tone::Tilde) {
                if (HasStopFinalCoda(states_.data(), states_.size())) {
                    asLiteral(); return;
                }
            }
        }
        if (ProcessTone(c)) {
            if (!escape_.isEscaped()) {
                engProt_.bias = LanguageBias::Vietnamese;  // Tone applied → VN intent
            } else {
                // Tone escaped (ss, ff, etc.) → user is canceling Vietnamese.
                // Reset bias and re-evaluate from scratch.
                RecalcEnglishBias(states_.data(), states_.size(), engProt_);
                CheckZwjfInitialBias(states_.data(), states_.size(), config_, engProt_);
            }
            ApplyAutoUO();
            UpdateSpellState();
            return;
        }
    }

    // 2. Try modifier keys (w, [], aa, ee, oo, dd)
    // Modifiers can transform invalid sequences into valid ones (uo -> ươ).
    // However, if we're clearly in an English word (Tier 1 Hard Protect),
    // skip modifiers and treat them as literal keys (e.g. 'brown' -> 'w' is literal).
    //
    // Pre-check for 'd' modifier: if dd→đ would fire (existing 'd' target) AND there's
    // already a consonant in coda position, adding 'd' forms an invalid coda like "pd".
    // Only fires when FindStrokeDTarget finds a target — doesn't affect plain 'd' in "wind".
    if (lower == L'd' && engProt_.bias != LanguageBias::HardEnglish && states_.size() >= 3) {
        size_t dTarget = FindStrokeDTarget(states_.data(), states_.size());
        if (dTarget != SIZE_MAX && IsStrokeDBlockedByCoda(states_.data(), states_.size(), dTarget)) {
            engProt_.bias = LanguageBias::HardEnglish;
        }
    }
    bool blockModifiers = escape_.isEscaped() ||
        (!config_.allowEnglishBypass && engProt_.bias == LanguageBias::HardEnglish);
    // Allow dd→đ through English Protection when the word matches a spell exclusion.
    if (blockModifiers && lower == L'd') {
        if (WouldStrokeDMatchExclusion(states_.data(), states_.size(),
                config_.spellExclusions,
                [](const CharState& s) { return Compose(s); })) {
            blockModifiers = false;
        }
    }
    if (blockModifiers) {
        // Don't try modifiers — treat as literal
    } else if (config_.spellCheckEnabled && spellCheckDisabled_) {
        // PREVENT modifier application if sequence is already structurally invalid.
        // But allow modifier ESCAPE (ww undoes horn, dd undoes stroke, aa/ee/oo undoes
        // circumflex) — same principle as tone escape bypass above (line 190).
        bool canEscape = false;
        if (lower == L'w') {
            canEscape = HasEscapableModifier(states_.data(), states_.size(), Modifier::Horn) ||
                        HasEscapableModifier(states_.data(), states_.size(), Modifier::Breve);
        } else if (lower == L'd') {
            canEscape = HasEscapableModifier(states_.data(), states_.size(), Modifier::Stroke, true);
            if (!canEscape) {
                canEscape = WouldStrokeDMatchExclusion(states_.data(), states_.size(),
                    config_.spellExclusions,
                    [](const CharState& s) { return Compose(s); });
            }
        } else if (IsVowelChar(c) && !states_.empty()) {
            const CharState& last = states_.back();
            if (last.IsVowel() && last.base == lower && last.mod == Modifier::Circumflex)
                canEscape = true;
        }
        if (canEscape && ProcessModifier(c, lower)) {
            ApplyAutoUO();
            UpdateSpellState();
            return;
        }
    } else if (ProcessModifier(c, lower)) {
        if (engProt_.bias == LanguageBias::HardEnglish || 
            engProt_.bias == LanguageBias::SoftEnglish) {
            engProt_.bias = LanguageBias::Vietnamese;  // Modifier applied → VN intent
        }
        ApplyAutoUO();
        UpdateSpellState();
        return;
    }

    // 2b. Quick end consonant: g→ng, h→nh, k→ch (after vowel)
    if (config_.quickEndConsonant && !states_.empty() && states_.back().IsVowel()) {
        wchar_t first = 0, second = 0;
        if (lower == L'g') { first = L'n'; second = L'g'; }
        else if (lower == L'h') { first = L'n'; second = L'h'; }
        else if (lower == L'k') { first = L'c'; second = L'h'; }
        if (first) {
            ProcessChar(first);
            ProcessChar(second);
            UpdateSpellState();
            return;
        }
    }

    // 3. Regular character
    ProcessChar(c, lower, isUpper);
    RelocateToneToTarget();
    ApplyAutoUO();
    UpdateSpellState();

    // English Protection: re-evaluate bias after adding character
    // (always active — independent of spell check setting)
    CheckEnglishBias(states_.data(), states_.size(), engProt_);
    CheckZwjfInitialBias(states_.data(), states_.size(), config_, engProt_);
}

//-----------------------------------------------------------------------------
// Tone Processing
//-----------------------------------------------------------------------------

bool TelexEngine::ProcessTone(wchar_t c) {
    Tone newTone = KeyToTone(c);
    if (newTone == Tone::None) return false;

    size_t targetIdx = FindToneTarget();
    if (targetIdx == SIZE_MAX) return false;

    CharState& target = states_[targetIdx];

    // Escape: same tone → clear tone and add key as character
    if (target.tone == newTone) {
        target.tone = Tone::None;
        // Remove consumed first-tone entry from rawInput_ so auto-restore
        // gives "user" instead of "usser" for u-s-s-e-r
        if (target.toneRawIdx != SIZE_MAX) {
            EraseConsumedRaw(target.toneRawIdx);
        }
        target.toneRawIdx = SIZE_MAX;
        ProcessChar(c);
        escape_.escape(EscapeKind::Tone);  // Signal caller: user canceled tone
        return true;
    }

    // Apply or replace tone
    target.tone = newTone;
    target.toneRawIdx = rawInput_.size() - 1;
    escape_.clear();
    return true;
}

//-----------------------------------------------------------------------------
// Clear Tone (z key) — remove any existing tone
//-----------------------------------------------------------------------------

bool TelexEngine::ProcessClearTone() {
    size_t targetIdx = FindToneTarget();
    if (targetIdx == SIZE_MAX) return false;

    CharState& target = states_[targetIdx];
    if (target.tone == Tone::None) return false;

    target.tone = Tone::None;
    target.toneRawIdx = SIZE_MAX;
    return true;
}

//-----------------------------------------------------------------------------
// Modifier Processing (W, [], AA, EE, OO, DD) - TABLE-DRIVEN
//-----------------------------------------------------------------------------

bool TelexEngine::ProcessModifier(wchar_t c, wchar_t lower) {

    // Handle bracket keys: [ → ơ, ] → ư (full Telex only)
    if (config_.inputMethod != InputMethod::SimpleTelex) {
        if (c == L'[') {
            // Escape: [[ → undo inserted ơ, produce literal '['
            if (!states_.empty() && rawInput_.size() >= 2 &&
                rawInput_[rawInput_.size() - 2] == L'[') {
                CharState& last = states_.back();
                if (last.base == L'o' && last.mod == Modifier::Horn) {
                    size_t consumedIdx = last.rawIdx;
                    states_.pop_back();
                    EraseConsumedRaw(consumedIdx);
                    ProcessChar(c);
                    escape_.escape(EscapeKind::Horn);
                    return true;
                }
            }
            // [ → insert 'ơ' (o with horn)
            CharState s;
            s.base = L'o';
            s.mod = Modifier::Horn;
            s.rawIdx = rawInput_.empty() ? 0 : rawInput_.size() - 1;
            states_.push_back(s);
            return true;
        }
        if (c == L']') {
            // Escape: ]] → undo inserted ư, produce literal ']'
            if (!states_.empty() && rawInput_.size() >= 2 &&
                rawInput_[rawInput_.size() - 2] == L']') {
                CharState& last = states_.back();
                if (last.base == L'u' && last.mod == Modifier::Horn) {
                    size_t consumedIdx = last.rawIdx;
                    states_.pop_back();
                    EraseConsumedRaw(consumedIdx);
                    ProcessChar(c);
                    escape_.escape(EscapeKind::Horn);
                    return true;
                }
            }
            // ] → insert 'ư' (u with horn)
            CharState s;
            s.base = L'u';
            s.mod = Modifier::Horn;
            s.rawIdx = rawInput_.empty() ? 0 : rawInput_.size() - 1;
            states_.push_back(s);
            return true;
        }
    }

    // Handle 'w' modifier
    if (lower == L'w') {
        return ProcessWModifier(c);
    }

    // Handle double vowel → circumflex (aa→â, ee→ê, oo→ô)
    if (IsVowelChar(c) && !states_.empty()) {
        CharState& last = states_.back();
        bool isCircumflexBase = (lower == L'a' || lower == L'e' || lower == L'o');

        if (last.IsVowel() && last.base == lower && isCircumflexBase) {
            // Guard: don't apply circumflex if last 3 vowels form a triphthong
            // e.g., "ngoeo" + 'o' → consume but don't modify (triphthong complete)
            size_t n = states_.size();
            if (n >= 3 && states_[n - 3].IsVowel() && states_[n - 2].IsVowel() &&
                IsTriphthong(states_[n - 3].base, states_[n - 2].base, last.base)) {
                return true;  // Consume keystroke, no state change
            }
            // Escape: already has circumflex (adjacent case — last char, no coda possible)
            if (last.mod == Modifier::Circumflex) {
                last.mod = Modifier::None;
                ProcessChar(c);
                // With spell check ON, block further modifiers to prevent
                // oo→ô→oo→ô re-trigger cycle ("oo" is ValidPrefix in spell
                // checker since "oong" is valid, so spellCheckDisabled_ alone
                // doesn't catch it — unlike "aa"/"ee" which are Invalid).
                if (config_.spellCheckEnabled) {
                    escape_.escape(EscapeKind::Circumflex);
                }
                return true;
            }
            // Apply circumflex - PRESERVE FIRST LETTER CASE
            last.mod = Modifier::Circumflex;
            return true;
        }

        // Free marking: backward scan for circumflex across intervening chars
        // e.g., "tieng" + 'e' → "tiêng", "cau" + 'a' → "câu", "chieu" + 'e' → "chiêu"
        // Crosses consonants freely; crosses vowels only with spell-check validation (if enabled)
        if (isCircumflexBase) {
            // GUARD: don't apply cross-vowel circumflex if it completes a contiguous triphthong
            // e.g., "ngoe" + 'o' -> forms "o e o" triphthong, so let it be "ngoeo" instead of "ngôe"
            size_t n = states_.size();
            if (n >= 2 && states_[n - 1].IsVowel() && states_[n - 2].IsVowel() &&
                IsTriphthong(states_[n - 2].base, states_[n - 1].base, lower)) {
                return false; 
            }

            bool needsValidation = false;  // True when crossing different vowels
            int consonantsCrossed = 0;     // Count consonants in path (for same-vowel coda check)
            wchar_t singleCoda = 0;        // Base of single consonant crossed (for coda validation)
            for (auto it = states_.rbegin(); it != states_.rend(); ++it) {
                if (it->IsVowel() && it->base != lower) { needsValidation = true; continue; }
                if (!it->IsVowel()) {
                    if (consonantsCrossed == 0) singleCoda = it->base;
                    ++consonantsCrossed;
                }
                if (it->IsVowel() && it->base == lower) {
                    // Reject cross-vowel if: unsupported modifier
                    // Horn undo (ươ→uô) always allowed across vowels
                    if (needsValidation && it->mod != Modifier::Horn &&
                         (it->mod != Modifier::None && it->mod != Modifier::Circumflex)) break;

                    if (it->mod == Modifier::Circumflex) {
                        it->mod = Modifier::None;
                        ProcessChar(c);
                        escape_.escape(EscapeKind::Circumflex);
                        return true;
                    }
                    if (it->mod == Modifier::Breve && lower == L'a') {
                        it->mod = Modifier::Circumflex;
                        RelocateToneToTarget();
                        return true;
                    }
                    if (it->mod == Modifier::Horn && lower == L'o') {
                        auto oIndex = static_cast<size_t>(states_.rend() - it - 1);
                        it->mod = Modifier::Circumflex;
                        UndoHornU(states_.data(), oIndex);
                        RelocateToneToTarget();
                        return true;
                    }
                    if (it->mod == Modifier::None) {
                        // Spell check ON: validate via SpellChecker.
                        // Spell check OFF, cross-vowel + consonant: always reject.
                        // Vietnamese circumflex never crosses diff-vowel + consonant
                        // (dấu mũ ở SAU trong iê/uô → match trước needsValidation;
                        //  dấu mũ ở TRƯỚC trong âu/ây/êu/ôi → không có coda).
                        // Catches "readme" (e←a←dm←e) and "review" (e←v←i←e).
                        // Spell check OFF, same-vowel: reject if single consonant is not
                        // a valid Vietnamese coda (c/m/n/p/t). Catches "release" (e→l→e)
                        // while allowing "hiên" (e→n→e) and "tiêng" (e→ng→e).
                        if (needsValidation) {
                            if (config_.spellCheckEnabled && SpellCheck::Validate(
                                    states_.data(), states_.size(), config_.allowZwjf)
                                    == SpellCheck::Result::Invalid) {
                                it->mod = Modifier::None;
                                break;
                            }
                            if (!config_.spellCheckEnabled && consonantsCrossed >= 1) {
                                engProt_.bias = LanguageBias::HardEnglish;
                                break;
                            }
                        } else if (!config_.spellCheckEnabled && consonantsCrossed == 1) {
                            bool validCoda = (singleCoda == L'c' || singleCoda == L'k' ||
                                              singleCoda == L'm' || singleCoda == L'n' ||
                                              singleCoda == L'p' || singleCoda == L't');
                            if (!validCoda) {
                                engProt_.bias = LanguageBias::HardEnglish;
                                break;
                            }
                        }
                        it->mod = Modifier::Circumflex;
                        RelocateToneToTarget();
                        return true;
                    }
                    break;
                }
            }
        }
    }

    // Handle dd → đ
    if (lower == L'd') {
        return ProcessDModifier(c);
    }

    return false;
}

//-----------------------------------------------------------------------------
// W-Modifier Processing - EXPLICIT PRIORITY ORDER
//-----------------------------------------------------------------------------

bool TelexEngine::ProcessWModifier(wchar_t c) {
    // Simple Telex: 'w' only acts as modifier when preceded by a/o/u vowel
    if (config_.inputMethod == InputMethod::SimpleTelex) {
        bool hasVowelContext = false;
        for (const auto& s : states_) {
            if (s.IsVowel() && (s.base == L'a' || s.base == L'o' || s.base == L'u')) {
                hasVowelContext = true;
                break;
            }
        }
        if (!hasVowelContext) return false;  // Let PushChar add 'w' as literal
    }

    // Check if 'u' at position i is part of QU consonant cluster
    // QU-cluster 'u' should not be treated as a modifiable vowel
    // Only unmodified 'u' qualifies — a horned ư (e.g., P8 synthetic) is not a cluster 'u'.
    auto isQUClusterU = [this](size_t i) -> bool {
        return i > 0 && states_[i].base == L'u' && states_[i].mod == Modifier::None
            && states_[i - 1].base == L'q';
    };

    // PRIORITY ORDER for 'w':
    // P1: "ua" pattern → apply horn to 'u' (mưa, được)
    // P2: "uo" pair horn cycle — default ươ, edge prefixes (h/th/kh) get 3-state
    // P3: "oa" pattern → apply breve to 'a' (hoặc)
    // P4: Escape - if already have horn/breve, second 'w' clears it
    // P5: Standalone 'u' → horn
    // P6: Standalone 'o' (not in oa/uo) → horn
    // P7: Standalone 'a' → breve

    // Analyze current state (skip QU-cluster 'u' for modification targets)
    bool hasUA = false, hasOA = false, hasUO = false, hasUU = false;
    size_t uIdx = SIZE_MAX, firstUIdx = SIZE_MAX, oIdx = SIZE_MAX, aIdx = SIZE_MAX;
    size_t hornedIdx = SIZE_MAX, brevedIdx = SIZE_MAX;

    for (size_t i = 0; i < states_.size(); ++i) {
        if (!states_[i].IsVowel()) continue;

        // Skip 'u' in QU cluster — it's a consonant, not modifiable
        if (isQUClusterU(i)) continue;

        wchar_t base = states_[i].base;
        Modifier mod = states_[i].mod;

        if (base == L'u') {
            if (mod == Modifier::Horn) hornedIdx = i;
            else if (mod == Modifier::None) {
                if (firstUIdx == SIZE_MAX) firstUIdx = i;  // first unmodified u
                uIdx = i;  // last unmodified u
            }
        } else if (base == L'o') {
            if (mod == Modifier::Horn) hornedIdx = i;
            else if (mod == Modifier::None || mod == Modifier::Circumflex) oIdx = i;
        } else if (base == L'a') {
            if (mod == Modifier::Breve) brevedIdx = i;
            else if (mod == Modifier::None || mod == Modifier::Circumflex) aIdx = i;
        }
    }

    // Detect vowel patterns and uo pair indices (skip QU-cluster 'u')
    size_t pairU = SIZE_MAX, pairO = SIZE_MAX;
    for (size_t i = 0; i + 1 < states_.size(); ++i) {
        if (states_[i].IsVowel() && states_[i+1].IsVowel()) {
            if (isQUClusterU(i)) continue;
            wchar_t first = states_[i].base;
            wchar_t second = states_[i+1].base;
            if (first == L'u' && second == L'a') hasUA = true;
            if (first == L'o' && second == L'a') hasOA = true;
            if (first == L'u' && second == L'o') { hasUO = true; pairU = i; pairO = i + 1; }
            if (first == L'u' && second == L'u') hasUU = true;
        }
    }

    // P1: "ua" pattern → horn on 'u' (mưa, được, thưa)
    if (hasUA && uIdx != SIZE_MAX) {
        states_[uIdx].mod = Modifier::Horn;
        if (aIdx != SIZE_MAX && states_[aIdx].mod == Modifier::Circumflex) {
            states_[aIdx].mod = Modifier::None;
        }
        RelocateToneToHornVowel();
        return true;
    }

    // P2: "uo" pair — horn cycle
    // Default: uo → ươ (one press). For h/th/kh prefixes where uơ words exist
    // (huơ, thuở, khuơ): ươ → uơ → uo (three-press cycle).
    // Others: ươ → uo (two-press escape).
    if (hasUO && pairU != SIZE_MAX) {
        Modifier uMod = states_[pairU].mod;
        Modifier oMod = states_[pairO].mod;
        bool isEdge = IsUOEdgeCasePrefix(states_.data(), states_.size(), pairU);

        // Forward: uo/uô → ươ (first press, covers circumflex replacement)
        if (uMod != Modifier::Horn && (oMod == Modifier::None || oMod == Modifier::Circumflex)) {
            states_[pairU].mod = Modifier::Horn;
            states_[pairO].mod = Modifier::Horn;
            RelocateToneToHornVowel();
            return true;
        }

        // Forward: ưo → ươ (u already horned via P5, now complete pair)
        if (uMod == Modifier::Horn && oMod == Modifier::None) {
            states_[pairO].mod = Modifier::Horn;
            RelocateToneToHornVowel();
            return true;
        }

        // Cycle: ươ → uơ (h/th/kh only, second press)
        if (uMod == Modifier::Horn && oMod == Modifier::Horn && isEdge) {
            states_[pairU].mod = Modifier::None;
            RelocateToneToHornVowel();
            return true;
        }

        // Escape: ươ → uo (non h/th/kh, second press)
        if (uMod == Modifier::Horn && oMod == Modifier::Horn && !isEdge) {
            states_[pairU].mod = Modifier::None;
            states_[pairO].mod = Modifier::None;
            ProcessChar(c);
            escape_.escape(EscapeKind::Horn);
            return true;
        }

        // Escape: uơ → uo (h/th/kh third press, or any remaining uơ state)
        if (uMod == Modifier::None && oMod == Modifier::Horn) {
            states_[pairO].mod = Modifier::None;
            ProcessChar(c);
            escape_.escape(EscapeKind::Horn);
            return true;
        }
    }

    // P3: "oa" pattern → breve on 'a' (hoặc)
    if (hasOA && aIdx != SIZE_MAX) {
        states_[aIdx].mod = Modifier::Breve;
        return true;
    }

    // P4: Escape - clear existing modifier and add 'w' as literal
    // Must be before standalone applications (P5-P7) so that second 'w'
    // escapes the first modification.
    if (hornedIdx != SIZE_MAX) {
        // Special case: P8-synthesized ư (ww → w escape)
        // Only erase when synthetic ư is the last state (immediate ww sequence).
        // If other chars were typed after the synthetic ư (e.g., "window"),
        // it's now part of a word — do regular escape instead.
        if (states_[hornedIdx].synthetic && hornedIdx == states_.size() - 1) {
            states_.erase(states_.begin() + static_cast<ptrdiff_t>(hornedIdx));
            ProcessChar(c);
            escape_.escape(EscapeKind::Horn);
            return true;
        }
        UndoHornU(states_.data(), hornedIdx);  // Undo companion 'u' horn BEFORE clearing 'o'
        states_[hornedIdx].mod = Modifier::None;
        ProcessChar(c);
        escape_.escape(EscapeKind::Horn);
        return true;
    }
    if (brevedIdx != SIZE_MAX) {
        states_[brevedIdx].mod = Modifier::None;
        ProcessChar(c);
        escape_.escape(EscapeKind::Breve);
        return true;
    }

    // P5: Standalone 'u' → horn
    // For "uu" pattern, horn goes on FIRST 'u' (lưu, cưu, hưu): the second 'u' is the glide.
    if (uIdx != SIZE_MAX) {
        size_t targetU = (hasUU && firstUIdx != SIZE_MAX) ? firstUIdx : uIdx;
        states_[targetU].mod = Modifier::Horn;
        if (aIdx != SIZE_MAX && states_[aIdx].mod == Modifier::Circumflex) {
            states_[aIdx].mod = Modifier::None;
        }
        RelocateToneToHornVowel();
        return true;
    }

    // P6: Standalone 'o' (not in oa pattern) → horn (replaces circumflex: ô→ơ)
    if (oIdx != SIZE_MAX && !hasOA) {
        states_[oIdx].mod = Modifier::Horn;
        RelocateToneToHornVowel();
        return true;
    }

    // P7: Standalone 'a' → breve (or switch circumflex → breve: â→ă)
    if (aIdx != SIZE_MAX && (states_[aIdx].mod == Modifier::None ||
                              states_[aIdx].mod == Modifier::Circumflex)) {
        states_[aIdx].mod = Modifier::Breve;
        return true;
    }

    // P8: Full Telex only — standalone 'w' with no modifiable vowel → insert ư
    // Only fires when there are no vowels yet in the buffer (onset consonants or empty),
    // OR when the only vowel is 'i' in a potential "gi" consonant cluster (giữ, giữa, giường).
    // After a non-cluster vowel (e.g., "re" + w), 'w' is treated as literal — no Vietnamese
    // word has a vowel followed by standalone ư via P8.
    // In QU cluster, don't insert standalone ư (let 'w' be literal: "quew" → "quew")
    if (config_.inputMethod != InputMethod::SimpleTelex && !IsInQUCluster()) {
        bool hasNonClusterVowel = false;
        for (size_t i = 0; i < states_.size(); ++i) {
            if (!states_[i].IsVowel()) continue;
            // 'i' after 'g' is a potential "gi" cluster consonant — don't count it
            if (states_[i].base == L'i' && i > 0 && states_[i - 1].base == L'g') continue;
            hasNonClusterVowel = true;
            break;
        }
        if (!hasNonClusterVowel) {
            CharState s;
            s.base = L'u';
            s.mod = Modifier::Horn;
            s.isUpper = iswupper(c);
            s.synthetic = true;  // Mark as P8-synthesized (ww escape removes it entirely)
            s.rawIdx = rawInput_.empty() ? 0 : rawInput_.size() - 1;
            states_.push_back(s);
            return true;
        }
    }

    return false;
}

//-----------------------------------------------------------------------------
// D-Modifier Processing (dd → đ)
// Scan logic shared with VniEngine via FindStrokeDTarget (EngineHelpers.h)
//-----------------------------------------------------------------------------

bool TelexEngine::ProcessDModifier(wchar_t c) {
    if (escape_.isEscaped(EscapeKind::Stroke)) return false;
    size_t dIdx = FindStrokeDTarget(states_.data(), states_.size());
    if (dIdx == SIZE_MAX) return false;

    CharState& target = states_[dIdx];
    if (target.mod == Modifier::None) {
        target.mod = Modifier::Stroke;
        return true;
    } else if (target.mod == Modifier::Stroke) {
        target.mod = Modifier::None;
        escape_.escape(EscapeKind::Stroke);
        ProcessChar(c);
        return true;
    }
    return false;
}

//-----------------------------------------------------------------------------
// QU Cluster Detection
//-----------------------------------------------------------------------------

bool TelexEngine::IsInQUCluster() const {
    if (states_.size() < 2) return false;

    for (size_t i = 0; i + 1 < states_.size(); ++i) {
        if (states_[i].base == L'q' && states_[i+1].base == L'u'
            && states_[i+1].mod == Modifier::None) {
            return true;
        }
    }
    return false;
}

//-----------------------------------------------------------------------------
// Remove Consumed Raw Entry (for tone escape auto-restore fix)
//-----------------------------------------------------------------------------

void TelexEngine::EraseConsumedRaw(size_t idx) {
    if (idx >= rawInput_.size()) return;
    rawInput_.erase(rawInput_.begin() + static_cast<ptrdiff_t>(idx));
    // Adjust all indices that reference positions after the erased entry
    for (auto& s : states_) {
        if (s.rawIdx > idx) s.rawIdx--;
        if (s.toneRawIdx != SIZE_MAX && s.toneRawIdx > idx) s.toneRawIdx--;
    }
}

//-----------------------------------------------------------------------------
// Character Processing
//-----------------------------------------------------------------------------

void TelexEngine::ProcessChar(wchar_t /*c*/, wchar_t lower, bool isUpper) {
    CharState s;
    s.base = lower;
    s.isUpper = isUpper;
    s.rawIdx = rawInput_.empty() ? 0 : rawInput_.size() - 1;
    states_.push_back(s);
}

//-----------------------------------------------------------------------------
// Auto ươ Transformation — O(1) bounded scan
//-----------------------------------------------------------------------------

void TelexEngine::ApplyAutoUO() {
    // Requires 3+ chars: both patterns need a char after the uo/ưo pair.
    if (states_.size() < 3) return;

    // Scan backward, bounded to last 4 positions
    size_t start = (states_.size() > 4) ? states_.size() - 4 : 0;
    for (size_t i = start; i + 2 < states_.size(); ++i) {
        // Skip QU cluster: 'u' in "qu" is a consonant glide, not a modifiable vowel
        if (i > 0 && states_[i].base == L'u' && states_[i - 1].base == L'q') continue;

        // Pattern 1: u(no horn) + ơ(has horn) → horn the u to complete ươ
        // Exception: h/th/kh prefix — user may have intentionally chosen uơ (thuở, huơ)
        // Tone relocation not needed: ApplyW P2 already called RelocateToneToHornVowel()
        // when 'w' was typed, so tone is already correctly on ơ.
        if (states_[i].base == L'u' && states_[i].mod == Modifier::None &&
            states_[i+1].base == L'o' && states_[i+1].mod == Modifier::Horn) {
            if (!IsUOEdgeCasePrefix(states_.data(), states_.size(), i)) {
                states_[i].mod = Modifier::Horn;
            }
        }

        // Pattern 2: ư(has horn) + o(no horn) → horn the o to complete ươ
        // Tone relocation needed: ư was horned by ApplyW P5 when only 'u' existed,
        // so any tone placed before 'o' arrived landed on ư and must now move to ơ.
        if (states_[i].base == L'u' && states_[i].mod == Modifier::Horn &&
            states_[i+1].base == L'o' && states_[i+1].mod == Modifier::None) {
            states_[i+1].mod = Modifier::Horn;
            RelocateToneToTarget();
        }
    }
}

//-----------------------------------------------------------------------------
// Tone Relocation (after horn applied)
//-----------------------------------------------------------------------------

void TelexEngine::RelocateToneToHornVowel() {
    size_t hornIdx = SIZE_MAX;
    size_t tonedIdx = SIZE_MAX;

    for (size_t i = 0; i < states_.size(); ++i) {
        if (states_[i].IsVowel()) {
            if (states_[i].mod == Modifier::Horn) hornIdx = i;
            if (states_[i].tone != Tone::None) tonedIdx = i;
        }
    }

    if (hornIdx != SIZE_MAX && tonedIdx != SIZE_MAX && hornIdx != tonedIdx) {
        // Allow relocation from unmodified vowels and from horn vowels
        // (handles "ươ" diphthong: tone moves from ư to ơ)
        if (states_[tonedIdx].mod == Modifier::None || states_[tonedIdx].mod == Modifier::Horn) {
            states_[hornIdx].tone = states_[tonedIdx].tone;
            states_[tonedIdx].tone = Tone::None;
        }
    }
}

//-----------------------------------------------------------------------------
// Tone Relocation (after any modifier changes priority)
//-----------------------------------------------------------------------------

void TelexEngine::RelocateToneToTarget() {
    // Find where the tone currently is
    size_t tonedIdx = SIZE_MAX;
    for (size_t i = 0; i < states_.size(); ++i) {
        if (states_[i].IsVowel() && states_[i].tone != Tone::None) {
            tonedIdx = i;
            break;
        }
    }
    if (tonedIdx == SIZE_MAX) return;

    // Find where the tone should be now (modifier may have changed priority)
    size_t targetIdx = FindToneTarget();
    if (targetIdx == SIZE_MAX || targetIdx == tonedIdx) return;

    if (IsToneRelocBlockedByP4(states_.data(), states_.size(), targetIdx, config_.modernOrtho))
        return;

    states_[targetIdx].tone = states_[tonedIdx].tone;
    states_[targetIdx].toneRawIdx = states_[tonedIdx].toneRawIdx;
    states_[tonedIdx].tone = Tone::None;
    states_[tonedIdx].toneRawIdx = SIZE_MAX;
}

//-----------------------------------------------------------------------------
// Tone Target Finding — stack-allocated, no heap alloc
//-----------------------------------------------------------------------------

size_t TelexEngine::FindToneTarget() const {
    return config_.modernOrtho ? FindToneTargetModern() : FindToneTargetClassic();
}

size_t TelexEngine::FindToneTargetClassic() const {
    return FindToneTargetImpl(kDiphthongClassic, false);
}

size_t TelexEngine::FindToneTargetModern() const {
    return FindToneTargetImpl(kDiphthongModern, true);
}

size_t TelexEngine::FindToneTargetImpl(const uint8_t table[6][6], bool checkTriphthongs) const {
    return NextKey::FindToneTargetImpl(states_.data(), states_.size(), table, checkTriphthongs);
}

//-----------------------------------------------------------------------------
// Composition (State → Unicode) — O(1) flat array lookups
//-----------------------------------------------------------------------------

wchar_t TelexEngine::Compose(const CharState& s) {
    if (s.IsEmpty()) return 0;

    wchar_t ch = s.base;

    // Special: đ
    if (s.IsD() && s.mod == Modifier::Stroke) {
        return s.isUpper ? L'Đ' : L'đ';
    }

    // Step 1: Apply modifier — O(1) array lookup
    if (s.mod != Modifier::None && s.IsVowel()) {
        int bi = VowelBaseIndex(s.base);
        int mi = ModifierIndex(s.mod);
        if (bi >= 0 && mi >= 0) {
            wchar_t modified = kModifiedVowel[bi][mi];
            if (modified) ch = modified;
        }
    }

    // Step 2: Apply tone — O(1) array lookup
    if (s.tone != Tone::None) {
        int ti = ToneBaseIndex(ch);
        int si = ToneIndex(s.tone);
        if (ti >= 0 && si >= 0) {
            ch = kTonedVowel[ti][si];
        }
    }

    // Step 3: Apply case
    if (s.isUpper) {
        ch = ToUpperVietnamese(ch);
    }

    return ch;
}

std::wstring TelexEngine::ComposeAll() const {
    composeBuf_.clear();
    for (const auto& s : states_) {
        wchar_t ch = Compose(s);
        if (ch != 0) composeBuf_ += ch;
    }
    return composeBuf_;
}

//-----------------------------------------------------------------------------
// Public API
//-----------------------------------------------------------------------------

void TelexEngine::Backspace() {
    if (states_.empty()) return;
    escape_.clear();  // Allow đ re-trigger + Vietnamese re-trigger after user edits

    // Undo quick start consonant: ph→f, gi→j, qu→w (collapse both chars to original)
    if (quickStartKey_ != 0 && states_.size() == 2) {
        states_.clear();
        rawInput_.clear();
        rawInput_.push_back(quickStartKey_);
        ProcessChar(quickStartKey_);
        quickStartKey_ = 0;
        UpdateSpellState();
        return;
    }
    quickStartKey_ = 0;

    // Undo quick consonant expansion — restore original key in-place.
    // e.g., "aph" + BS → "app" (not "ap"), "rieng" + BS → "rienn".
    // The triggering key is still in rawInput_; re-add it as an escaped literal
    // so the user sees the full original sequence without having to retype.
    if (qc_.hasActive() && states_.size() - 1 == qc_.idx) {
        // For uu→ươ: undo the horn on the preceding 'u' before popping ơ
        UndoHornU(states_.data(), states_.size() - 1);

        wchar_t originalKey = qc_.lastKey;
        qc_.markEscaped();  // escaped=true, clearActive (idx+lastKey)

        states_.pop_back();  // Remove the converted char (h in ph, g in ng, i in gi, ơ in ươ)
        // rawInput_ already contains the original triggering key — don't trim it.
        // Re-add it as a literal char; qc_.escaped prevents re-conversion.
        if (originalKey != 0) {
            ProcessChar(originalKey);
        }

        UpdateSpellState();
        RecalcEnglishBias(states_.data(), states_.size(), engProt_);
        CheckZwjfInitialBias(states_.data(), states_.size(), config_, engProt_);
        return;
    }
    qc_.clearActive();

    // Trim rawInput_ to the position when this state was created.
    // This correctly handles modifier keys (circumflex, tone) that add to rawInput_
    // without creating new states — backspace removes all associated raw entries.
    size_t rawTarget = states_.back().rawIdx;
    states_.pop_back();
    if (rawInput_.size() > rawTarget) {
        rawInput_.resize(rawTarget);
    }
    UpdateSpellState();

    // English Protection: recalculate bias after backspace
    RecalcEnglishBias(states_.data(), states_.size(), engProt_);
    CheckZwjfInitialBias(states_.data(), states_.size(), config_, engProt_);
}

std::wstring TelexEngine::Peek() const {
    return ComposeAll();
}

std::wstring TelexEngine::Commit() {
    std::wstring composed = ComposeAll();

    // Single quick-start consonant alone (f->ph, j->gi, w->qu) — always restore
    // This allows "j " -> "j " instead of "gi ", since "gi" is a valid word and wouldn't auto-restore naturally.
    if (quickStartKey_ != 0 && rawInput_.size() == 1) {
        std::wstring raw(rawInput_.begin(), rawInput_.end());
        if (ShouldAutoRestore(raw, composed)) {
            Reset();
            return raw;
        }
    }

    // Quick consonant alone (gg, uu) — always restore regardless of spell check setting
    if (qc_.onlyQC) {
        std::wstring raw(rawInput_.begin(), rawInput_.end());
        if (ShouldAutoRestore(raw, composed)) {
            Reset();
            return raw;
        }
    }

    // Guard: skip auto-restore when any of these are true:
    //  - spell check / auto-restore not enabled
    //  - an active quick consonant (nn→ng, cc→ch, etc.) was applied — user did
    //    this intentionally; restoring would undo the conversion they wanted.
    //    (qc_.hasActive() when quick consonant is active)
    bool skipAutoRestore = !config_.spellCheckEnabled
                        || !config_.autoRestoreEnabled
                        || qc_.hasActive();
    if (!skipAutoRestore) {
        bool shouldRestore = spellCheckDisabled_;  // Already known Invalid

        // Also restore ValidPrefix at commit time — incomplete words like "úẻ"
        // (from typing "user") are ValidPrefix during typing (allowing future
        // modifiers) but should auto-restore when the user commits.
        if (!shouldRestore && !states_.empty()) {
            auto result = SpellCheck::Validate(states_.data(), states_.size(), config_.allowZwjf);
            shouldRestore = (result == SpellCheck::Result::ValidPrefix);
        }

        if (shouldRestore) {
            // Spell exclusion list is the primary authority on what to keep.
            // When empty, fall back to HasIntentionalStrokeD heuristic
            // (protects abbreviations like đt, đh while restoring đwa, ăndd).
            bool excluded = IsSpellExcluded(states_.data(), states_.size(),
                                            config_.spellExclusions,
                                            [](const CharState& s) { return Compose(s); });
            bool keepComposed = excluded ||
                                (config_.spellExclusions.empty() &&
                                 HasIntentionalStrokeD(rawInput_, composed));
            if (!keepComposed) {
                std::wstring raw(rawInput_.begin(), rawInput_.end());
                if (ShouldAutoRestore(raw, composed)) {
                    Reset();
                    return raw;
                }
            }
        }
    }

    Reset();
    return composed;
}

void TelexEngine::Reset() {
    states_.clear();
    rawInput_.clear();
    spellCheckDisabled_ = false;
    qc_.Reset();
    escape_.clear();
    quickStartKey_ = 0;
    engProt_.Reset();
}



//-----------------------------------------------------------------------------
// Spell Check State Update
//-----------------------------------------------------------------------------

void TelexEngine::UpdateSpellState() {
    UpdateSpellCheck(states_.data(), states_.size(), config_, spellCheckDisabled_,
                     [](const CharState& s) { return Compose(s); });
}

}  // namespace Telex
}  // namespace NextKey
