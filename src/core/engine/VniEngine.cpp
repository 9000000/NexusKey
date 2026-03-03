// NexusKey - VNI Input Method Engine Implementation (Optimized)
// SPDX-License-Identifier: GPL-3.0-only
//
// Uses shared VietnameseTables.h for O(1) flat array composition lookups.

#include "VniEngine.h"
#include "VietnameseTables.h"
#include <algorithm>

namespace NextKey {
namespace Vni {

//=============================================================================
// Helper functions
//=============================================================================

namespace {

bool IsVowelChar(wchar_t c) {
    c = towlower(c);
    return c == L'a' || c == L'e' || c == L'i' || c == L'o' || c == L'u' || c == L'y';
}

bool IsToneKey(wchar_t c) {
    return c >= L'1' && c <= L'5';
}

Tone KeyToTone(wchar_t c) {
    switch (c) {
        case L'1': return Tone::Acute;
        case L'2': return Tone::Grave;
        case L'3': return Tone::Hook;
        case L'4': return Tone::Tilde;
        case L'5': return Tone::Dot;
        default: return Tone::None;
    }
}

bool IsModifierKey(wchar_t c) {
    return c == L'6' || c == L'7' || c == L'8' || c == L'9';
}

/// Map VNI Modifier enum to shared table column index
constexpr int ModifierIndex(Modifier mod) {
    switch (mod) {
        case Modifier::Circumflex: return 0;
        case Modifier::Breve:      return 1;
        case Modifier::Horn:       return 2;
        default:                   return -1;
    }
}

/// Map VNI Tone enum to shared table column index
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
// CharState Implementation
//=============================================================================

bool CharState::IsVowel() const noexcept {
    return IsVowelChar(base);
}

//=============================================================================
// VniEngine Implementation
//=============================================================================

VniEngine::VniEngine(const TypingConfig& config) : config_(config) {
    states_.reserve(8);
    rawInput_.reserve(12);
}

void VniEngine::PushChar(wchar_t c) {
    rawInput_ += c;

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

    // 1. Try tone keys (1-5) — gated by spell check
    if (IsToneKey(c)) {
        if (config_.spellCheckEnabled && spellCheckDisabled_ && !config_.freeMarking) {
            ProcessChar(c);
            UpdateSpellState();
            return;
        }
        if (ProcessTone(c)) {
            UpdateSpellState();
            return;
        }
    }

    // 2. Try modifier keys (6-9)
    // Modifiers are NOT gated by spell check — they can transform invalid
    // sequences into valid ones (e.g., vowel modifiers create valid nuclei)
    if (IsModifierKey(c)) {
        if (ProcessModifier(c)) {
            UpdateSpellState();
            return;
        }
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

    // Regular character
    ProcessChar(c);
    UpdateSpellState();
}

void VniEngine::Backspace() {
    if (!states_.empty()) {
        states_.pop_back();
    }
    if (!rawInput_.empty()) {
        rawInput_.pop_back();
    }
    UpdateSpellState();
}

std::wstring VniEngine::Peek() const {
    std::wstring result;
    result.reserve(states_.size());
    for (const auto& s : states_) {
        result += ComposeChar(s);
    }
    return result;
}

std::wstring VniEngine::Commit() {
    std::wstring composed = Peek();

    // When tempSpellOff_ is active, user intentionally bypassed spell check —
    // skip auto-restore entirely and return composed text as-is
    if (config_.spellCheckEnabled && config_.autoRestoreEnabled &&
        spellCheckDisabled_ && !tempSpellOff_) {
        std::wstring raw = rawInput_;
        if (raw != composed) {
            bool hasDiacritics = false;
            for (wchar_t ch : composed) {
                if (ch > 0x7F) { hasDiacritics = true; break; }
            }
            // Restore raw when:
            // 1. Composed has diacritics (e.g., "gôgle" → "google")
            // 2. Same length but different content (escaped modifiers changed letters)
            // Skip when raw is longer than composed (escape duplicates, e.g., "musst" → keep "must")
            if (hasDiacritics || raw.length() <= composed.length()) {
                Reset();
                return raw;
            }
        }
    }

    Reset();
    return composed;
}

void VniEngine::Reset() {
    states_.clear();
    rawInput_.clear();
    spellCheckDisabled_ = false;
    tempSpellOff_ = false;
}

void VniEngine::ToggleTempSpellOff() {
    tempSpellOff_ = !tempSpellOff_;
    if (tempSpellOff_) {
        spellCheckDisabled_ = false;
    }
}

size_t VniEngine::Count() const noexcept {
    return states_.size();
}

//-----------------------------------------------------------------------------
// Modifier Processing (6, 7, 8, 9)
//-----------------------------------------------------------------------------

bool VniEngine::ProcessModifier(wchar_t c) {
    Modifier targetMod = Modifier::None;

    switch (c) {
        case L'6': targetMod = Modifier::Circumflex; break;
        case L'7': targetMod = Modifier::Horn; break;
        case L'8': targetMod = Modifier::Breve; break;
        case L'9': targetMod = Modifier::Stroke; break;
        default: return false;
    }

    // Handle đ specially (key 9)
    if (targetMod == Modifier::Stroke) {
        for (auto it = states_.rbegin(); it != states_.rend(); ++it) {
            if (it->IsD()) {
                if (it->mod == Modifier::None) {
                    it->mod = Modifier::Stroke;
                    return true;
                } else if (it->mod == Modifier::Stroke) {
                    it->mod = Modifier::None;
                    ProcessChar(c);
                    return true;
                }
            }
        }
        return false;
    }

    // Handle vowel modifiers (6, 7, 8)
    for (auto it = states_.rbegin(); it != states_.rend(); ++it) {
        if (!it->IsVowel()) continue;

        wchar_t base = towlower(it->base);

        bool canApply = false;
        switch (targetMod) {
            case Modifier::Circumflex:
                canApply = (base == L'a' || base == L'e' || base == L'o');
                break;
            case Modifier::Horn:
                canApply = (base == L'o' || base == L'u');
                break;
            case Modifier::Breve:
                canApply = (base == L'a');
                break;
            default: break;
        }

        if (canApply) {
            if (it->mod == Modifier::None) {
                it->mod = targetMod;
                return true;
            } else if (it->mod == targetMod) {
                it->mod = Modifier::None;
                ProcessChar(c);
                return true;
            }
        }
    }

    return false;
}

//-----------------------------------------------------------------------------
// Tone Processing (1-5)
//-----------------------------------------------------------------------------

bool VniEngine::ProcessTone(wchar_t c) {
    Tone newTone = KeyToTone(c);
    if (newTone == Tone::None) return false;

    CharState* target = FindToneTarget();
    if (!target) return false;

    if (target->tone == Tone::None) {
        target->tone = newTone;
        return true;
    } else if (target->tone == newTone) {
        target->tone = Tone::None;
        ProcessChar(c);
        return true;
    } else {
        target->tone = newTone;
        return true;
    }
}

CharState* VniEngine::FindToneTarget() {
    return config_.modernOrtho ? FindToneTargetModern() : FindToneTargetClassic();
}

CharState* VniEngine::FindToneTargetClassic() {
    // Helper: detect "gi" consonant cluster (g + i + another vowel)
    auto isGICluster = [&](size_t i) -> bool {
        if (i == 0 || states_[i].base != L'i') return false;
        if (states_[i - 1].base != L'g') return false;
        // 'i' is part of "gi" cluster only if next char is a vowel
        return i + 1 < states_.size() && states_[i + 1].IsVowel();
    };

    // Helper: detect "qu" consonant cluster (q + u)
    auto isQUCluster = [&](size_t i) -> bool {
        return i > 0 && states_[i].base == L'u' && states_[i - 1].base == L'q';
    };

    // Collect vowel indices
    size_t vowelIndices[8];
    size_t vowelCount = 0;
    for (size_t i = 0; i < states_.size() && vowelCount < 8; ++i) {
        if (states_[i].IsVowel() && !isGICluster(i) && !isQUCluster(i)) {
            vowelIndices[vowelCount++] = i;
        }
    }

    if (vowelCount == 0) return nullptr;

    // Priority 1: Horn vowels
    size_t lastHornIdx = SIZE_MAX;
    for (size_t k = 0; k < vowelCount; ++k) {
        if (states_[vowelIndices[k]].mod == Modifier::Horn) {
            lastHornIdx = vowelIndices[k];
        }
    }
    if (lastHornIdx != SIZE_MAX) return &states_[lastHornIdx];

    // Priority 2: Modified vowels (â, ê, ô, ă)
    for (size_t k = 0; k < vowelCount; ++k) {
        if (states_[vowelIndices[k]].mod != Modifier::None) {
            return &states_[vowelIndices[k]];
        }
    }

    // Priority 3: Diphthong rules (classic — same as TelexEngine classic)
    if (vowelCount >= 2) {
        size_t lastIdx = vowelIndices[vowelCount - 1];
        size_t prevIdx = vowelIndices[vowelCount - 2];

        if (lastIdx == prevIdx + 1) {
            wchar_t first = states_[prevIdx].base;
            wchar_t last = states_[lastIdx].base;

            // Falling diphthongs: tone on FIRST
            if (first == L'a' && (last == L'i' || last == L'o' || last == L'u' || last == L'y')) return &states_[prevIdx];
            if (first == L'e' && (last == L'i' || last == L'o' || last == L'u')) return &states_[prevIdx];
            if (first == L'o' && (last == L'i' || last == L'u')) return &states_[prevIdx];
            if ((first == L'u' || first == L'i') && (last == L'i' || last == L'u')) return &states_[prevIdx];
            if (first == L'u' && (last == L'a' || last == L'e')) return &states_[prevIdx];

            // Classic: oa, oe → tone on FIRST (old-style: hòa, xòe)
            if (first == L'o' && (last == L'a' || last == L'e')) return &states_[prevIdx];

            // Rising diphthongs: tone on SECOND
            if (first == L'u' && last == L'y') return &states_[lastIdx];
        }
    }

    // Default: rightmost vowel
    return &states_[vowelIndices[vowelCount - 1]];
}

CharState* VniEngine::FindToneTargetModern() {
    // Helper: detect "gi" consonant cluster (g + i + another vowel)
    auto isGICluster = [&](size_t i) -> bool {
        if (i == 0 || states_[i].base != L'i') return false;
        if (states_[i - 1].base != L'g') return false;
        // 'i' is part of "gi" cluster only if next char is a vowel
        return i + 1 < states_.size() && states_[i + 1].IsVowel();
    };

    // Helper: detect "qu" consonant cluster (q + u)
    auto isQUCluster = [&](size_t i) -> bool {
        return i > 0 && states_[i].base == L'u' && states_[i - 1].base == L'q';
    };

    // Collect vowel indices
    size_t vowelIndices[8];
    size_t vowelCount = 0;
    for (size_t i = 0; i < states_.size() && vowelCount < 8; ++i) {
        if (states_[i].IsVowel() && !isGICluster(i) && !isQUCluster(i)) {
            vowelIndices[vowelCount++] = i;
        }
    }

    if (vowelCount == 0) return nullptr;

    // Priority 1: Horn vowels (last one for ươ)
    size_t lastHornIdx = SIZE_MAX;
    for (size_t k = 0; k < vowelCount; ++k) {
        if (states_[vowelIndices[k]].mod == Modifier::Horn) {
            lastHornIdx = vowelIndices[k];
        }
    }
    if (lastHornIdx != SIZE_MAX) return &states_[lastHornIdx];

    // Priority 2: Modified vowels (â, ê, ô, ă)
    for (size_t k = 0; k < vowelCount; ++k) {
        if (states_[vowelIndices[k]].mod != Modifier::None) {
            return &states_[vowelIndices[k]];
        }
    }

    // Priority 3: Diphthong rules (MODERN placement)
    if (vowelCount >= 2) {
        size_t lastIdx = vowelIndices[vowelCount - 1];
        size_t prevIdx = vowelIndices[vowelCount - 2];

        if (lastIdx == prevIdx + 1) {
            wchar_t first = states_[prevIdx].base;
            wchar_t last = states_[lastIdx].base;

            // Triphthongs: tone on MIDDLE
            if (vowelCount >= 3) {
                size_t midIdx = vowelIndices[vowelCount - 2];
                size_t firstVIdx = vowelIndices[vowelCount - 3];
                if (midIdx == firstVIdx + 1 && lastIdx == midIdx + 1) {
                    wchar_t v1 = states_[firstVIdx].base;
                    wchar_t v2 = states_[midIdx].base;
                    wchar_t v3 = states_[lastIdx].base;
                    if ((v1 == L'o' && v2 == L'a' && v3 == L'i') ||
                        (v1 == L'o' && v2 == L'e' && v3 == L'o') ||
                        (v1 == L'u' && v2 == L'y' && v3 == L'a') ||
                        (v1 == L'u' && v2 == L'y' && v3 == L'u')) {
                        return &states_[midIdx];
                    }
                }
            }

            // Falling diphthongs: tone on FIRST
            if (first == L'a' && (last == L'i' || last == L'o' || last == L'u' || last == L'y')) return &states_[prevIdx];
            if (first == L'e' && (last == L'i' || last == L'o' || last == L'u')) return &states_[prevIdx];
            if (first == L'o' && (last == L'i' || last == L'u')) return &states_[prevIdx];
            if ((first == L'u' || first == L'i') && (last == L'i' || last == L'u')) return &states_[prevIdx];

            // "ua" → tone on FIRST (same as classic: mùa, lụa, chùa)
            if (first == L'u' && last == L'a') return &states_[prevIdx];
            // "ue" → tone on SECOND (modern: thuế)
            if (first == L'u' && last == L'e') return &states_[lastIdx];

            // Rising diphthongs: tone on SECOND
            if (first == L'o' && (last == L'a' || last == L'e')) return &states_[lastIdx];
            if (first == L'u' && last == L'y') return &states_[lastIdx];

            // "uo" → tone on SECOND
            if (first == L'u' && last == L'o') return &states_[lastIdx];
        }
    }

    // Default: rightmost vowel
    return &states_[vowelIndices[vowelCount - 1]];
}

//-----------------------------------------------------------------------------
// Character Processing
//-----------------------------------------------------------------------------

void VniEngine::ProcessChar(wchar_t c) {
    CharState state;
    state.base = towlower(c);
    state.isUpper = iswupper(c);
    states_.push_back(state);
}

//-----------------------------------------------------------------------------
// Character Composition — O(1) flat array lookups
//-----------------------------------------------------------------------------

wchar_t VniEngine::ComposeChar(const CharState& state) const {
    wchar_t result = state.base;

    // Apply modifier first
    if (state.mod != Modifier::None) {
        if (state.mod == Modifier::Stroke && state.base == L'd') {
            result = L'đ';
        } else {
            int bi = VowelBaseIndex(state.base);
            int mi = ModifierIndex(state.mod);
            if (bi >= 0 && mi >= 0) {
                wchar_t modified = kModifiedVowel[bi][mi];
                if (modified) result = modified;
            }
        }
    }

    // Apply tone
    if (state.tone != Tone::None) {
        int ti = ToneBaseIndex(result);
        int si = ToneIndex(state.tone);
        if (ti >= 0 && si >= 0) {
            result = kTonedVowel[ti][si];
        }
    }

    // Apply case
    if (state.isUpper) {
        result = ToUpperVietnamese(result);
    }

    return result;
}

//-----------------------------------------------------------------------------
// Spell Check State Update
//-----------------------------------------------------------------------------

void VniEngine::UpdateSpellState() {
    if (!config_.spellCheckEnabled || states_.empty()) {
        spellCheckDisabled_ = false;
        return;
    }
    if (tempSpellOff_) {
        spellCheckDisabled_ = false;
        return;
    }
    auto result = SpellCheck::Validate(states_.data(), states_.size(), config_.allowZwjf);
    spellCheckDisabled_ = (result == SpellCheck::Result::Invalid);
}

}  // namespace Vni
}  // namespace NextKey
