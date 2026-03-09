// NexusKey - VNI Input Method Engine Implementation (Optimized)
// SPDX-License-Identifier: GPL-3.0-only
//
// Uses shared VietnameseTables.h for O(1) flat array composition lookups.

#include "VniEngine.h"
#include "EngineHelpers.h"
#include "VietnameseTables.h"

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
    quickConsonantOnly_ = false;  // Any new char clears the flag

    // 0a. Quick start consonant: f→ph, j→gi, w→qu (only at word start)
    if (config_.quickStartConsonant && states_.empty()) {
        wchar_t lower = towlower(c);
        wchar_t first = 0, second = 0;
        if (lower == L'f') { first = L'p'; second = L'h'; }
        else if (lower == L'j') { first = L'g'; second = L'i'; }
        else if (lower == L'w') { first = L'q'; second = L'u'; }
        if (first) {
            bool upper = iswupper(c);
            size_t ri = rawInput_.size() - 1;
            ProcessChar(upper ? towupper(first) : first, ri);
            ProcessChar(second, rawInput_.size());
            quickStartKey_ = c;  // Remember original key for undo
            UpdateSpellState();
            return;
        }
    }

    // 0a-cont. Undo quick start consonant if next char is not a vowel
    if (quickStartKey_ != 0) {
        wchar_t savedKey = quickStartKey_;
        quickStartKey_ = 0;
        if (!IsVowelChar(c)) {
            states_.clear();
            rawInput_.clear();
            rawInput_ += savedKey;
            ProcessChar(savedKey, 0);
            rawInput_ += c;
            // Fall through to normal processing below
        }
    }

    // 0b. Quick consonant: cc→ch, gg→gi, nn→ng, kk→kh, qq→qu, pp→ph, tt→th
    // Skip if backspace just undid a quick consonant (let user type the literal)
    bool quickEscaped = quickConsonantEscaped_;
    quickConsonantEscaped_ = false;

    // Suppress consecutive re-triggering: after cc→ch, skip quick consonant
    // while the user keeps pressing the same key (e.g., cccc → chcc, not chch)
    wchar_t lower = towlower(c);
    if (lastQuickConsonantKey_ != 0) {
        if (lower == lastQuickConsonantKey_) {
            quickEscaped = true;  // Reuse escape flag to skip quick consonant
        } else {
            lastQuickConsonantKey_ = 0;  // Different key, allow future expansions
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
                c = iswupper(c) ? towupper(replacement) : replacement;
                if (states_.size() == 1) quickConsonantOnly_ = true;
                quickConsonantIdx_ = states_.size();
                lastQuickConsonantKey_ = lower;  // Suppress re-trigger on consecutive same key
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
                quickConsonantIdx_ = states_.size() - 1;
                if (states_.size() == 2) quickConsonantOnly_ = true;
                lastQuickConsonantKey_ = lower;  // Suppress re-trigger on consecutive same key
                UpdateSpellState();
                return;
            }
            // Triphthong — fall through to normal processing
        }
    }

    // 1. Try tone keys (1-5) — gated by spell check
    if (IsToneKey(c)) {
        if (config_.spellCheckEnabled && spellCheckDisabled_) {
            ProcessChar(c, rawInput_.size() - 1);
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
        wchar_t first = 0, second = 0;
        if (lower == L'g') { first = L'n'; second = L'g'; }
        else if (lower == L'h') { first = L'n'; second = L'h'; }
        else if (lower == L'k') { first = L'c'; second = L'h'; }
        if (first) {
            size_t ri = rawInput_.size() - 1;
            ProcessChar(first, ri);
            ProcessChar(second, rawInput_.size());
            UpdateSpellState();
            return;
        }
    }

    // Regular character
    ProcessChar(c, rawInput_.size() - 1);
    UpdateSpellState();
}

