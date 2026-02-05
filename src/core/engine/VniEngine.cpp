// NexusKey - VNI Input Method Engine Implementation
// SPDX-License-Identifier: GPL-3.0-only

#include "VniEngine.h"
#include <algorithm>
#include <map>

namespace NextKey {
namespace Vni {

//=============================================================================
// RULE TABLES - VNI uses numeric keys for modifiers and tones
//=============================================================================

// Table: (base, modifier) → modified base
struct BaseModKey {
    wchar_t base;
    Modifier mod;
    bool operator<(const BaseModKey& o) const {
        return std::tie(base, mod) < std::tie(o.base, o.mod);
    }
};

const std::map<BaseModKey, wchar_t> kBaseModTable = {
    // Circumflex (key 6): â ê ô
    {{L'a', Modifier::Circumflex}, L'â'},
    {{L'e', Modifier::Circumflex}, L'ê'},
    {{L'o', Modifier::Circumflex}, L'ô'},
    // Breve (key 8): ă
    {{L'a', Modifier::Breve}, L'ă'},
    // Horn (key 7): ơ ư
    {{L'o', Modifier::Horn}, L'ơ'},
    {{L'u', Modifier::Horn}, L'ư'},
    // Stroke (key 9): handled separately for 'd'
};

// Table: (char, tone) → toned char
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
    // ă
    {{L'ă', Tone::Acute}, L'ắ'}, {{L'ă', Tone::Grave}, L'ằ'},
    {{L'ă', Tone::Hook}, L'ẳ'}, {{L'ă', Tone::Tilde}, L'ẵ'}, {{L'ă', Tone::Dot}, L'ặ'},
    // â
    {{L'â', Tone::Acute}, L'ấ'}, {{L'â', Tone::Grave}, L'ầ'},
    {{L'â', Tone::Hook}, L'ẩ'}, {{L'â', Tone::Tilde}, L'ẫ'}, {{L'â', Tone::Dot}, L'ậ'},
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

wchar_t ToUpperVietnamese(wchar_t ch) {
    // Vietnamese special uppercase mappings
    static const std::map<wchar_t, wchar_t> upperMap = {
        {L'ă', L'Ă'}, {L'â', L'Â'}, {L'đ', L'Đ'}, {L'ê', L'Ê'},
        {L'ô', L'Ô'}, {L'ơ', L'Ơ'}, {L'ư', L'Ư'},
        // Toned vowels
        {L'á', L'Á'}, {L'à', L'À'}, {L'ả', L'Ả'}, {L'ã', L'Ã'}, {L'ạ', L'Ạ'},
        {L'ắ', L'Ắ'}, {L'ằ', L'Ằ'}, {L'ẳ', L'Ẳ'}, {L'ẵ', L'Ẵ'}, {L'ặ', L'Ặ'},
        {L'ấ', L'Ấ'}, {L'ầ', L'Ầ'}, {L'ẩ', L'Ẩ'}, {L'ẫ', L'Ẫ'}, {L'ậ', L'Ậ'},
        {L'é', L'É'}, {L'è', L'È'}, {L'ẻ', L'Ẻ'}, {L'ẽ', L'Ẽ'}, {L'ẹ', L'Ẹ'},
        {L'ế', L'Ế'}, {L'ề', L'Ề'}, {L'ể', L'Ể'}, {L'ễ', L'Ễ'}, {L'ệ', L'Ệ'},
        {L'í', L'Í'}, {L'ì', L'Ì'}, {L'ỉ', L'Ỉ'}, {L'ĩ', L'Ĩ'}, {L'ị', L'Ị'},
        {L'ó', L'Ó'}, {L'ò', L'Ò'}, {L'ỏ', L'Ỏ'}, {L'õ', L'Õ'}, {L'ọ', L'Ọ'},
        {L'ố', L'Ố'}, {L'ồ', L'Ồ'}, {L'ổ', L'Ổ'}, {L'ỗ', L'Ỗ'}, {L'ộ', L'Ộ'},
        {L'ớ', L'Ớ'}, {L'ờ', L'Ờ'}, {L'ở', L'Ở'}, {L'ỡ', L'Ỡ'}, {L'ợ', L'Ợ'},
        {L'ú', L'Ú'}, {L'ù', L'Ù'}, {L'ủ', L'Ủ'}, {L'ũ', L'Ũ'}, {L'ụ', L'Ụ'},
        {L'ứ', L'Ứ'}, {L'ừ', L'Ừ'}, {L'ử', L'Ử'}, {L'ữ', L'Ữ'}, {L'ự', L'Ự'},
        {L'ý', L'Ý'}, {L'ỳ', L'Ỳ'}, {L'ỷ', L'Ỷ'}, {L'ỹ', L'Ỹ'}, {L'ỵ', L'Ỵ'},
    };
    auto it = upperMap.find(ch);
    if (it != upperMap.end()) return it->second;
    return towupper(ch);
}

}  // namespace

//=============================================================================
// CharState Implementation
//=============================================================================

bool CharState::IsVowel() const {
    return IsVowelChar(base);
}

//=============================================================================
// VniEngine Implementation
//=============================================================================

VniEngine::VniEngine(const TypingConfig& config) : config_(config) {}

void VniEngine::PushChar(wchar_t c) {
    rawInput_ += c;
    
    // Try tone keys first (1-5)
    if (IsToneKey(c)) {
        if (ProcessTone(c)) return;
    }
    
    // Try modifier keys (6-9)
    if (IsModifierKey(c)) {
        if (ProcessModifier(c)) return;
    }
    
    // Regular character
    ProcessChar(c);
}

void VniEngine::Backspace() {
    if (!states_.empty()) {
        states_.pop_back();
    }
    if (!rawInput_.empty()) {
        rawInput_.pop_back();
    }
}

std::wstring VniEngine::Peek() const {
    std::wstring result;
    for (const auto& s : states_) {
        wchar_t ch = const_cast<VniEngine*>(this)->ComposeChar(s);
        result += ch;
    }
    return result;
}

std::wstring VniEngine::Commit() {
    std::wstring result = Peek();
    Reset();
    return result;
}

void VniEngine::Reset() {
    states_.clear();
    rawInput_.clear();
}

size_t VniEngine::Count() const {
    return states_.size();
}

//-----------------------------------------------------------------------------
// Modifier Processing (6, 7, 8, 9)
//-----------------------------------------------------------------------------

bool VniEngine::ProcessModifier(wchar_t c) {
    Modifier targetMod = Modifier::None;
    
    switch (c) {
        case L'6': targetMod = Modifier::Circumflex; break;  // â ê ô
        case L'7': targetMod = Modifier::Horn; break;        // ơ ư
        case L'8': targetMod = Modifier::Breve; break;       // ă
        case L'9': targetMod = Modifier::Stroke; break;      // đ
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
                    // Escape: đ + 9 → d9
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
        
        // Check if this modifier applies to this base vowel
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
                // Escape: clear modifier and add literal
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
        // Escape: clear tone and add literal
        target->tone = Tone::None;
        ProcessChar(c);
        return true;
    } else {
        // Replace tone
        target->tone = newTone;
        return true;
    }
}

CharState* VniEngine::FindToneTarget() {
    // Find rightmost vowel for tone placement
    // (VNI typically uses simpler rules than Telex)
    for (auto it = states_.rbegin(); it != states_.rend(); ++it) {
        if (it->IsVowel()) {
            return &(*it);
        }
    }
    return nullptr;
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
// Character Composition
//-----------------------------------------------------------------------------

wchar_t VniEngine::ComposeChar(const CharState& state) const {
    wchar_t result = state.base;
    
    // Apply modifier first
    if (state.mod != Modifier::None) {
        if (state.mod == Modifier::Stroke && state.base == L'd') {
            result = L'đ';
        } else {
            auto it = kBaseModTable.find({state.base, state.mod});
            if (it != kBaseModTable.end()) {
                result = it->second;
            }
        }
    }
    
    // Apply tone
    if (state.tone != Tone::None) {
        auto it = kToneTable.find({result, state.tone});
        if (it != kToneTable.end()) {
            result = it->second;
        }
    }
    
    // Apply case
    if (state.isUpper) {
        result = ToUpperVietnamese(result);
    }
    
    return result;
}

}  // namespace Vni
}  // namespace NextKey
