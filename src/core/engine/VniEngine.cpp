// NexusKey - VNI Input Method Engine Implementation (Optimized)
// Copyright (c) 2024-2026 PhatMT. All rights reserved.
// SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-NexusKey-Commercial
// Dual-licensed: GPL-3.0 for open-source use, commercial license for proprietary use.
// See LICENSE and LICENSE-COMMERCIAL in the project root.
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

// IsVowelChar is shared — defined in VietnameseTables.h

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
// VniEngine Implementation
//=============================================================================

VniEngine::VniEngine(const TypingConfig& config) : config_(config) {
    states_.reserve(8);
    rawInput_.reserve(12);
}

void VniEngine::PushChar(wchar_t c) {
    // Guard: cap buffer size to prevent unbounded memory growth.
    if (rawInput_.size() >= 64) return;

    rawInput_ += c;
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
                qc_.idx = states_.size();
                qc_.lastKey = lower;  // Save ORIGINAL key before update
                c = isUpper ? towupper(replacement) : replacement;
                lower = towlower(c);
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
                qc_.idx = states_.size() - 1;
                if (states_.size() == 2) qc_.onlyQC = true;
                qc_.lastKey = lower;  // Suppress re-trigger on consecutive same key
                UpdateSpellState();
                return;
            }
            // Triphthong — fall through to normal processing
        }
    }

    // 1. Try tone keys (1-5) — gated by spell check + English protection
    if (IsToneKey(c)) {
        // All "treat as literal" paths share the same two operations.
        auto asLiteral = [&] { ProcessChar(c, rawInput_.size() - 1, lower, isUpper); UpdateSpellState(); };

        // Cache FindToneTarget result from spell-check gate to avoid redundant call
        // in ProcessTone. nullptr means "not yet computed".
        CharState* cachedToneTarget = nullptr;
        bool hasCachedTarget = false;

        if (config_.spellCheckEnabled && spellCheckDisabled_) {
            // Allow tone escape (33, 11, etc.) even when spell check disabled:
            // the user is canceling a tone they already applied (e.g. oc33 → oc3).
            cachedToneTarget = FindToneTarget();
            hasCachedTarget = true;
            CharState* t = cachedToneTarget;
            bool isEscape = (t && t->tone == KeyToTone(c));
            // Allow tone if applying it would match a spell exclusion (e.g. "kà").
            bool matchesExclusion = false;
            if (!isEscape && !config_.spellExclusions.empty() && t) {
                size_t tIdx = static_cast<size_t>(t - states_.data());
                CharState tentative = *t;
                tentative.tone = KeyToTone(c);
                wchar_t tonedCh = ComposeChar(tentative);
                matchesExclusion = WouldToneMatchExclusion(states_.data(), states_.size(),
                    config_.spellExclusions,
                    [this](const CharState& s) { return ComposeChar(s); },
                    tIdx, tonedCh);
            }
            if (!isEscape && !matchesExclusion) { asLiteral(); return; }
            // Fall through: either tone escape or exclusion match
        }
        // Tone escape: user pressed same tone key twice — blocks Vietnamese.
        if (escape_.isEscaped())                                { asLiteral(); return; }
        // English Protection: always active, independent of spell check.
        if (!config_.allowEnglishBypass) {
            if (engProt_.bias == LanguageBias::HardEnglish)         { asLiteral(); return; }
            if (engProt_.bias == LanguageBias::SoftEnglish) {
                if (!UpdateToneInsistence(c, engProt_))              { asLiteral(); return; }
                // User insisted (same key twice) — fall through to apply tone
            }
            // Structural hard-English: V+C+V pattern is impossible in Vietnamese syllables.
            if (IsHardEnglishToneContext(states_.data(), states_.size(), c)) {
                engProt_.bias = LanguageBias::HardEnglish;
                asLiteral(); return;
            }
            // Adjacent plain vowel pair with no Vietnamese diphthong rule
            // (e.g., 'ea' in "search", 'ae' in "aerial", 'oy' in "boy").
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
        if (ProcessTone(c, hasCachedTarget ? cachedToneTarget : nullptr)) {
            if (!escape_.isEscaped()) {
                engProt_.bias = LanguageBias::Vietnamese;  // Tone applied → VN intent
            } else {
                // Tone escaped (e.g., 11) → user is canceling Vietnamese.
                RecalcEnglishBias(states_.data(), states_.size(), engProt_);
                CheckZwjfInitialBias(states_.data(), states_.size(), config_, engProt_);
            }
            UpdateSpellState();
            return;
        }
    }

    // 2. Try modifier keys (6-9)
    // Modifiers can transform invalid sequences into valid ones
    // However, if we're clearly in an English word, skip modifiers
    if (IsModifierKey(c)) {
        bool blockMod = escape_.isEscaped() ||
            (!config_.allowEnglishBypass && engProt_.bias == LanguageBias::HardEnglish);
        // Allow modifiers through English Protection when the word matches a spell exclusion.
        if (blockMod && !escape_.isEscaped() && !config_.spellExclusions.empty()) {
            if (WouldModifierKeyMatchExclusion(c))
                blockMod = false;
        }
        if (blockMod) {
            // Don't try modifiers — treat as literal
        } else if (config_.spellCheckEnabled && spellCheckDisabled_) {
            // PREVENT modifier application if sequence is already structurally invalid.
            // But allow modifier ESCAPE (77 undoes horn, 99 undoes stroke, 66/88 undoes
            // circumflex/breve) — same principle as tone escape bypass.
            Modifier escapeMod = Modifier::None;
            switch (c) {
                case L'6': escapeMod = Modifier::Circumflex; break;
                case L'7': escapeMod = Modifier::Horn; break;
                case L'8': escapeMod = Modifier::Breve; break;
                case L'9': escapeMod = Modifier::Stroke; break;
            }
            bool canExclude = WouldModifierKeyMatchExclusion(c);
            if ((canExclude || (escapeMod != Modifier::None &&
                HasEscapableModifier(states_.data(), states_.size(), escapeMod,
                                     escapeMod == Modifier::Stroke))) &&
                ProcessModifier(c)) {
                if (engProt_.bias == LanguageBias::HardEnglish ||
                    engProt_.bias == LanguageBias::SoftEnglish) {
                    engProt_.bias = LanguageBias::Vietnamese;
                }
                UpdateSpellState();
                return;
            }
        } else if (ProcessModifier(c)) {
            if (engProt_.bias == LanguageBias::HardEnglish ||
                engProt_.bias == LanguageBias::SoftEnglish) {
                engProt_.bias = LanguageBias::Vietnamese;
            }
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
    ProcessChar(c, rawInput_.size() - 1, lower, isUpper);
    ApplyAutoUO();
    RelocateToneToTarget();
    UpdateSpellState();

    // English Protection: re-evaluate bias after adding character
    // (always active — independent of spell check setting)
    CheckEnglishBias(states_.data(), states_.size(), engProt_);
    CheckZwjfInitialBias(states_.data(), states_.size(), config_, engProt_);
}

void VniEngine::Backspace() {
    if (states_.empty()) return;
    escape_.clear();  // Allow đ re-trigger + Vietnamese re-trigger after user edits

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

    // Undo quick consonant expansion — restore original key in-place.
    // e.g., "aph" + BS → "app" (not "ap"), "rieng" + BS → "rienn".
    if (qc_.hasActive() && states_.size() - 1 == qc_.idx) {
        UndoHornU(states_.data(), states_.size() - 1);

        wchar_t originalKey = qc_.lastKey;
        qc_.markEscaped();  // escaped=true, clearActive (idx+lastKey)

        states_.pop_back();
        // rawInput_ still holds the original triggering key — don't trim it.
        if (originalKey != 0) {
            ProcessChar(originalKey, rawInput_.size() - 1);
        }

        UpdateSpellState();
        RecalcEnglishBias(states_.data(), states_.size(), engProt_);
        CheckZwjfInitialBias(states_.data(), states_.size(), config_, engProt_);
        return;
    }
    qc_.clearActive();

    // Trim rawInput_ to the position when this state was created.
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

const std::wstring& VniEngine::Peek() const {
    composeBuf_.clear();
    for (const auto& s : states_) {
        composeBuf_ += ComposeChar(s);
    }
    return composeBuf_;
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
    if (qc_.onlyQC) {
        std::wstring raw = rawInput_;
        if (ShouldAutoRestore(raw, composed)) {
            Reset();
            return raw;
        }
    }

    // Guard: skip auto-restore when any of these are true:
    //  - spell check / auto-restore not enabled
    //  - an active quick consonant (nn→ng, cc→ch, etc.) was applied — user did
    //    this intentionally; restoring would undo the conversion they wanted.
    //    (qc_.idx is SIZE_MAX when no quick consonant is active)
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
            // Spell exclusion list takes priority. HasIntentionalStrokeD heuristic
            // also protects abbreviations like đt, đh while restoring đwa, ăndd.
            bool excluded = IsSpellExcluded(states_.data(), states_.size(),
                                            config_.spellExclusions,
                                            [this](const CharState& s) { return ComposeChar(s); });
            bool keepComposed = excluded ||
                                HasIntentionalStrokeD(rawInput_, composed);
            if (!keepComposed) {
                std::wstring raw = rawInput_;
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

void VniEngine::Reset() {
    states_.clear();
    rawInput_.clear();
    spellCheckDisabled_ = false;
    qc_.Reset();
    escape_.clear();
    quickStartKey_ = 0;
    engProt_.Reset();
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

    // Handle đ specially (key 9) — scan logic shared via FindStrokeDTarget (EngineHelpers.h)
    if (targetMod == Modifier::Stroke) {
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
            ProcessChar(c, rawInput_.size() - 1);
            return true;
        }
        return false;
    }

    // Handle vowel modifiers (6, 7, 8)

    // Horn on "uo" pair: cycle logic (same as Telex ProcessWModifier P2)
    // Default: uo → ươ (one press). h/th/kh: ươ → uơ → uo (three-press).
    // Others: ươ → uo (two-press). QU cluster handled by IsClusterConsonant.
    if (targetMod == Modifier::Horn) {
        size_t pairU = SIZE_MAX, pairO = SIZE_MAX;
        for (size_t i = 0; i + 1 < states_.size(); ++i) {
            if (states_[i].IsVowel() && states_[i+1].IsVowel() &&
                states_[i].base == L'u' && states_[i+1].base == L'o') {
                if (IsClusterConsonant(states_.data(), states_.size(), i)) continue;
                pairU = i; pairO = i + 1;
            }
        }
        if (pairU != SIZE_MAX) {
            Modifier uMod = states_[pairU].mod;
            Modifier oMod = states_[pairO].mod;
            bool isEdge = IsUOEdgeCasePrefix(states_.data(), states_.size(), pairU);

            // Forward: uo → ươ (first press)
            if (uMod == Modifier::None && oMod == Modifier::None) {
                states_[pairU].mod = Modifier::Horn;
                states_[pairO].mod = Modifier::Horn;
                return true;
            }
            // Forward: ưo → ươ (u already horned, complete pair)
            if (uMod == Modifier::Horn && oMod == Modifier::None) {
                states_[pairO].mod = Modifier::Horn;
                return true;
            }
            // Cycle: ươ → uơ (h/th/kh second press)
            if (uMod == Modifier::Horn && oMod == Modifier::Horn && isEdge) {
                states_[pairU].mod = Modifier::None;
                return true;
            }
            // Escape: ươ → uo (non h/th/kh second press)
            if (uMod == Modifier::Horn && oMod == Modifier::Horn && !isEdge) {
                states_[pairU].mod = Modifier::None;
                states_[pairO].mod = Modifier::None;
                escape_.escape(EscapeKind::Horn);
                ProcessChar(c, rawInput_.size() - 1);
                return true;
            }
            // Escape: uơ → uo (h/th/kh third press)
            if (uMod == Modifier::None && oMod == Modifier::Horn) {
                states_[pairO].mod = Modifier::None;
                escape_.escape(EscapeKind::Horn);
                ProcessChar(c, rawInput_.size() - 1);
                return true;
            }
        }
    }

    // Special: "uu" pattern → horn on FIRST 'u' (lưu, cưu, hưu)
    // Same logic as Telex ProcessWModifier P5.
    if (targetMod == Modifier::Horn) {
        size_t firstU = SIZE_MAX, lastU = SIZE_MAX;
        for (size_t i = 0; i < states_.size(); ++i) {
            if (!states_[i].IsVowel() || states_[i].base != L'u') continue;
            if (IsClusterConsonant(states_.data(), states_.size(), i)) continue;
            if (states_[i].mod != Modifier::None) continue;
            if (firstU == SIZE_MAX) firstU = i;
            lastU = i;
        }
        if (firstU != SIZE_MAX && lastU != firstU) {
            states_[firstU].mod = Modifier::Horn;
            return true;
        }
    }

    // Generic three-pass scan for remaining modifier cases (non-uo horn, circumflex, breve)
    // Pass 1: find rightmost unmodified vowel that accepts this modifier
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

        if (canApply && it->mod == Modifier::None) {
            it->mod = targetMod;
            return true;
        }
    }

    // Pass 1.5: switch between compatible modifiers on the same vowel
    // e.g., â(circumflex)+8→ă(breve), ă(breve)+6→â(circumflex), ô(circumflex)+7→ơ(horn)
    for (auto it = states_.rbegin(); it != states_.rend(); ++it) {
        if (!it->IsVowel()) continue;
        wchar_t base = towlower(it->base);

        bool canSwitch = false;
        switch (targetMod) {
            case Modifier::Circumflex:
                // ă→â (breve→circumflex on 'a'), ơ→ô (horn→circumflex on 'o')
                canSwitch = (base == L'a' && it->mod == Modifier::Breve) ||
                            (base == L'o' && it->mod == Modifier::Horn);
                break;
            case Modifier::Horn:
                // ô→ơ (circumflex→horn on 'o')
                canSwitch = (base == L'o' && it->mod == Modifier::Circumflex);
                break;
            case Modifier::Breve:
                // â→ă (circumflex→breve on 'a')
                canSwitch = (base == L'a' && it->mod == Modifier::Circumflex);
                break;
            default: break;
        }

        if (canSwitch) {
            it->mod = targetMod;
            return true;
        }
    }

    // Pass 2: no unmodified target — escape the rightmost vowel with matching modifier
    for (auto it = states_.rbegin(); it != states_.rend(); ++it) {
        if (!it->IsVowel()) continue;
        if (it->mod == targetMod) {
            it->mod = Modifier::None;
            escape_.escape(EscapeKind::Modifier);
            ProcessChar(c, rawInput_.size() - 1);
            return true;
        }
    }

    return false;
}

//-----------------------------------------------------------------------------
// Tone Processing (1-5)
//-----------------------------------------------------------------------------

bool VniEngine::ProcessTone(wchar_t c, CharState* cachedTarget) {
    Tone newTone = KeyToTone(c);
    if (newTone == Tone::None) return false;

    CharState* target = cachedTarget ? cachedTarget : FindToneTarget();
    if (!target) return false;

    escape_.clear();
    if (target->tone == Tone::None) {
        target->tone = newTone;
        return true;
    } else if (target->tone == newTone) {
        target->tone = Tone::None;
        ProcessChar(c, rawInput_.size() - 1);
        escape_.escape(EscapeKind::Tone);
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
    size_t idx = NextKey::FindToneTargetImpl(states_.data(), states_.size(), table, checkTriphthongs);
    return (idx == SIZE_MAX) ? nullptr : &states_[idx];
}

//-----------------------------------------------------------------------------
// Character Processing
//-----------------------------------------------------------------------------

void VniEngine::ProcessChar(wchar_t /*c*/, size_t rawIdx, wchar_t lower, bool isUpper) {
    CharState state;
    state.base = lower;
    state.isUpper = isUpper;
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
// Auto horn companion: complete ươ pair when a char is typed AFTER the pair.
// Requires i + 2 < size: the pair must NOT be the last two states.
// Exception: h/th/kh prefixes skip Pattern 1 to preserve intentional uơ (thuở, huơ).
//-----------------------------------------------------------------------------

void VniEngine::ApplyAutoUO() {
    if (states_.size() < 3 || escape_.isEscaped()) return;

    size_t start = (states_.size() > 4) ? states_.size() - 4 : 0;
    for (size_t i = start; i + 2 < states_.size(); ++i) {
        // Skip QU cluster
        if (i > 0 && states_[i].base == L'u' && states_[i - 1].base == L'q') continue;

        // Pattern 1: u(plain) + ơ(horn) → horn the u
        // Exception: h/th/kh prefix — user may have intentionally chosen uơ (thuở, huơ)
        if (states_[i].base == L'u' && states_[i].mod == Modifier::None &&
            states_[i + 1].base == L'o' && states_[i + 1].mod == Modifier::Horn) {
            if (!IsUOEdgeCasePrefix(states_.data(), states_.size(), i)) {
                states_[i].mod = Modifier::Horn;
            }
        }

        // Pattern 2: ư(horn) + o(plain) → horn the o
        if (states_[i].base == L'u' && states_[i].mod == Modifier::Horn &&
            states_[i + 1].base == L'o' && states_[i + 1].mod == Modifier::None) {
            states_[i + 1].mod = Modifier::Horn;
        }
    }
}

//-----------------------------------------------------------------------------
// Spell Check State Update
//-----------------------------------------------------------------------------

void VniEngine::RelocateToneToTarget() {
    CharState* toned = nullptr;
    for (auto& s : states_) {
        if (s.IsVowel() && s.tone != Tone::None) {
            toned = &s;
            break;
        }
    }
    if (!toned) return;

    CharState* target = FindToneTarget();
    if (!target || target == toned) return;

    size_t targetIdx = static_cast<size_t>(target - states_.data());
    if (IsToneRelocBlockedByP4(states_.data(), states_.size(), targetIdx, config_.modernOrtho))
        return;

    target->tone = toned->tone;
    toned->tone = Tone::None;
}

bool VniEngine::WouldModifierKeyMatchExclusion(wchar_t key) const {
    auto compose = [this](const CharState& s) { return ComposeChar(s); };
    switch (key) {
        case L'6': return WouldAnyModifierMatchExclusion(states_.data(), states_.size(),
                       config_.spellExclusions, compose, Modifier::Circumflex, L"aeo");
        case L'7': return WouldAnyModifierMatchExclusion(states_.data(), states_.size(),
                       config_.spellExclusions, compose, Modifier::Horn, L"uo");
        case L'8': return WouldAnyModifierMatchExclusion(states_.data(), states_.size(),
                       config_.spellExclusions, compose, Modifier::Breve, L"a");
        case L'9': return WouldStrokeDMatchExclusion(states_.data(), states_.size(),
                       config_.spellExclusions, compose);
        default: return false;
    }
}

void VniEngine::UpdateSpellState() {
    UpdateSpellCheck(states_.data(), states_.size(), config_, spellCheckDisabled_,
                     [this](const CharState& s) { return ComposeChar(s); });
}

}  // namespace Vni
}  // namespace NextKey