void VniEngine::Backspace() {
    if (states_.empty()) return;
    dModifierEscaped_ = false;  // Allow đ re-trigger after user edits

    // Undo quick start consonant: ph→f, gi→j, qu→w (collapse both chars to original)
    if (quickStartKey_ != 0 && states_.size() == 2) {
        states_.clear();
        rawInput_.clear();
        rawInput_ += quickStartKey_;
        ProcessChar(quickStartKey_, 0);
        quickStartKey_ = 0;
        UpdateSpellState();
        return;
    }
    quickStartKey_ = 0;

    // Check if we're undoing a quick consonant expansion
    if (quickConsonantIdx_ != SIZE_MAX && states_.size() - 1 == quickConsonantIdx_) {
        quickConsonantEscaped_ = true;
        UndoHornU(states_.data(), states_.size() - 1);
    }
    quickConsonantIdx_ = SIZE_MAX;
    lastQuickConsonantKey_ = 0;

    // Trim rawInput_ to the position when this state was created.
    // This correctly handles quick consonants (f→ph) where one raw key
    // produces multiple states, and modifier/tone keys that modify
    // existing states without creating new ones.
    size_t rawTarget = states_.back().rawIdx;
    states_.pop_back();
    if (rawInput_.size() > rawTarget) {
        rawInput_.resize(rawTarget);
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

    // Single quick-start consonant alone (f->ph, j->gi, w->qu) — always restore
    // This allows "j " -> "j " instead of "gi ", since "gi" is a valid word and wouldn't auto-restore naturally.
    if (quickStartKey_ != 0 && rawInput_.size() == 1) {
        std::wstring raw = rawInput_;
        if (ShouldAutoRestore(raw, composed)) {
            Reset();
            return raw;
        }
    }

    // Quick consonant alone (gg, uu) — always restore regardless of spell check setting
    if (quickConsonantOnly_) {
        std::wstring raw = rawInput_;
        if (ShouldAutoRestore(raw, composed)) {
            Reset();
            return raw;
        }
    }

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

        if (shouldRestore && !HasIntentionalStrokeD(rawInput_)) {
            std::wstring raw = rawInput_;
            if (ShouldAutoRestore(raw, composed)) {
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
    quickConsonantOnly_ = false;
    quickConsonantEscaped_ = false;
    quickConsonantIdx_ = SIZE_MAX;
    lastQuickConsonantKey_ = 0;
    dModifierEscaped_ = false;
    quickStartKey_ = 0;
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

    // Handle đ specially (key 9) — Vietnamese đ only at syllable start
    if (targetMod == Modifier::Stroke) {
        if (dModifierEscaped_) return false;  // User already escaped đ→d
        if (!states_.empty() && states_[0].IsD()) {
            CharState& first = states_[0];
            if (first.mod == Modifier::None) {
                first.mod = Modifier::Stroke;
                return true;
            } else if (first.mod == Modifier::Stroke) {
                first.mod = Modifier::None;
                dModifierEscaped_ = true;  // Lock: user intentionally removed đ
                ProcessChar(c, rawInput_.size() - 1);
                return true;
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
                ProcessChar(c, rawInput_.size() - 1);
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
        ProcessChar(c, rawInput_.size() - 1);
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
    return FindToneTargetImpl(kDiphthongClassic, false);
}

CharState* VniEngine::FindToneTargetModern() {
    return FindToneTargetImpl(kDiphthongModern, true);
}

CharState* VniEngine::FindToneTargetImpl(const uint8_t table[6][6], bool checkTriphthongs) {
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

    if (vowelCount == 0) return nullptr;

    // Priority 1: Horn vowels (last one for ươ)
    size_t lastHornIdx = SIZE_MAX;
    for (size_t k = 0; k < vowelCount; ++k) {
        if (states_[vowels[k]].mod == Modifier::Horn) lastHornIdx = vowels[k];
    }
    if (lastHornIdx != SIZE_MAX) return &states_[lastHornIdx];

    // Priority 2: Other modified vowels (â, ê, ô, ă)
    for (size_t k = 0; k < vowelCount; ++k) {
        if (states_[vowels[k]].mod != Modifier::None) return &states_[vowels[k]];
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
                            return &states_[midIdx];
                    }
                }
            }

            // Diphthong table lookup
            int fi = DiphthongVowelIndex(states_[prevIdx].base);
            int li = DiphthongVowelIndex(states_[lastIdx].base);
            if (fi >= 0 && li >= 0) {
                uint8_t rule = table[fi][li];
                if (rule == 1) return &states_[prevIdx];   // tone on FIRST
                if (rule == 2) return &states_[lastIdx];    // tone on SECOND
            }
        }
    }

    // Default: rightmost vowel
    return &states_[vowels[vowelCount - 1]];
}

//-----------------------------------------------------------------------------
// Character Processing
//-----------------------------------------------------------------------------

void VniEngine::ProcessChar(wchar_t c, size_t rawIdx) {
    CharState state;
    state.base = towlower(c);
    state.isUpper = iswupper(c);
    state.rawIdx = rawIdx;
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
    UpdateSpellCheck(states_.data(), states_.size(), config_, tempSpellOff_, spellCheckDisabled_);
}

}  // namespace Vni
}  // namespace NextKey
