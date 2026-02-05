// NexusKey - Telex Engine Implementation V2 (Table-Driven Architecture)
// SPDX-License-Identifier: GPL-3.0-only
//
// REWRITE: Cleaner, more maintainable, table-driven architecture
// - Explicit priority rules
// - QU/GI cluster handling
// - Proper case preservation
// - Easy to extend and debug

#include "TelexEngine.h"
#include <algorithm>
#include <map>

namespace NextKey {
namespace Telex {

namespace {

//=============================================================================
// RULE TABLES - Separating RULES from LOGIC
//=============================================================================

// Table 1: (base, modifier) → modified base
struct BaseModKey {
    wchar_t base;
    Modifier mod;
    bool operator<(const BaseModKey& o) const {
        return std::tie(base, mod) < std::tie(o.base, o.mod);
    }
};

const std::map<BaseModKey, wchar_t> kBaseModTable = {
    // Circumflex: â ê ô
    {{L'a', Modifier::Circumflex}, L'â'},
    {{L'e', Modifier::Circumflex}, L'ê'},
    {{L'o', Modifier::Circumflex}, L'ô'},
    // Breve: ă
    {{L'a', Modifier::Breve}, L'ă'},
    // Horn: ơ ư
    {{L'o', Modifier::Horn}, L'ơ'},
    {{L'u', Modifier::Horn}, L'ư'},
};

// Table 2: (char, tone) → toned char (complete 60 entries)
struct ToneKey {
    wchar_t ch;
    Tone tone;
    bool operator<(const ToneKey& o) const {
        return std::tie(ch, tone) < std::tie(o.ch, o.tone);
    }
};

const std::map<ToneKey, wchar_t> kToneTable = {
    // a
    {{L'a', Tone::Acute}, L'á'}, {{L'a', Tone::Grave}, L'à'},
    {{L'a', Tone::Hook}, L'ả'}, {{L'a', Tone::Tilde}, L'ã'}, {{L'a', Tone::Dot}, L'ạ'},
    // â
    {{L'â', Tone::Acute}, L'ấ'}, {{L'â', Tone::Grave}, L'ầ'},
    {{L'â', Tone::Hook}, L'ẩ'}, {{L'â', Tone::Tilde}, L'ẫ'}, {{L'â', Tone::Dot}, L'ậ'},
    // ă
    {{L'ă', Tone::Acute}, L'ắ'}, {{L'ă', Tone::Grave}, L'ằ'},
    {{L'ă', Tone::Hook}, L'ẳ'}, {{L'ă', Tone::Tilde}, L'ẵ'}, {{L'ă', Tone::Dot}, L'ặ'},
    // e
    {{L'e', Tone::Acute}, L'é'}, {{L'e', Tone::Grave}, L'è'},
    {{L'e', Tone::Hook}, L'ẻ'}, {{L'e', Tone::Tilde}, L'ẽ'}, {{L'e', Tone::Dot}, L'ẹ'},
    // ê
    {{L'ê', Tone::Acute}, L'ế'}, {{L'ê', Tone::Grave}, L'ề'},
    {{L'ê', Tone::Hook}, L'ể'}, {{L'ê', Tone::Tilde}, L'ễ'}, {{L'ê', Tone::Dot}, L'ệ'},
    // i
    {{L'i', Tone::Acute}, L'í'}, {{L'i', Tone::Grave}, L'ì'},
    {{L'i', Tone::Hook}, L'ỉ'}, {{L'i', Tone::Tilde}, L'ĩ'}, {{L'i', Tone::Dot}, L'ị'},
    // o
    {{L'o', Tone::Acute}, L'ó'}, {{L'o', Tone::Grave}, L'ò'},
    {{L'o', Tone::Hook}, L'ỏ'}, {{L'o', Tone::Tilde}, L'õ'}, {{L'o', Tone::Dot}, L'ọ'},
    // ô
    {{L'ô', Tone::Acute}, L'ố'}, {{L'ô', Tone::Grave}, L'ồ'},
    {{L'ô', Tone::Hook}, L'ổ'}, {{L'ô', Tone::Tilde}, L'ỗ'}, {{L'ô', Tone::Dot}, L'ộ'},
    // ơ
    {{L'ơ', Tone::Acute}, L'ớ'}, {{L'ơ', Tone::Grave}, L'ờ'},
    {{L'ơ', Tone::Hook}, L'ở'}, {{L'ơ', Tone::Tilde}, L'ỡ'}, {{L'ơ', Tone::Dot}, L'ợ'},
    // u
    {{L'u', Tone::Acute}, L'ú'}, {{L'u', Tone::Grave}, L'ù'},
    {{L'u', Tone::Hook}, L'ủ'}, {{L'u', Tone::Tilde}, L'ũ'}, {{L'u', Tone::Dot}, L'ụ'},
    // ư
    {{L'ư', Tone::Acute}, L'ứ'}, {{L'ư', Tone::Grave}, L'ừ'},
    {{L'ư', Tone::Hook}, L'ử'}, {{L'ư', Tone::Tilde}, L'ữ'}, {{L'ư', Tone::Dot}, L'ự'},
    // y
    {{L'y', Tone::Acute}, L'ý'}, {{L'y', Tone::Grave}, L'ỳ'},
    {{L'y', Tone::Hook}, L'ỷ'}, {{L'y', Tone::Tilde}, L'ỹ'}, {{L'y', Tone::Dot}, L'ỵ'},
};

//=============================================================================
// HELPER FUNCTIONS
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

// Vietnamese-aware uppercase conversion
wchar_t ToUpperVietnamese(wchar_t ch) {
    // Latin-1 Supplement
    if (ch >= 0x00E0 && ch <= 0x00F6) return ch - 0x20;
    if (ch >= 0x00F8 && ch <= 0x00FE) return ch - 0x20;

    // Latin Extended-A
    if (ch == L'ă') return L'Ă';
    if (ch == L'đ') return L'Đ';

    // Latin Extended-B
    if (ch == L'ơ') return L'Ơ';
    if (ch == L'ư') return L'Ư';

    // Vietnamese toned vowels (Latin Extended Additional)
    if (ch >= 0x1EA0 && ch <= 0x1EF9) {
        if (ch & 1) return ch - 1;  // Odd → Even
        return ch;
    }

    return towupper(ch);
}

}  // namespace

//=============================================================================
// TelexEngine Implementation
//=============================================================================

TelexEngine::TelexEngine(const TypingConfig& config) : config_(config) {
    Reset();
}

//-----------------------------------------------------------------------------
// Main Entry Point
//-----------------------------------------------------------------------------

void TelexEngine::PushChar(wchar_t c) {
    rawInput_.push_back(c);

    // 1. Try tone keys (s, f, r, x, j)
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
                // Don't update isUpper - keep original case
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
        // Clear circumflex on 'a' if present
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
    // If horn vowel exists, let P7 escape handle it  
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
    // Search for any 'd' in the word
    for (auto it = states_.rbegin(); it != states_.rend(); ++it) {
        if (it->IsD()) {
            if (it->mod == Modifier::None) {
                // Apply đ - PRESERVE FIRST LETTER CASE
                it->mod = Modifier::Breve;
                // Keep original isUpper from when 'd' was first typed
                return true;
            } else if (it->mod == Modifier::Breve) {
                // Escape: đ + d → dd
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
    // Check if we have "qu" pattern (q followed by u)
    // In this case, 'u' is part of consonant cluster, not a vowel
    if (states_.size() < 2) return false;
    
    for (size_t i = 0; i + 1 < states_.size(); ++i) {
        if (states_[i].base == L'q' && states_[i+1].base == L'u') {
            // Found "qu" cluster - 'w' should not apply to 'u'
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
// Auto ươ Transformation
//-----------------------------------------------------------------------------

void TelexEngine::ApplyAutoUO() {
    // Pattern: 'u' (no horn) + 'ơ' (has horn) + [any char]
    // Result: Convert 'u' to 'ư'
    if (states_.size() < 3) return;

    for (size_t i = 0; i + 2 < states_.size(); ++i) {
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
    // Example: "cuả" + w → "cửa" (tone moves from a to ư)
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
// Tone Target Finding - EXPLICIT PRIORITY
//-----------------------------------------------------------------------------

size_t TelexEngine::FindToneTarget() const {
    std::vector<size_t> vowelIndices;
    for (size_t i = 0; i < states_.size(); ++i) {
        if (states_[i].IsVowel()) {
            vowelIndices.push_back(i);
        }
    }

    if (vowelIndices.empty()) return SIZE_MAX;

    // Priority 1: Horn vowels (last one for ươ)
    size_t lastHornIdx = SIZE_MAX;
    for (size_t idx : vowelIndices) {
        if (states_[idx].mod == Modifier::Horn) {
            lastHornIdx = idx;
        }
    }
    if (lastHornIdx != SIZE_MAX) return lastHornIdx;

    // Priority 2: Modified vowels (â, ê, ô, ă)
    for (size_t idx : vowelIndices) {
        if (states_[idx].mod != Modifier::None) {
            return idx;
        }
    }

    // Priority 3: Diphthong rules
    if (vowelIndices.size() >= 2) {
        size_t lastIdx = vowelIndices.back();
        size_t prevIdx = vowelIndices[vowelIndices.size() - 2];

        if (lastIdx == prevIdx + 1) {
            wchar_t first = states_[prevIdx].base;
            wchar_t last = states_[lastIdx].base;

            // Falling diphthongs: tone on FIRST
            if (first == L'a' && (last == L'i' || last == L'o' || last == L'u' || last == L'y')) return prevIdx;
            if (first == L'e' && (last == L'i' || last == L'o' || last == L'u')) return prevIdx;
            if (first == L'o' && (last == L'i' || last == L'u')) return prevIdx;
            if ((first == L'u' || first == L'i') && (last == L'i' || last == L'u')) return prevIdx;
            if (first == L'u' && (last == L'a' || last == L'e')) return prevIdx;

            // Rising diphthongs: tone on SECOND
            if (first == L'o' && (last == L'a' || last == L'e')) return lastIdx;
            if (first == L'u' && last == L'y') return lastIdx;
        }
    }

    // Default: rightmost vowel
    return vowelIndices.back();
}

//-----------------------------------------------------------------------------
// Composition (State → Unicode)
//-----------------------------------------------------------------------------

wchar_t TelexEngine::Compose(const CharState& s) {
    if (s.IsEmpty()) return 0;

    wchar_t ch = s.base;

    // Special: đ
    if (s.IsD() && s.mod == Modifier::Breve) {
        return s.isUpper ? L'Đ' : L'đ';
    }

    // Step 1: Apply modifier
    if (s.mod != Modifier::None && s.IsVowel()) {
        auto it = kBaseModTable.find({s.base, s.mod});
        if (it != kBaseModTable.end()) {
            ch = it->second;
        }
    }

    // Step 2: Apply tone
    if (s.tone != Tone::None) {
        auto it = kToneTable.find({ch, s.tone});
        if (it != kToneTable.end()) {
            ch = it->second;
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

size_t TelexEngine::Count() const {
    return states_.size();
}

}  // namespace Telex
}  // namespace NextKey
