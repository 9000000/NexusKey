// NexusKey - Telex Engine Implementation V3 (Optimized Table-Driven)
// SPDX-License-Identifier: GPL-3.0-only
//
// V3 changes: flat constexpr arrays for O(1) Compose(), stack-allocated
// FindToneTarget(), bounded ApplyAutoUO(), pre-reserved buffers.

#include "TelexEngine.h"
#include "VietnameseTables.h"
#include <algorithm>

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

    // 1. Try tone keys (s, f, r, x, j)
    // Note: allowZwjf is for spell-check validation only (accept "ja" as valid).
    // Tone/modifier behavior of z/w/j/f is always standard Telex.
    if (IsToneKey(c) && !states_.empty()) {
        if (ProcessTone(c)) {
            ApplyAutoUO();
            return;
        }
    }

    // 2. Try modifier keys (w, aa, ee, oo, dd)
    if (!states_.empty()) {
        if (ProcessModifier(c)) {
            ApplyAutoUO();
            return;
        }
    }

    // 3. Regular character
    ProcessChar(c);
    ApplyAutoUO();
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
        ProcessChar(c);
        return true;
    }

    // Apply or replace tone
    target.tone = newTone;
    return true;
}

//-----------------------------------------------------------------------------
// Modifier Processing (W, AA, EE, OO, DD) - TABLE-DRIVEN
//-----------------------------------------------------------------------------

