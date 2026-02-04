// NexusKey - Telex Engine Implementation (State-Based)
// SPDX-License-Identifier: GPL-3.0-only

#include "TelexEngine.h"
#include <algorithm>
#include <map>

namespace NextKey {
namespace Telex {

namespace {

//=============================================================================
// COMPOSE TABLES - Small, immutable, complete
//=============================================================================

// Table 1: (base, modifier) → modified base
// Only 6 entries for vowels that can have modifiers
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

// Table 2: (char, tone) → toned char
// ~60 entries covering all vowels × 5 tones
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

// Helper: is this a tone key?
bool IsToneKey(wchar_t c) {
    wchar_t lower = towlower(c);
    return lower == L's' || lower == L'f' || lower == L'r' ||
           lower == L'x' || lower == L'j';
}

// Helper: tone key to Tone enum
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

// Helper: is this a base vowel?
bool IsVowelChar(wchar_t c) {
    wchar_t lower = towlower(c);
    return lower == L'a' || lower == L'e' || lower == L'i' ||
           lower == L'o' || lower == L'u' || lower == L'y';
}

}  // namespace

//=============================================================================
// TelexEngine Implementation
//=============================================================================

TelexEngine::TelexEngine(const TypingConfig& config) : config_(config) {
    Reset();
}

void TelexEngine::PushChar(wchar_t c) {
    // Track raw input for escape
    rawInput_.push_back(c);

    // 1. Check for tone keys (s, f, r, x, j)
    if (IsToneKey(c) && !states_.empty()) {
        if (ProcessTone(c)) {
            ApplyAutoUO();  // Check for auto ươ after tone
            return;
        }
    }

    // 2. Check for modifier keys (w, or double vowel like aa, ee, oo, dd)
    if (!states_.empty()) {
        if (ProcessModifier(c)) {
            ApplyAutoUO();  // Check auto-ươ (e.g., huonw → hươn)
            return;
        }
    }

    // 3. Regular character - add new state
    ProcessChar(c);

    // 4. Auto ươ: if character added after 'uơ' pattern, convert to 'ươ'
    ApplyAutoUO();
}

bool TelexEngine::ProcessTone(wchar_t c) {
    Tone newTone = KeyToTone(c);
    if (newTone == Tone::None) return false;

    size_t targetIdx = FindToneTarget();
    if (targetIdx == SIZE_MAX) {
        // No vowel to apply tone to - add as regular char
        return false;
    }

    CharState& target = states_[targetIdx];

    // Escape: if same tone already applied, clear it and add key as character
    if (target.tone == newTone) {
        target.tone = Tone::None;
        ProcessChar(c);
        return true;
    }

    // Apply or replace tone (different tone replaces existing)
    target.tone = newTone;
    return true;
}

bool TelexEngine::ProcessModifier(wchar_t c) {
    wchar_t lower = towlower(c);
    CharState& last = states_.back();

    // 1. Check for 'w' modifier (ă, ơ, ư)
    if (lower == L'w') {
        // First pass: find unmodified vowel to apply modifier
        decltype(states_.rbegin()) lastModifiedIt = states_.rend();

        for (auto it = states_.rbegin(); it != states_.rend(); ++it) {
            if (!it->IsVowel()) continue;

            wchar_t base = it->base;
            Modifier newMod = Modifier::None;

            if (base == L'a') {
                newMod = Modifier::Breve;  // ă
            } else if (base == L'o' || base == L'u') {
                newMod = Modifier::Horn;   // ơ, ư
            }

            if (newMod != Modifier::None) {
                if (it->mod == newMod) {
                    // Remember first vowel with same modifier (for escape)
                    if (lastModifiedIt == states_.rend()) {
                        lastModifiedIt = it;
                    }
                    continue;  // Keep looking for unmodified vowel
                }
                if (it->mod != Modifier::None) {
                    continue;  // Has different modifier, skip
                }
                // Found unmodified vowel - apply modifier
                it->mod = newMod;
                return true;
            }
        }

        // No unmodified vowel found - check if we should escape
        if (lastModifiedIt != states_.rend()) {
            // Escape: clear modifier and add 'w' as character
            lastModifiedIt->mod = Modifier::None;
            ProcessChar(c);
            return true;
        }

        // No applicable vowel at all - add 'w' as character
        return false;
    }

    // 2. Check for double vowel → circumflex (aa→â, ee→ê, oo→ô)
    if (IsVowelChar(c) && last.IsVowel() && last.base == lower) {
        // Can this vowel have circumflex?
        if (lower == L'a' || lower == L'e' || lower == L'o') {
            // Escape: if already has circumflex, clear it and add vowel
            if (last.mod == Modifier::Circumflex) {
                last.mod = Modifier::None;
                ProcessChar(c);
                return true;
            }
            last.mod = Modifier::Circumflex;
            last.isUpper = iswupper(c);  // Update case from new input
            return true;
        }
    }

    // 3. Check for dd → đ (non-consecutive: search for any 'd' in word)
    if (lower == L'd') {
        // Search backwards for any 'd' that doesn't have Breve modifier
        for (auto it = states_.rbegin(); it != states_.rend(); ++it) {
            if (it->IsD()) {
                if (it->mod == Modifier::None) {
                    // Found unmodified 'd' → make it đ
                    it->mod = Modifier::Breve;
                    it->isUpper = iswupper(c);
                    return true;
                } else if (it->mod == Modifier::Breve) {
                    // Found đ → escape (clear modifier, add 'd')
                    it->mod = Modifier::None;
                    ProcessChar(c);
                    return true;
                }
            }
        }
    }

    return false;
}

void TelexEngine::ProcessChar(wchar_t c) {
    CharState s;
    s.base = towlower(c);
    s.isUpper = iswupper(c);
    s.mod = Modifier::None;
    s.tone = Tone::None;
    states_.push_back(s);
}

void TelexEngine::ApplyAutoUO() {
    // Auto ươ: if pattern 'u' (no horn) + 'ơ' (has horn) exists and
    // there's something after 'ơ', convert 'u' to 'ư'
    // This handles: dduown → đươn, nguowif → người
    if (states_.size() < 3) return;

    for (size_t i = 0; i + 2 < states_.size(); ++i) {
        if (states_[i].base == L'u' && states_[i].mod == Modifier::None &&
            states_[i+1].base == L'o' && states_[i+1].mod == Modifier::Horn) {
            // Found 'uơ' pattern with something after it
            states_[i].mod = Modifier::Horn;
        }
    }
}

size_t TelexEngine::FindToneTarget() const {
    // Vietnamese tone placement rules:
    // 1. If word has ơ, ư (horn vowels) - tone goes on the LAST horn vowel
    //    (important for ươ diphthong: tone goes on ơ, not ư)
    // 2. If word has ô, â, ê (circumflex) or ă (breve) - tone goes there
    // 3. For vowel clusters (oa, oe, uy, etc.), usually second vowel
    // 4. Default: rightmost vowel

    // Find all vowel indices
    std::vector<size_t> vowelIndices;
    for (size_t i = 0; i < states_.size(); ++i) {
        if (states_[i].IsVowel()) {
            vowelIndices.push_back(i);
        }
    }

    if (vowelIndices.empty()) return SIZE_MAX;

    // Priority 1: Horn vowels (ơ, ư) - prefer the LAST one for ươ diphthong
    size_t lastHornIdx = SIZE_MAX;
    for (size_t idx : vowelIndices) {
        if (states_[idx].mod == Modifier::Horn) {
            lastHornIdx = idx;  // Keep updating to get the last one
        }
    }
    if (lastHornIdx != SIZE_MAX) {
        return lastHornIdx;
    }

    // Priority 2: Modified vowels (â, ê, ô, ă)
    for (size_t idx : vowelIndices) {
        if (states_[idx].mod != Modifier::None) {
            return idx;
        }
    }

    // Priority 3: For consecutive vowel pairs (diphthongs)
    if (vowelIndices.size() >= 2) {
        size_t lastIdx = vowelIndices.back();
        size_t prevIdx = vowelIndices[vowelIndices.size() - 2];

        // Check if they're consecutive
        if (lastIdx == prevIdx + 1) {
            wchar_t firstVowel = states_[prevIdx].base;
            wchar_t lastVowel = states_[lastIdx].base;

            // Falling diphthongs: tone on FIRST vowel
            // ai, ao, au, ay, âu, ây, etc.
            if (firstVowel == L'a' && (lastVowel == L'i' || lastVowel == L'o' || lastVowel == L'u' || lastVowel == L'y')) {
                return prevIdx;
            }
            // ei, eo, êu, etc.
            if (firstVowel == L'e' && (lastVowel == L'i' || lastVowel == L'o' || lastVowel == L'u')) {
                return prevIdx;
            }
            // oi, ôi, ơi
            if (firstVowel == L'o' && (lastVowel == L'i' || lastVowel == L'u')) {
                return prevIdx;
            }
            // ui, ưi, iu
            if ((firstVowel == L'u' || firstVowel == L'i') && (lastVowel == L'i' || lastVowel == L'u')) {
                return prevIdx;
            }

            // Rising diphthongs: tone on SECOND vowel
            // oa, oe, oă
            if (firstVowel == L'o' && (lastVowel == L'a' || lastVowel == L'e')) {
                return lastIdx;
            }
            // ua, uê, uy, uâ
            if (firstVowel == L'u' && (lastVowel == L'a' || lastVowel == L'e' || lastVowel == L'y')) {
                return lastIdx;
            }
        }
    }

    // Default: rightmost vowel
    return vowelIndices.back();
}

// Helper: convert Vietnamese lowercase to uppercase (towupper doesn't work without locale)
wchar_t ToUpperVietnamese(wchar_t ch) {
    // Latin-1 Supplement (difference is 0x20)
    if (ch >= 0x00E0 && ch <= 0x00F6) return ch - 0x20;  // à-ö → À-Ö
    if (ch >= 0x00F8 && ch <= 0x00FE) return ch - 0x20;  // ø-þ → Ø-Þ

    // Latin Extended-A (ă, đ, etc.)
    if (ch == L'ă') return L'Ă';  // U+0103 → U+0102
    if (ch == L'đ') return L'Đ';  // U+0111 → U+0110

    // Latin Extended-B (ơ, ư)
    if (ch == L'ơ') return L'Ơ';  // U+01A1 → U+01A0
    if (ch == L'ư') return L'Ư';  // U+01B0 → U+01AF

    // Vietnamese toned vowels (Latin Extended Additional U+1EA0-U+1EF9)
    // These are pairs: lowercase at odd, uppercase at even
    if (ch >= 0x1EA0 && ch <= 0x1EF9) {
        if (ch & 1) return ch - 1;  // Odd → Even (lowercase → uppercase)
        return ch;  // Already uppercase
    }

    // Fallback to standard towupper
    return towupper(ch);
}

wchar_t TelexEngine::Compose(const CharState& s) {
    if (s.IsEmpty()) return 0;

    wchar_t ch = s.base;

    // Special case: đ (d with Breve modifier)
    if (s.IsD() && s.mod == Modifier::Breve) {
        return s.isUpper ? L'Đ' : L'đ';
    }

    // Step 1: Apply modifier (base → modified base)
    if (s.mod != Modifier::None && s.IsVowel()) {
        auto it = kBaseModTable.find({s.base, s.mod});
        if (it != kBaseModTable.end()) {
            ch = it->second;
        }
    }

    // Step 2: Apply tone (modified base → toned char)
    if (s.tone != Tone::None) {
        auto it = kToneTable.find({ch, s.tone});
        if (it != kToneTable.end()) {
            ch = it->second;
        }
    }

    // Step 3: Apply case (use Vietnamese-aware uppercase conversion)
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
        if (ch != 0) {
            result += ch;
        }
    }
    return result;
}

void TelexEngine::Backspace() {
    if (states_.empty()) return;

    CharState& last = states_.back();

    // Backspace order: tone → modifier → base
    if (last.tone != Tone::None) {
        last.tone = Tone::None;
    } else if (last.mod != Modifier::None) {
        last.mod = Modifier::None;
    } else {
        states_.pop_back();
    }

    // Keep rawInput_ in sync (just pop last)
    if (!rawInput_.empty()) {
        rawInput_.pop_back();
    }
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
