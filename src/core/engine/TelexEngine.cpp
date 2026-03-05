// NexusKey - Telex Engine Implementation V3 (Optimized Table-Driven)
// SPDX-License-Identifier: GPL-3.0-only
//
// V3 changes: flat constexpr arrays for O(1) Compose(), stack-allocated
// FindToneTarget(), bounded ApplyAutoUO(), pre-reserved buffers.

#include "TelexEngine.h"
#include "EngineHelpers.h"
#include "VietnameseTables.h"

namespace NextKey {
namespace Telex {

namespace {

//=============================================================================
// Telex-specific helpers (tone key mapping etc.)
//=============================================================================

bool IsToneKey(wchar_t c) {
    wchar_t lower = towlower(c);
    return lower == L's' || lower == L'f' || lower == L'r' ||
           lower == L'x' || lower == L'j';
}

Tone KeyToTone(wchar_t c) {
    switch (towlower(c)) {
        case L's': return Tone::Acute;
        case L'f': return Tone::Grave;
        case L'r': return Tone::Hook;
        case L'x': return Tone::Tilde;
        case L'j': return Tone::Dot;
        default: return Tone::None;
    }
}

bool IsVowelChar(wchar_t c) {
    wchar_t lower = towlower(c);
    return lower == L'a' || lower == L'e' || lower == L'i' ||
           lower == L'o' || lower == L'u' || lower == L'y';
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
    rawInput_.push_back(c);

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
            UpdateSpellState();
            return;
        }
    }

    // 0b. Quick consonant: cc→ch, gg→gi, nn→ng
    if (config_.quickConsonant && !states_.empty()) {
        wchar_t lower = towlower(c);
        const CharState& last = states_.back();
        if (!last.IsVowel() && !last.IsD()) {
            wchar_t replacement = 0;
            if (last.base == L'c' && lower == L'c') replacement = L'h';
            else if (last.base == L'g' && lower == L'g') replacement = L'i';
            else if (last.base == L'n' && lower == L'n') replacement = L'g';
            if (replacement) {
                c = iswupper(c) ? towupper(replacement) : replacement;
            }
        }
    }