bool TelexEngine::ProcessModifier(wchar_t c) {
    wchar_t lower = towlower(c);

    // Handle 'w' modifier
    if (lower == L'w') {
        return ProcessWModifier(c);
    }

    // Handle double vowel → circumflex (aa→â, ee→ê, oo→ô)
    if (IsVowelChar(c)) {
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
    // Check if 'w' should be ignored (QU cluster)
    if (IsInQUCluster()) {
        return false;  // Let 'w' be added as regular character
    }

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

    // PRIORITY ORDER for 'w':
    // P1: "ua" pattern → apply horn to 'u' (mưa, được)
    // P2: "uo" pattern → apply horn to 'o' (uơ → later AutoUO makes ươ)
    // P3: "oa" pattern → apply breve to 'a' (hoặc)
    // P4: Standalone 'u' → horn
    // P5: Standalone 'o' (not in oa/uo) → horn
    // P6: Standalone 'a' → breve
    // P7: Escape - if already have horn/breve, second 'w' clears it

    // Analyze current state
    bool hasUA = false, hasOA = false, hasUO = false;
    size_t uIdx = SIZE_MAX, oIdx = SIZE_MAX, aIdx = SIZE_MAX;
    size_t hornedIdx = SIZE_MAX, brevedIdx = SIZE_MAX;

    for (size_t i = 0; i < states_.size(); ++i) {
        if (!states_[i].IsVowel()) continue;

        wchar_t base = states_[i].base;
        Modifier mod = states_[i].mod;

        if (base == L'u') {
            if (mod == Modifier::Horn) hornedIdx = i;
            else if (mod == Modifier::None) uIdx = i;
        } else if (base == L'o') {
            if (mod == Modifier::Horn) hornedIdx = i;
            else if (mod == Modifier::None) oIdx = i;
        } else if (base == L'a') {
            if (mod == Modifier::Breve) brevedIdx = i;
            else if (mod == Modifier::None || mod == Modifier::Circumflex) aIdx = i;
        }
    }

    // Detect vowel patterns
    for (size_t i = 0; i + 1 < states_.size(); ++i) {
        if (states_[i].IsVowel() && states_[i+1].IsVowel()) {
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

    // P2: "uo" pattern → horn on 'o' (uơ, will become ươ via AutoUO)
    if (hasUO && oIdx != SIZE_MAX) {
        states_[oIdx].mod = Modifier::Horn;
        RelocateToneToHornVowel();
        return true;
    }

    // P3: "oa" pattern → breve on 'a' (hoặc)
    if (hasOA && aIdx != SIZE_MAX) {
        states_[aIdx].mod = Modifier::Breve;
        return true;
    }

    // P4: Standalone 'u' → horn
    if (uIdx != SIZE_MAX) {
        states_[uIdx].mod = Modifier::Horn;
        if (aIdx != SIZE_MAX && states_[aIdx].mod == Modifier::Circumflex) {
            states_[aIdx].mod = Modifier::None;
        }
        RelocateToneToHornVowel();
        return true;
    }

    // P5: Standalone 'o' (not in oa pattern) → horn
    if (oIdx != SIZE_MAX && !hasOA) {
        states_[oIdx].mod = Modifier::Horn;
        RelocateToneToHornVowel();
        return true;
    }

    // P6: Standalone 'a' → breve (BUT only if no horn vowel exists)
    if (aIdx != SIZE_MAX && states_[aIdx].mod == Modifier::None && hornedIdx == SIZE_MAX) {
        states_[aIdx].mod = Modifier::Breve;
        return true;
    }

    // P7: Escape - clear existing modifier and add 'w' as literal
    if (hornedIdx != SIZE_MAX) {
        states_[hornedIdx].mod = Modifier::None;
        ProcessChar(c);
        return true;
    }
    if (brevedIdx != SIZE_MAX) {
        states_[brevedIdx].mod = Modifier::None;
        ProcessChar(c);
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
// Character Processing
//-----------------------------------------------------------------------------

void TelexEngine::ProcessChar(wchar_t c) {
    CharState s;
    s.base = towlower(c);
    s.isUpper = iswupper(c);
    s.mod = Modifier::None;
    s.tone = Tone::None;
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
        if (states_[tonedIdx].mod == Modifier::None) {
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
    size_t vowels[8];
    size_t vowelCount = 0;
    for (size_t i = 0; i < states_.size() && vowelCount < 8; ++i) {
        if (states_[i].IsVowel()) {
            vowels[vowelCount++] = i;
        }
    }

    if (vowelCount == 0) return SIZE_MAX;

    // Priority 1: Horn vowels (last one for ươ)
    size_t lastHornIdx = SIZE_MAX;
    for (size_t k = 0; k < vowelCount; ++k) {
        if (states_[vowels[k]].mod == Modifier::Horn) {
            lastHornIdx = vowels[k];
        }
    }
    if (lastHornIdx != SIZE_MAX) return lastHornIdx;

    // Priority 2: Modified vowels (â, ê, ô, ă)
    for (size_t k = 0; k < vowelCount; ++k) {
        if (states_[vowels[k]].mod != Modifier::None) {
            return vowels[k];
        }
    }

    // Priority 3: Diphthong rules
    if (vowelCount >= 2) {
        size_t lastIdx = vowels[vowelCount - 1];
        size_t prevIdx = vowels[vowelCount - 2];

        if (lastIdx == prevIdx + 1) {
            wchar_t first = states_[prevIdx].base;
            wchar_t last = states_[lastIdx].base;

            // Falling diphthongs: tone on FIRST
            if (first == L'a' && (last == L'i' || last == L'o' || last == L'u' || last == L'y')) return prevIdx;
            if (first == L'e' && (last == L'i' || last == L'o' || last == L'u')) return prevIdx;
            if (first == L'o' && (last == L'i' || last == L'u')) return prevIdx;
            if ((first == L'u' || first == L'i') && (last == L'i' || last == L'u')) return prevIdx;
            if (first == L'u' && (last == L'a' || last == L'e')) return prevIdx;

            // Classic: oa, oe → tone on FIRST (old-style: hòa, xòe)
            if (first == L'o' && (last == L'a' || last == L'e')) return prevIdx;

            // Rising diphthongs: tone on SECOND
            if (first == L'u' && last == L'y') return lastIdx;
        }
    }

    // Default: rightmost vowel
    return vowels[vowelCount - 1];
}

size_t TelexEngine::FindToneTargetModern() const {
    size_t vowels[8];
    size_t vowelCount = 0;
    for (size_t i = 0; i < states_.size() && vowelCount < 8; ++i) {
        if (states_[i].IsVowel()) {
            vowels[vowelCount++] = i;
        }
    }

    if (vowelCount == 0) return SIZE_MAX;

    // Priority 1: Horn vowels (last one for ươ)
    size_t lastHornIdx = SIZE_MAX;
    for (size_t k = 0; k < vowelCount; ++k) {
        if (states_[vowels[k]].mod == Modifier::Horn) {
            lastHornIdx = vowels[k];
        }
    }
    if (lastHornIdx != SIZE_MAX) return lastHornIdx;

    // Priority 2: Modified vowels (â, ê, ô, ă)
    for (size_t k = 0; k < vowelCount; ++k) {
        if (states_[vowels[k]].mod != Modifier::None) {
            return vowels[k];
        }
    }

    // Priority 3: Diphthong rules (MODERN placement)
    if (vowelCount >= 2) {
        size_t lastIdx = vowels[vowelCount - 1];
        size_t prevIdx = vowels[vowelCount - 2];

        if (lastIdx == prevIdx + 1) {
            wchar_t first = states_[prevIdx].base;
            wchar_t last = states_[lastIdx].base;

            // Triphthongs: check 3-vowel patterns (tone on MIDDLE)
            if (vowelCount >= 3) {
                size_t midIdx = vowels[vowelCount - 2];
                size_t firstIdx = vowels[vowelCount - 3];
                if (midIdx == firstIdx + 1 && lastIdx == midIdx + 1) {
                    wchar_t v1 = states_[firstIdx].base;
                    wchar_t v2 = states_[midIdx].base;
                    wchar_t v3 = states_[lastIdx].base;
                    // oai, oeo, uya, uyu
                    if ((v1 == L'o' && v2 == L'a' && v3 == L'i') ||
                        (v1 == L'o' && v2 == L'e' && v3 == L'o') ||
                        (v1 == L'u' && v2 == L'y' && v3 == L'a') ||
                        (v1 == L'u' && v2 == L'y' && v3 == L'u')) {
                        return midIdx;
                    }
                }
            }

            // Falling diphthongs: tone on FIRST
            if (first == L'a' && (last == L'i' || last == L'o' || last == L'u' || last == L'y')) return prevIdx;
            if (first == L'e' && (last == L'i' || last == L'o' || last == L'u')) return prevIdx;
            if (first == L'o' && (last == L'i' || last == L'u')) return prevIdx;
            if ((first == L'u' || first == L'i') && (last == L'i' || last == L'u')) return prevIdx;

            // MODERN: "ua", "ue" → tone on SECOND (differs from classic)
            if (first == L'u' && (last == L'a' || last == L'e')) return lastIdx;

            // Rising diphthongs: tone on SECOND (same as classic)
            if (first == L'o' && (last == L'a' || last == L'e')) return lastIdx;
            if (first == L'u' && last == L'y') return lastIdx;

            // "uo" → tone on SECOND
            if (first == L'u' && last == L'o') return lastIdx;
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

    CharState& last = states_.back();
    if (last.tone != Tone::None) {
        last.tone = Tone::None;
    } else if (last.mod != Modifier::None) {
        last.mod = Modifier::None;
    } else {
        states_.pop_back();
    }

    if (!rawInput_.empty()) rawInput_.pop_back();
}

std::wstring TelexEngine::Peek() const {
    return ComposeAll();
}

std::wstring TelexEngine::Commit() {
    std::wstring result = ComposeAll();
    Reset();
    return result;
}

void TelexEngine::Reset() {
    states_.clear();
    rawInput_.clear();
    state_ = TelexStates::Valid;
}

size_t TelexEngine::Count() const noexcept {
    return states_.size();
}

}  // namespace Telex
}  // namespace NextKey