    // 1a. 'z' key — clear existing tone (if any)
    if (towlower(c) == L'z' && !states_.empty()) {
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

    // 1b. Try tone keys (s, f, r, x, j) — gated by spell check
    if (IsToneKey(c) && !states_.empty()) {
        if (config_.spellCheckEnabled && spellCheckDisabled_) {
            ProcessChar(c);
            UpdateSpellState();
            return;
        }
        if (ProcessTone(c)) {
            ApplyAutoUO();
            UpdateSpellState();
            return;
        }
    }

    // 2. Try modifier keys (w, [], aa, ee, oo, dd)
    // Modifiers are NOT gated by spell check — they can transform invalid
    // sequences into valid ones (e.g., "uo" → "ươ", "ie" → "iê")
    // Brackets and standalone 'w' can insert new chars, so try even on empty states
    if (ProcessModifier(c)) {
        ApplyAutoUO();
        UpdateSpellState();
        return;
    }

    // 2b. Quick end consonant: g→ng, h→nh, k→ch (after vowel)
    if (config_.quickEndConsonant && !states_.empty() && states_.back().IsVowel()) {
        wchar_t lower = towlower(c);
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
    ProcessChar(c);
    ApplyAutoUO();
    UpdateSpellState();
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
        return true;
    }

    // Apply or replace tone
    target.tone = newTone;
    target.toneRawIdx = rawInput_.size() - 1;
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

bool TelexEngine::ProcessModifier(wchar_t c) {
    wchar_t lower = towlower(c);

    // Handle bracket keys: [ → ơ, ] → ư (full Telex only)
    if (config_.inputMethod != InputMethod::SimpleTelex) {
        if (c == L'[') {
            // [ → insert 'ơ' (o with horn)
            CharState s;
            s.base = L'o';
            s.mod = Modifier::Horn;
            s.rawIdx = rawInput_.empty() ? 0 : rawInput_.size() - 1;
            states_.push_back(s);
            return true;
        }
        if (c == L']') {
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
        if (last.IsVowel() && last.base == lower) {
            if (lower == L'a' || lower == L'e' || lower == L'o') {
                // Escape: already has circumflex
                if (last.mod == Modifier::Circumflex) {
                    last.mod = Modifier::None;
                    ProcessChar(c);
                    return true;
                }
                // Apply circumflex - PRESERVE FIRST LETTER CASE
                last.mod = Modifier::Circumflex;
                return true;
            }
        }

        // Free marking: backward scan for circumflex across intervening consonants
        // e.g., "tiéng" + 'e' → find 'é' across 'n','g' → apply circumflex → "tiếng"
        if (!spellCheckDisabled_ && (lower == L'a' || lower == L'e' || lower == L'o')) {
            for (auto it = states_.rbegin(); it != states_.rend(); ++it) {
                if (it->IsVowel() && it->base == lower) {
                    if (it->mod == Modifier::Circumflex) {
                        // Escape: already has circumflex → remove it, add char
                        it->mod = Modifier::None;
                        ProcessChar(c);
                        return true;
                    }
                    if (it->mod == Modifier::None) {
                        it->mod = Modifier::Circumflex;
                        return true;
                    }
                    break;  // Found a matching vowel but can't modify → stop
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
    auto isQUClusterU = [this](size_t i) -> bool {
        return i > 0 && states_[i].base == L'u' && states_[i - 1].base == L'q';
    };

    // PRIORITY ORDER for 'w':
    // P1: "ua" pattern → apply horn to 'u' (mưa, được)
    // P2: "uo" pattern → apply horn to 'o' (uơ → later AutoUO makes ươ)
    // P3: "oa" pattern → apply breve to 'a' (hoặc)
    // P4: Escape - if already have horn/breve, second 'w' clears it
    // P5: Standalone 'u' → horn
    // P6: Standalone 'o' (not in oa/uo) → horn
    // P7: Standalone 'a' → breve

    // Analyze current state (skip QU-cluster 'u' for modification targets)
    bool hasUA = false, hasOA = false, hasUO = false;
    size_t uIdx = SIZE_MAX, oIdx = SIZE_MAX, aIdx = SIZE_MAX;
    size_t hornedIdx = SIZE_MAX, brevedIdx = SIZE_MAX;

    for (size_t i = 0; i < states_.size(); ++i) {
        if (!states_[i].IsVowel()) continue;

        // Skip 'u' in QU cluster — it's a consonant, not modifiable
        if (isQUClusterU(i)) continue;

        wchar_t base = states_[i].base;
        Modifier mod = states_[i].mod;

        if (base == L'u') {
            if (mod == Modifier::Horn) hornedIdx = i;
            else if (mod == Modifier::None) uIdx = i;
        } else if (base == L'o') {
            if (mod == Modifier::Horn) hornedIdx = i;
            else if (mod == Modifier::None || mod == Modifier::Circumflex) oIdx = i;
        } else if (base == L'a') {
            if (mod == Modifier::Breve) brevedIdx = i;
            else if (mod == Modifier::None || mod == Modifier::Circumflex) aIdx = i;
        }
    }

    // Detect vowel patterns (skip QU-cluster 'u')
    for (size_t i = 0; i + 1 < states_.size(); ++i) {
        if (states_[i].IsVowel() && states_[i+1].IsVowel()) {
            if (isQUClusterU(i)) continue;
            wchar_t first = states_[i].base;
            wchar_t second = states_[i+1].base;
            if (first == L'u' && second == L'a') hasUA = true;
            if (first == L'o' && second == L'a') hasOA = true;
            if (first == L'u' && second == L'o') hasUO = true;
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

    // P2: "uo" pattern → horn on 'o' (uơ → later AutoUO makes ươ when next char typed)
    if (hasUO && oIdx != SIZE_MAX) {
        // When replacing circumflex (e.g., "luoow" → ô→ơ), also horn the 'u'
        // since no future char will trigger AutoUO
        bool wasCircumflex = states_[oIdx].mod == Modifier::Circumflex;
        states_[oIdx].mod = Modifier::Horn;
        if (wasCircumflex && uIdx != SIZE_MAX && states_[uIdx].mod == Modifier::None) {
            states_[uIdx].mod = Modifier::Horn;
        }
        RelocateToneToHornVowel();
        return true;
    }

    // P3: "oa" pattern → breve on 'a' (hoặc)
    if (hasOA && aIdx != SIZE_MAX) {
        states_[aIdx].mod = Modifier::Breve;
        return true;
    }

    // P4: Escape - clear existing modifier and add 'w' as literal
    // Must be before standalone applications (P5-P7) so that second 'w'
    // escapes the first modification.
    // Exception: when horned 'o' has an unmodified 'u' companion,
    // skip escape so P5 applies horn to u (e.g., "uoww" → "ươ", "huoww" → "hươ")
    bool canPromoteUO = (hornedIdx != SIZE_MAX &&
        states_[hornedIdx].base == L'o' && uIdx != SIZE_MAX);

    if (hornedIdx != SIZE_MAX && !canPromoteUO) {
        // Special case: P8-synthesized ư (ww → w escape)
        // Only erase when synthetic ư is the last state (immediate ww sequence).
        // If other chars were typed after the synthetic ư (e.g., "window"),
        // it's now part of a word — do regular escape instead.
        if (states_[hornedIdx].synthetic && hornedIdx == states_.size() - 1) {
            states_.erase(states_.begin() + static_cast<ptrdiff_t>(hornedIdx));
            ProcessChar(c);
            return true;
        }
        states_[hornedIdx].mod = Modifier::None;
        // Undo AutoUO: if we cleared horn on 'o' and the preceding char is ư,
        // that ư was auto-applied — clear it too
        if (states_[hornedIdx].base == L'o' && hornedIdx > 0 &&
            states_[hornedIdx - 1].base == L'u' &&
            states_[hornedIdx - 1].mod == Modifier::Horn) {
            states_[hornedIdx - 1].mod = Modifier::None;
        }
        ProcessChar(c);
        return true;
    }
    if (brevedIdx != SIZE_MAX) {
        states_[brevedIdx].mod = Modifier::None;
        ProcessChar(c);
        return true;
    }

    // P5: Standalone 'u' → horn
    if (uIdx != SIZE_MAX) {
        states_[uIdx].mod = Modifier::Horn;
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

    // P7: Standalone 'a' → breve
    if (aIdx != SIZE_MAX && states_[aIdx].mod == Modifier::None) {
        states_[aIdx].mod = Modifier::Breve;
        return true;
    }

    // P8: Full Telex only — standalone 'w' with no modifiable vowel → insert ư
    // In QU cluster, don't insert standalone ư (let 'w' be literal: "quew" → "quew")
    if (config_.inputMethod != InputMethod::SimpleTelex && !IsInQUCluster()) {
        CharState s;
        s.base = L'u';
        s.mod = Modifier::Horn;
        s.isUpper = iswupper(c);
        s.synthetic = true;  // Mark as P8-synthesized (ww escape removes it entirely)
        s.rawIdx = rawInput_.empty() ? 0 : rawInput_.size() - 1;
        states_.push_back(s);
        return true;
    }

    return false;
}

//-----------------------------------------------------------------------------
// D-Modifier Processing (dd → đ)
//-----------------------------------------------------------------------------

bool TelexEngine::ProcessDModifier(wchar_t c) {
    for (auto it = states_.rbegin(); it != states_.rend(); ++it) {
        if (it->IsD()) {
            if (it->mod == Modifier::None) {
                it->mod = Modifier::Breve;
                return true;
            } else if (it->mod == Modifier::Breve) {
                it->mod = Modifier::None;
                ProcessChar(c);
                return true;
            }
        }
    }
    return false;
}

//-----------------------------------------------------------------------------
// QU Cluster Detection
//-----------------------------------------------------------------------------

bool TelexEngine::IsInQUCluster() const {
    if (states_.size() < 2) return false;

    for (size_t i = 0; i + 1 < states_.size(); ++i) {
        if (states_[i].base == L'q' && states_[i+1].base == L'u') {
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

void TelexEngine::ProcessChar(wchar_t c) {
    CharState s;
    s.base = towlower(c);
    s.isUpper = iswupper(c);
    s.mod = Modifier::None;
    s.tone = Tone::None;
    s.rawIdx = rawInput_.empty() ? 0 : rawInput_.size() - 1;
    states_.push_back(s);
}

//-----------------------------------------------------------------------------
// Auto ươ Transformation — O(1) bounded scan
//-----------------------------------------------------------------------------

void TelexEngine::ApplyAutoUO() {
    // Pattern: 'u' (no horn) + 'ơ' (has horn) + [any char]
    // Only need to check the last 3 positions
    if (states_.size() < 3) return;

    // Scan backward, bounded to last 4 positions
    size_t start = (states_.size() > 4) ? states_.size() - 4 : 0;
    for (size_t i = start; i + 2 < states_.size(); ++i) {
        if (states_[i].base == L'u' && states_[i].mod == Modifier::None &&
            states_[i+1].base == L'o' && states_[i+1].mod == Modifier::Horn) {
            // Skip QU cluster: don't auto-horn 'u' when preceded by 'q'
            if (i > 0 && states_[i - 1].base == L'q') continue;
            states_[i].mod = Modifier::Horn;
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
    // Collect vowel positions, skipping "gi" and "qu" consonant clusters
    size_t vowels[8];
    size_t vowelCount = 0;
    for (size_t i = 0; i < states_.size() && vowelCount < 8; ++i) {
        if (!states_[i].IsVowel()) continue;
        // "gi" cluster: g + i + another vowel → 'i' acts as consonant
        if (i > 0 && states_[i].base == L'i' && states_[i - 1].base == L'g'
            && i + 1 < states_.size() && states_[i + 1].IsVowel()) continue;
        // "qu" cluster: q + u → 'u' acts as consonant
        if (i > 0 && states_[i].base == L'u' && states_[i - 1].base == L'q') continue;
        vowels[vowelCount++] = i;
    }

    if (vowelCount == 0) return SIZE_MAX;

    // Priority 1: Horn vowels (last one for ươ)
    size_t lastHornIdx = SIZE_MAX;
    for (size_t k = 0; k < vowelCount; ++k) {
        if (states_[vowels[k]].mod == Modifier::Horn) lastHornIdx = vowels[k];
    }
    if (lastHornIdx != SIZE_MAX) return lastHornIdx;

    // Priority 2: Other modified vowels (â, ê, ô, ă)
    for (size_t k = 0; k < vowelCount; ++k) {
        if (states_[vowels[k]].mod != Modifier::None) return vowels[k];
    }

    // Priority 3: Diphthong/triphthong rules
    if (vowelCount >= 2) {
        size_t lastIdx = vowels[vowelCount - 1];
        size_t prevIdx = vowels[vowelCount - 2];

        if (lastIdx == prevIdx + 1) {
            // Triphthongs (Modern only): tone on MIDDLE vowel
            if (checkTriphthongs && vowelCount >= 3) {
                size_t midIdx = vowels[vowelCount - 2];
                size_t firstVIdx = vowels[vowelCount - 3];
                if (midIdx == firstVIdx + 1 && lastIdx == midIdx + 1) {
                    wchar_t v1 = states_[firstVIdx].base;
                    wchar_t v2 = states_[midIdx].base;
                    wchar_t v3 = states_[lastIdx].base;
                    for (size_t t = 0; t < kTriphthongCount; ++t) {
                        if (kTriphthongs[t].v1 == v1 && kTriphthongs[t].v2 == v2 && kTriphthongs[t].v3 == v3)
                            return midIdx;
                    }
                }
            }

            // Diphthong table lookup
            int fi = DiphthongVowelIndex(states_[prevIdx].base);
            int li = DiphthongVowelIndex(states_[lastIdx].base);
            if (fi >= 0 && li >= 0) {
                uint8_t rule = table[fi][li];
                if (rule == 1) return prevIdx;   // tone on FIRST
                if (rule == 2) return lastIdx;    // tone on SECOND
            }
        }
    }

    // Default: rightmost vowel
    return vowels[vowelCount - 1];
}

//-----------------------------------------------------------------------------
// Composition (State → Unicode) — O(1) flat array lookups
//-----------------------------------------------------------------------------

wchar_t TelexEngine::Compose(const CharState& s) {
    if (s.IsEmpty()) return 0;

    wchar_t ch = s.base;

    // Special: đ
    if (s.IsD() && s.mod == Modifier::Breve) {
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
    std::wstring result;
    result.reserve(states_.size());
    for (const auto& s : states_) {
        wchar_t ch = Compose(s);
        if (ch != 0) result += ch;
    }
    return result;
}

//-----------------------------------------------------------------------------
// Public API
//-----------------------------------------------------------------------------

void TelexEngine::Backspace() {
    if (states_.empty()) return;

    // Trim rawInput_ to the position when this state was created.
    // This correctly handles modifier keys (circumflex, tone) that add to rawInput_
    // without creating new states — backspace removes all associated raw entries.
    size_t rawTarget = states_.back().rawIdx;
    states_.pop_back();
    if (rawInput_.size() > rawTarget) {
        rawInput_.resize(rawTarget);
    }
    UpdateSpellState();
}

std::wstring TelexEngine::Peek() const {
    return ComposeAll();
}

std::wstring TelexEngine::Commit() {
    std::wstring composed = ComposeAll();

    // When tempSpellOff_ is active, user intentionally bypassed spell check —
    // skip auto-restore entirely and return composed text as-is
    if (config_.spellCheckEnabled && config_.autoRestoreEnabled && !tempSpellOff_) {
        bool shouldRestore = spellCheckDisabled_;  // Already known Invalid

        // Also restore ValidPrefix at commit time — incomplete words like "úẻ"
        // (from typing "user") are ValidPrefix during typing (allowing future
        // modifiers) but should auto-restore when the user commits.
        if (!shouldRestore && !states_.empty()) {
            auto result = SpellCheck::Validate(states_.data(), states_.size(), config_.allowZwjf);
            shouldRestore = (result == SpellCheck::Result::ValidPrefix);
        }

        if (shouldRestore) {
            std::wstring raw(rawInput_.begin(), rawInput_.end());
            if (ShouldAutoRestore(raw, composed)) {
                Reset();
                return raw;
            }
        }
    }

    Reset();
    return composed;
}

void TelexEngine::Reset() {
    states_.clear();
    rawInput_.clear();
    state_ = TelexStates::Valid;
    spellCheckDisabled_ = false;
    tempSpellOff_ = false;
}

void TelexEngine::ToggleTempSpellOff() {
    tempSpellOff_ = !tempSpellOff_;
    if (tempSpellOff_) {
        spellCheckDisabled_ = false;
    }
}

size_t TelexEngine::Count() const noexcept {
    return states_.size();
}

//-----------------------------------------------------------------------------
// Spell Check State Update
//-----------------------------------------------------------------------------

void TelexEngine::UpdateSpellState() {
    UpdateSpellCheck(states_.data(), states_.size(), config_, tempSpellOff_, spellCheckDisabled_);
}

}  // namespace Telex
}  // namespace NextKey
